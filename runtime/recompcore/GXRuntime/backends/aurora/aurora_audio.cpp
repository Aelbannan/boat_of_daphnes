// SPDX-License-Identifier: GPL-3.0-or-later
#include "aurora_backend_private.h"
#include "audio_stretch.hpp"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <vector>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_init.h>
#include <cstdio>

namespace {
// Stretch control (DOL_AUDIO_STRETCH=0 turns it off). The factor aims the
// queue at kStretchTargetMs: 1 at or above it, rising to kStretchMax as the
// queue empties, smoothed over about 100 ms of pushes. Nothing is stretched
// before playback starts, so the prebuffer fills with the game's own timing.
constexpr double kStretchTargetMs = 100.0;
constexpr double kStretchMax = 2.0;
gx_aurora::AudioStretcher g_stretcher;
// The player's master volume (0..1), applied as the output stream's gain on
// every push so it survives the stream being opened again.
std::atomic<float> g_master_volume{1.0f};
float g_master_applied = -1.0f;
std::vector<int16_t> g_stretch_out;
double g_stretch = 1.0;
FILE* g_sink_capture;
bool g_sink_capture_checked;
u64 g_sink_capture_bytes;
u32 g_sink_capture_rate;
void capture_sink(const s16* samples, int bytes) {
    if (!g_sink_capture_checked) {
        g_sink_capture_checked = true;
        const char* path = std::getenv("DOL_AUDIO_SINK_CAPTURE");
        if (path && path[0]) {
            // Never overwrite an earlier diagnostic capture.
            g_sink_capture = std::fopen(path, "wbx");
            std::fprintf(stderr, "[audio-sink] capture=%s opened=%u format=S16-native channels=L,R\n", path, g_sink_capture ? 1u : 0u);
        }
    }
    if (!g_sink_capture || bytes <= 0) return;
    if (g_sink_capture_rate != gx_aurora::g_audio_sample_rate) {
        g_sink_capture_rate = gx_aurora::g_audio_sample_rate;
        std::fprintf(stderr, "[audio-sink] offset_bytes=%llu sample_rate=%u\n",
            (unsigned long long)g_sink_capture_bytes, g_sink_capture_rate);
    }
    if (std::fwrite(samples, 1, bytes, g_sink_capture) != static_cast<size_t>(bytes)) {
        std::fprintf(stderr, "[audio-sink] capture write failed\n");
        std::fclose(g_sink_capture); g_sink_capture = nullptr;
    } else g_sink_capture_bytes += bytes;
}
// DOL_AUDIO_DEVICE_CAPTURE=path: the final mix as the device gets it (after
// SDL's resampling and mixing), float32 stereo at the device rate, from SDL's
// postmix callback. Diagnostic only; the callback is not installed otherwise.
FILE* g_dev_capture;
SDL_AudioSpec g_dev_spec;
void SDLCALL device_capture_cb(void*, const SDL_AudioSpec* spec, float* buffer, int buflen) {
    if (g_dev_capture) {
        if (spec->freq != g_dev_spec.freq || spec->channels != g_dev_spec.channels) {
            g_dev_spec = *spec;
            std::fprintf(stderr, "[audio-device] capture format rate=%d channels=%d format=0x%x\n",
                         spec->freq, spec->channels, (unsigned)spec->format);
        }
        std::fwrite(buffer, 1, static_cast<size_t>(buflen), g_dev_capture);
    }
}
void install_device_capture(SDL_AudioStream* stream) {
    const char* path = std::getenv("DOL_AUDIO_DEVICE_CAPTURE");
    if (!path || !path[0] || g_dev_capture) return;
    SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(stream);
    SDL_AudioSpec fmt; int frames = 0;
    if (!device || !SDL_GetAudioDeviceFormat(device, &fmt, &frames)) return;
    g_dev_capture = std::fopen(path, "wbx");
    std::fprintf(stderr, "[audio-device] capture=%s opened=%u device_rate=%d channels=%d buffer_frames=%d\n",
                 path, g_dev_capture ? 1u : 0u, fmt.freq, fmt.channels, frames);
    if (g_dev_capture) SDL_SetAudioPostmixCallback(device, device_capture_cb, nullptr);
}
// Per-interval counters for the once-a-second [fps] line. Only the guest
// thread (the pusher) writes them; the reader runs on the same thread in the
// Windows app, and a torn read elsewhere would only skew one diagnostic line.
struct IntervalStats {
    u32 pushes = 0, frames_in = 0, starved = 0, underruns = 0, stretched = 0, dropped = 0;
    double underrun_ms = 0.0;
    int queue_min_ms = 1 << 30, queue_max_ms = 0;
    double max_gap_ms = 0.0, stretch_sum = 0.0;
};
IntervalStats g_stats;
Uint64 g_prev_push_ns;      // the previous push, for the gap and the silence it implies
double g_prev_queue_ms;     // what the queue held right after it
// DOL_AUDIO_PUSH_TRACE=path: one record per push (wall ns, frames, queue bytes
// before it), for replaying the producer's timing offline. Diagnostic only.
FILE* g_push_trace;
bool g_push_trace_checked;
struct PushRecord { u64 ns; u32 frames; s32 queued; };
PushRecord g_trace_buf[4096];
u32 g_trace_n;
void trace_flush() {
    if (g_push_trace && g_trace_n)
        std::fwrite(g_trace_buf, sizeof(PushRecord), g_trace_n, g_push_trace);
    g_trace_n = 0;
}
void trace_push(u64 ns, u32 frames, int queued) {
    if (!g_push_trace_checked) {
        g_push_trace_checked = true;
        const char* path = std::getenv("DOL_AUDIO_PUSH_TRACE");
        if (path && path[0]) {
            g_push_trace = std::fopen(path, "wbx");
            std::fprintf(stderr, "[audio-trace] push trace=%s opened=%u rate=%u\n", path, g_push_trace ? 1u : 0u,
                         (unsigned)gx_aurora::g_audio_sample_rate);
        }
    }
    if (!g_push_trace) return;
    g_trace_buf[g_trace_n++] = PushRecord{ns, frames, queued};
    if (g_trace_n == 4096) trace_flush();
}
// Tempo control for the stretcher. The guest produces audio on the game thread,
// so when the game runs at a fraction of full speed the sound arrives at that
// fraction of real time and the device runs dry unless playback slows to match.
// The earlier rule (a factor from the queue depth alone, smoothed per push, and
// pushes are 8 frames = 0.25 ms) swung between 1x and 2x inside every burst of
// pushes and kept the target depth (100 ms) above what the queue held at full
// speed, so SOLA was almost always splicing and the queue still ran dry.
// This one follows the measured production speed (seconds-long average), adds a
// slow correction toward a target depth, and sizes that depth from the longest
// stall between pushes seen in the last few seconds. At speed 1 it passes the
// samples through untouched.
// Measured on a quiet PC (2026-10-09, docs/fork/audio-static.md): the earlier
// rule left less silence at the device in every scene (sailing 4 ms against
// 275 ms in 30 s), so it stays the default; the pacer is opt-in.
//   DOL_AUDIO_POLICY=pacer    this rule (default: the earlier one)
//   DOL_AUDIO_TARGET_MS=n     minimum target depth (default 80)
//   DOL_AUDIO_TAU_MS=n        time to repay a depth error (default 400)
struct Pacer {
    double speed = 1.0;        // production speed, 1 = real time
    double target_ms = 80.0;
    double factor = 1.0;       // stretch: output length over input length
    Uint64 window_ns = 0, last_push_ns = 0;
    u64 frames_total = 0, window_frames = 0;
    double slot_gap[4] = {0, 0, 0, 0};  // longest push gap per second, 4 s
    Uint64 slot_start_ns = 0;
    int slot = 0;
    void reset() { *this = Pacer{}; }
    // Called once per push (frames at `rate`), returns the stretch to use.
    double update(Uint64 now_ns, u32 frames, u32 rate, double level_ms, bool playing,
                  double base_ms, double tau_ms) {
        frames_total += frames;
        window_frames += frames;
        if (!playing) { // before playback nothing is stretched and nothing is learnt
            window_ns = now_ns; window_frames = 0; last_push_ns = now_ns; slot_start_ns = now_ns;
            speed = 1.0; factor = 1.0;
            return 1.0;
        }
        if (now_ns - slot_start_ns >= 1000000000ull) {
            slot = (slot + 1) & 3; slot_gap[slot] = 0; slot_start_ns = now_ns;
        }
        const double gap_ms = static_cast<double>(now_ns - last_push_ns) / 1e6;
        last_push_ns = now_ns;
        if (gap_ms > 2.0 && gap_ms > slot_gap[slot]) slot_gap[slot] = gap_ms;
        const double dt_ms = static_cast<double>(now_ns - window_ns) / 1e6;
        if (dt_ms >= 100.0) {
            if (dt_ms < 500.0) { // a longer silence is a stall, not a speed
                double inst = window_frames * 1000.0 / (static_cast<double>(rate) * dt_ms);
                if (inst > 1.5) inst = 1.5;
                const double k = 1.0 - std::exp(-dt_ms / 1000.0);
                speed += k * (inst - speed);
            }
            window_ns = now_ns; window_frames = 0;
            double worst = 0;
            for (double g : slot_gap) if (g > worst) worst = g;
            target_ms = std::min(300.0, std::max(base_ms, 1.5 * worst + 40.0));
            double p = speed + (level_ms - target_ms) / tau_ms;
            if (level_ms < target_ms * 0.5) p *= 0.6 + 0.8 * level_ms / target_ms; // nearly dry: slow faster
            p = std::min(1.0, std::max(0.4, p));
            const double f = 1.0 / p;
            // Hysteresis: SOLA starts only above 3 percent, and leaves below 1.
            factor = factor > 1.0 ? (f > 1.01 ? f : 1.0) : (f > 1.03 ? f : 1.0);
        }
        return factor;
    }
};
Pacer g_pacer;
bool legacy_policy() {
    static const bool legacy = [] {
        const char* env = std::getenv("DOL_AUDIO_POLICY");
        return env == nullptr || env[0] != 'p';
    }();
    return legacy;
}
double env_double(const char* name, double fallback) {
    const char* env = std::getenv(name);
    return env && env[0] ? std::atof(env) : fallback;
}
bool stretch_enabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("DOL_AUDIO_STRETCH");
        return env == nullptr || env[0] != '0';
    }();
    return enabled;
}
}  // namespace

namespace gx_aurora {
static std::atomic_bool resume_requested{false};
void close_audio_capture() {
    trace_flush();
    if (g_push_trace) { std::fclose(g_push_trace); g_push_trace = nullptr; }
    g_push_trace_checked = false;
    if (g_sink_capture && std::fclose(g_sink_capture) != 0)
        std::fprintf(stderr, "[audio-sink] capture close failed\n");
    g_sink_capture = nullptr;
    g_sink_capture_checked = false;
    g_sink_capture_bytes = 0;
    g_sink_capture_rate = 0;
    if (g_dev_capture) {
        SDL_AudioDeviceID device = g_audio_stream ? SDL_GetAudioStreamDevice(g_audio_stream) : 0;
        if (device) SDL_SetAudioPostmixCallback(device, nullptr, nullptr);
        std::fclose(g_dev_capture); g_dev_capture = nullptr;
    }
}
void recover_audio_output() {
    static Uint64 next_check;
    bool requested = resume_requested.exchange(false, std::memory_order_relaxed);
    Uint64 now = SDL_GetTicks();
    if (!requested && now < next_check) return;
    next_check = now + 250;
    if (g_audio_stream) {
        SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(g_audio_stream);
        SDL_AudioSpec format;
        if (!device || !SDL_GetAudioDeviceFormat(device, &format, nullptr)) {
            SDL_DestroyAudioStream(g_audio_stream); g_audio_stream = nullptr;
            g_audio_playing = false;
        } else if (SDL_AudioDevicePaused(device)) g_audio_playing = false;
    }
    if (requested) retry_audio_open(true);
    // A paused device can already hold a full prebuffer. Resume it before
    // the push path drops overflow/discard samples and returns without
    // reaching its ordinary start-playback check.
    if (g_audio_stream && !g_audio_playing) {
        const int queued = SDL_GetAudioStreamQueued(g_audio_stream);
        const int prebuffer = static_cast<int>(g_audio_sample_rate) * 2 * sizeof(s16) * g_audio_prebuffer_ms / 1000;
        if (queued >= prebuffer) {
            if (SDL_ResumeAudioStreamDevice(g_audio_stream))
                g_audio_playing = true;
            else
                std::fprintf(stderr, "[audio] failed to resume queued playback: %s\n", SDL_GetError());
        }
    }
}
bool retry_audio_open(bool force) {
    if (g_audio_stream != nullptr) return true;
    static Uint64 next_attempt;
    Uint64 now = SDL_GetTicks();
    if (!force && now < next_attempt) return false;
    next_attempt = now + 2000;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "[audio] output unavailable; retrying in 2s: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16; spec.channels = 2; spec.freq = static_cast<int>(g_audio_sample_rate);
    g_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    g_audio_playing = false;
    if (!g_audio_stream) {
        std::fprintf(stderr, "[audio] output unavailable; retrying in 2s: %s\n", SDL_GetError());
        return false;
    }
    g_stretcher = AudioStretcher(); g_stretch_out.clear(); g_stretch = 1.0; g_pacer.reset();
    std::fprintf(stderr, "[audio] output available; prebuffering\n");
    install_device_capture(g_audio_stream);
    return true;
}
} // namespace gx_aurora

extern "C" {
// Settings > Audio > Master Volume: 0 (silent) to 1 (the game's own level).
void dol_aurora_audio_set_master_volume(float volume) {
    g_master_volume.store(volume < 0.0f ? 0.0f : volume > 1.0f ? 1.0f : volume, std::memory_order_relaxed);
}

void dol_aurora_audio_resume(void) {
    gx_aurora::resume_requested.store(true, std::memory_order_relaxed);
}


void aurora_backend_audio_set_sample_rate(u32 sample_rate) {
    if (sample_rate != 48000)
        sample_rate = 32000;
    if (gx_aurora::g_audio_sample_rate == sample_rate)
        return;
    gx_aurora::g_audio_sample_rate = sample_rate;
    if (gx_aurora::g_audio_stream == nullptr)
        return;

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(gx_aurora::g_audio_sample_rate);
    if (!SDL_SetAudioStreamFormat(gx_aurora::g_audio_stream, &spec, nullptr))
        std::fprintf(stderr, "[audio] failed to set input rate %u: %s\n",
                     gx_aurora::g_audio_sample_rate, SDL_GetError());
    else if (gx_aurora::g_audio_queue_log)
        std::fprintf(stderr, "[audio-queue] input-rate=%u\n",
                     gx_aurora::g_audio_sample_rate);
}

void dol_aurora_audio_interval(DolAuroraAudioStats* out) {
    auto& st = g_stats;
    out->pushes = st.pushes;
    out->frames_in = st.frames_in;
    out->starved = st.starved;
    out->underruns = st.underruns;
    out->underrun_ms = static_cast<unsigned>(st.underrun_ms + 0.5);
    out->stretched = st.stretched;
    out->queue_min_ms = st.pushes ? st.queue_min_ms : 0;
    out->queue_max_ms = st.queue_max_ms;
    out->max_gap_ms = static_cast<unsigned>(st.max_gap_ms + 0.5);
    out->stretch_x100 = st.pushes ? static_cast<int>(100.0 * st.stretch_sum / st.pushes + 0.5) : 100;
    st = IntervalStats{};
}

void aurora_backend_audio_push(const s16* samples, u32 frames) {
    if (samples == nullptr || frames == 0) return;
    gx_aurora::recover_audio_output();
    gx_aurora::g_audio_push_count++;
    if (!gx_aurora::retry_audio_open(false)) {
        gx_aurora::g_audio_dropped_count++;
        gx_aurora::g_audio_dropped_frames += frames;
        return;
    }
    const int bytes_per_second =
        static_cast<int>(gx_aurora::g_audio_sample_rate) * 2 * static_cast<int>(sizeof(s16));
    const int prebuffer_bytes = bytes_per_second * gx_aurora::g_audio_prebuffer_ms / 1000;
    const int max_queued_bytes = bytes_per_second * gx_aurora::g_audio_max_queue_ms / 1000;
    const int bytes = static_cast<int>(frames * 2u * sizeof(s16));
    u32 nonzero_samples = 0;
    s32 peak_sample = 0;
    u32 sample_hash = 2166136261u;
    for (u32 i = 0; i < frames * 2u; i++) {
        const s32 sample = samples[i];
        if (sample != 0)
            nonzero_samples++;
        const s32 magnitude = sample < 0 ? -sample : sample;
        if (magnitude > peak_sample)
            peak_sample = magnitude;
        sample_hash ^= static_cast<u16>(sample);
        sample_hash *= 16777619u;
    }

    int queued = SDL_GetAudioStreamQueued(gx_aurora::g_audio_stream);
    if (queued < 0) {
        gx_aurora::g_audio_dropped_count++;
        gx_aurora::g_audio_dropped_frames += frames;
        SDL_DestroyAudioStream(gx_aurora::g_audio_stream);
        gx_aurora::g_audio_stream = nullptr;
        gx_aurora::g_audio_playing = false;
        return;
    }
    // Fast-forward (dol_aurora_set_fast_forward): the sound made faster than
    // real time is kept only up to the stretcher's target, so the queue neither
    // holds it late after the fast-forward nor runs dry during it.
    if (gx_aurora::g_audio_discard.load(std::memory_order_relaxed) &&
        queued >= bytes_per_second * static_cast<int>(kStretchTargetMs) / 1000) {
        gx_aurora::g_audio_dropped_count++;
        gx_aurora::g_audio_dropped_frames += frames;
        return;
    }
    // With the host pacing retraces by the wall clock (BlueWake's
    // BLUEWAKE_WALL_PACE), the queue must not pace the game as well: its 1 ms
    // waits added up to about 35 ms whenever the device drained a buffer, and
    // two or three frames a second stayed on screen for 60 ms. A push that
    // would overfill the queue is dropped instead and counted.
    static const bool no_throttle = [] {
        const char* env = std::getenv("DOL_AUDIO_NO_THROTTLE");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    if (no_throttle && queued > max_queued_bytes) {
        gx_aurora::g_audio_dropped_count++;
        gx_aurora::g_audio_dropped_frames += frames;
        return;
    }
    unsigned waited_ms = 0;
    while (queued > max_queued_bytes && waited_ms < 20u) {
        gx_aurora::g_audio_throttle_count++;
        if (gx_aurora::g_audio_queue_log &&
            (gx_aurora::g_audio_throttle_count <= 8 ||
             (gx_aurora::g_audio_throttle_count % 100) == 0))
            std::fprintf(stderr,
                         "[audio-queue] throttle push=%llu waits=%llu queued=%d "
                         "max=%d\n",
                         gx_aurora::g_audio_push_count, gx_aurora::g_audio_throttle_count, queued,
                         max_queued_bytes);
        SDL_Delay(1);
        waited_ms++;
        queued = SDL_GetAudioStreamQueued(gx_aurora::g_audio_stream);
    }

    // Interval counters (see IntervalStats): the gap since the previous push
    // against what the queue held after it says how long the device played
    // silence in between; no per-sample work, no logging.
    const Uint64 now_ns = SDL_GetTicksNS();
    {
        auto& st = g_stats;
        st.pushes++;
        st.frames_in += frames;
        const double queued_ms = queued * 1000.0 / bytes_per_second;
        if (queued_ms < st.queue_min_ms) st.queue_min_ms = static_cast<int>(queued_ms);
        if (queued_ms > st.queue_max_ms) st.queue_max_ms = static_cast<int>(queued_ms);
        if (gx_aurora::g_audio_playing && g_prev_push_ns != 0) {
            const double gap_ms = static_cast<double>(now_ns - g_prev_push_ns) / 1.0e6;
            if (gap_ms > st.max_gap_ms) st.max_gap_ms = gap_ms;
            const double silent_ms = gap_ms - g_prev_queue_ms;
            if (silent_ms > 0.5) {
                st.underruns++;
                st.underrun_ms += silent_ms;
            }
        }
        trace_push(now_ns, frames, queued);
    }
    const bool low_queue =
        queued >= 0 && queued < bytes_per_second / 100;
    // A push that finds less than 10 ms queued while playing means the device
    // ran dry or nearly so: the output played silence in between (stutter).
    if (low_queue && gx_aurora::g_audio_playing) {
        gx_aurora::g_audio_starved_count++;
        g_stats.starved++;
    }
    if (gx_aurora::g_audio_queue_log &&
        (gx_aurora::g_audio_push_count <= 16 || (gx_aurora::g_audio_push_count % 4000) == 0 ||
         (low_queue && gx_aurora::g_audio_push_count - gx_aurora::g_audio_low_log_push >= 4000))) {
        if (low_queue)
            gx_aurora::g_audio_low_log_push = gx_aurora::g_audio_push_count;
        std::fprintf(stderr,
                     "[audio-queue] push=%llu queued_before=%d queued_after=%d "
                     "playing=%u nonzero=%u peak=%d hash=0x%08X throttles=%llu\n",
                     gx_aurora::g_audio_push_count, queued, queued + bytes,
                     gx_aurora::g_audio_playing ? 1u : 0u, nonzero_samples, peak_sample,
                     sample_hash, gx_aurora::g_audio_throttle_count);
    }
    const s16* out_samples = samples;
    int out_bytes = bytes;
    if (stretch_enabled() && queued >= 0) {
        double factor;
        if (legacy_policy()) {
            double desired = 1.0;
            if (gx_aurora::g_audio_playing) {
                const double queued_ms = queued * 1000.0 / bytes_per_second;
                if (queued_ms < kStretchTargetMs)
                    desired = 1.0 + (kStretchMax - 1.0) * (kStretchTargetMs - queued_ms) / kStretchTargetMs;
            }
            g_stretch += 0.1 * (desired - g_stretch);
            factor = g_stretch < 1.01 && !g_stretcher.active() ? 1.0
                     : (g_stretch < 1.002 ? 1.0 : g_stretch);
        } else {
            static const double base_ms = env_double("DOL_AUDIO_TARGET_MS", 80.0);
            static const double tau_ms = env_double("DOL_AUDIO_TAU_MS", 400.0);
            const double level_ms = (queued + static_cast<double>(g_stretcher.held_frames()) * 4.0) * 1000.0 /
                                    bytes_per_second;
            g_stretch = factor = g_pacer.update(now_ns, frames, gx_aurora::g_audio_sample_rate, level_ms,
                                                gx_aurora::g_audio_playing, base_ms, tau_ms);
        }
        if (factor > 1.0 || g_stretcher.active()) {
            g_stretch_out.clear();
            g_stretcher.process(samples, frames, factor, g_stretch_out);
            out_samples = g_stretch_out.data();
            out_bytes = static_cast<int>(g_stretch_out.size() * sizeof(s16));
            if (factor > 1.0) {
                gx_aurora::g_audio_stretched_count++;
                g_stats.stretched++;
            }
        }
    }
    {
        const float volume = g_master_volume.load(std::memory_order_relaxed);
        if (volume != g_master_applied) {
            g_master_applied = volume;
            SDL_SetAudioStreamGain(gx_aurora::g_audio_stream, volume);
            std::fprintf(stderr, "[audio] master volume %.2f (stream gain now %.2f)\n", volume,
                         SDL_GetAudioStreamGain(gx_aurora::g_audio_stream));
        }
    }
    if (out_bytes > 0 && !SDL_PutAudioStreamData(gx_aurora::g_audio_stream, out_samples, out_bytes)) {
        gx_aurora::g_audio_dropped_count++;
        gx_aurora::g_audio_dropped_frames += static_cast<u32>(out_bytes / (2 * sizeof(s16)));
        std::fprintf(stderr, "[audio] failed to queue samples: %s\n", SDL_GetError());
        return;
    }
    g_stats.stretch_sum += g_stretch;
    g_prev_push_ns = now_ns;
    g_prev_queue_ms = (queued + out_bytes) * 1000.0 / bytes_per_second;
    // Exactly the post-discard, post-stretch bytes successfully submitted to SDL.
    capture_sink(out_samples, out_bytes);
    if (!gx_aurora::g_audio_playing && queued + out_bytes >= prebuffer_bytes) {
        if (SDL_ResumeAudioStreamDevice(gx_aurora::g_audio_stream))
        {
            gx_aurora::g_audio_playing = true;
            if (gx_aurora::g_audio_queue_log)
                std::fprintf(stderr,
                             "[audio-queue] playback-start push=%llu "
                             "queued_before=%d queued_after=%d prebuffer=%d "
                             "nonzero=%u peak=%d hash=0x%08X\n",
                             gx_aurora::g_audio_push_count, queued, queued + out_bytes,
                             prebuffer_bytes, nonzero_samples, peak_sample, sample_hash);
        }
        else
            std::fprintf(stderr, "[audio] failed to start playback: %s\n", SDL_GetError());
    }
}

} // extern "C"
