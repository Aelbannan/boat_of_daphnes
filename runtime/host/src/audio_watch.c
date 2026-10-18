#include "audio_watch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GZLE01 revision 0 addresses (tww decompilation, config/GZLE01/symbols.txt).
// g_dComIfG_gameInfo is 0x803C4C08 and its play state starts at +0x12A0.
enum {
    kDemoManager = 0x803CA6D0u,  // play.mDemo (dDemo_manager_c*)
    kStageName = 0x803C9D3Cu,    // play.mCurStage.mName (8 characters)
    kEventId = 0x803C9EB8u,      // play.mEvtCtrl.mEventId (s16)
    kLastDac = 0x803F7404u,      // JASystem::Kernel::lastRspMadep: the newest mixed output
    kDacSamples = 1120u,         // JASystem::Kernel::gDacSize: 560 stereo frames
    kManagerFile = 0xD0u,        // dDemo_manager_c::mCurFile: non-null while a demo runs
    kManagerFrame = 0xD4u,       // dDemo_manager_c::mFrame
    kAdaptorSound = 0xECu,       // TAdaptor_sound::mpSound
    kAdaptorSoundId = 0xF0u,     // TAdaptor_sound::mSoundID
};

static const unsigned kRetracesPerSecond = 60u;
// Longer than the quiet passages in the game's own music (the intro story has
// one of 4.6 s), shorter than a scene that has lost its sound.
static const unsigned kSilentRetraces = 360u;  // 6 seconds
static const int kSilentPeak = 32;             // of 32767, about -60 dB
static const unsigned kMissingPerDemo = 4u;
static const unsigned kLineBudget = 600u;

typedef struct {
    u32 adaptor, id;
    int frame;
} Cue;

static bool g_trace_loaded, g_trace;
static unsigned g_lines;
static bool g_active;
static char g_stage[9];
static int g_event, g_frame;
static unsigned long long g_started;
static unsigned g_cues, g_sounds, g_missing, g_retriggers, g_silent, g_quiet_run, g_retraces;
static bool g_lost;
// Streamed music (the intro story, some cutscenes) outside a demo: its silence.
static unsigned g_stream_quiet;
static bool g_stream_lost;
// The last sound id each adaptor got a sound for: TAdaptor_sound's fade-in output runs every frame of a cue's
// ramp and asks for the sound again each time, so a sound that has played to its natural end is asked for once
// more and a refusal then is not a cue that never sounded (getperl_deku's FLY_ALL: playing from demo frame
// 3101, two refused calls at 3159).
typedef struct {
    u32 adaptor, id;
} Sounded;
static Sounded g_sounded[32];
static unsigned g_sounded_count;
static Cue g_pending[16];
static unsigned g_pending_count;

static bool in_ram(u32 address, u32 size) {
    return address >= 0x80000000u && address + size <= 0x81800000u && address + size > address;
}

static bool budget(void) {
    // BLUEWAKE_AUDIO_WATCH_TRACE=1 lists every cue of a long demo: a bigger budget for it
    if (g_lines >= (g_trace ? 60000u : kLineBudget))
        return false;
    g_lines++;
    return true;
}

void bluewake_sound_trace_init(void);
static unsigned long long g_trace_retrace;

void bluewake_audio_watch_cue(CPUState* cpu) {
    if (cpu == NULL || !g_active || g_pending_count >= sizeof g_pending / sizeof g_pending[0])
        return;
    const u32 adaptor = cpu->gpr[4];
    if (!in_ram(adaptor, kAdaptorSoundId + 4u))
        return;
    Cue* cue = &g_pending[g_pending_count++];
    cue->adaptor = adaptor;
    cue->id = mem_read32(cpu, adaptor + kAdaptorSoundId);
    cue->frame = g_frame;
}

static void read_scene(CPUState* cpu) {
    for (unsigned i = 0; i < 8u; i++) {
        const char c = (char)mem_read8(cpu, kStageName + i);
        g_stage[i] = c >= 0x20 && c <= 0x7E ? c : '\0';
        if (g_stage[i] == '\0')
            break;
    }
    g_stage[8] = '\0';
    g_event = (s16)mem_read16(cpu, kEventId);
}

// The loudest sample in the game's newest mixed output.
static int output_peak(CPUState* cpu) {
    const u32 buffer = mem_read32(cpu, kLastDac);
    if (!in_ram(buffer, kDacSamples * 2u))
        return -1;
    int peak = 0;
    for (u32 i = 0; i < kDacSamples; i++) {
        int sample = (s16)mem_read16(cpu, buffer + i * 2u);
        if (sample < 0)
            sample = -sample;
        if (sample > peak)
            peak = sample;
    }
    return peak;
}

// Why a sound start returned no handle (JAInter::SeMgr::storeSeBuffer, 0x80294A10): a sound id's category is
// (id >> 12) & 0xFF; the scene's category table (SeMgr::categoryInfoTable) gives how many sounds the category
// may hold, and when it holds that many the new sound starts only if its priority beats the lowest one's.
// Lists what holds the category's slots. GZLE01 revision 0 SDA slots: seRegist r13-0x6AEC (LinkSound[], 0xC
// each, head at +4), categoryInfoTable r13-0x6AFC, seScene r13-0x6AE0; a JAISound has its id at +0xC, its next
// at +0x34 and its SE parameter's priority at +0x40 -> +4.
static void explain_missing(CPUState* cpu, u32 id) {
    if ((id & 0xC0000000u) != 0u || !budget())
        return;
    const u32 sda = cpu->gpr[13];
    const u32 cat = (id >> 12) & 0xFFu;
    const u32 regist = mem_read32(cpu, sda - 0x6AECu);
    const u32 table = mem_read32(cpu, sda - 0x6AFCu);
    const u32 scene = mem_read8(cpu, sda - 0x6AE0u);
    if (!in_ram(regist, 0x100u) || !in_ram(table, 0x40u))
        return;
    const u32 info = mem_read32(cpu, table + scene * 4u);
    const u32 limit = in_ram(info, 0x100u) ? mem_read8(cpu, info + cat * 2u + 1u) : 0xFFFFu;
    char held[256];
    size_t used = 0;
    unsigned count = 0;
    held[0] = '\0';
    for (u32 sound = mem_read32(cpu, regist + cat * 0xCu + 4u); in_ram(sound, 0x48u) && count < 40u;
         sound = mem_read32(cpu, sound + 0x34u)) {
        const u32 param = mem_read32(cpu, sound + 0x40u);
        const unsigned priority = in_ram(param, 8u) ? mem_read8(cpu, param + 4u) : 0xFFu;
        if (used + 24u < sizeof held)
            used += (size_t)snprintf(held + used, sizeof held - used, " %05X/p%u", mem_read32(cpu, sound + 0xCu) & 0xFFFFFu,
                                     priority);
        count++;
    }
    // JAIBasic::startSoundBasic (0x80290628) refuses a new SE, and clears the handle, while seEntryCancel[category] is set
    // (SeMgr::seEntryCancel, r13-0x6AD4).
    const u32 cancel_table = mem_read32(cpu, sda - 0x6AD4u);
    const unsigned cancel = in_ram(cancel_table, 0x40u) ? mem_read8(cpu, cancel_table + cat) : 0xFFu;
    fprintf(stderr, "[demo-sound-why] id=0x%08X category=%u scene=%u category_limit=%u entry_cancel=%u registered=%u:%s\n", id, cat, scene,
            limit, cancel, count, held);
}

static void settle_cues(CPUState* cpu) {
    for (unsigned i = 0; i < g_pending_count; i++) {
        const Cue* cue = &g_pending[i];
        const u32 sound = mem_read32(cpu, cue->adaptor + kAdaptorSound);
        const bool got = in_ram(sound, 8u);
        g_cues++;
        bool retrigger = false;
        if (got) {
            g_sounds++;
            unsigned k = 0;
            while (k < g_sounded_count && g_sounded[k].adaptor != cue->adaptor)
                k++;
            if (k == g_sounded_count && g_sounded_count < sizeof g_sounded / sizeof g_sounded[0])
                g_sounded_count++;
            if (k < g_sounded_count) {
                g_sounded[k].adaptor = cue->adaptor;
                g_sounded[k].id = cue->id;
            }
        } else {
            for (unsigned k = 0; k < g_sounded_count; k++)
                if (g_sounded[k].adaptor == cue->adaptor && g_sounded[k].id == cue->id)
                    retrigger = true;
            if (retrigger)
                g_retriggers++;
            else
                g_missing++;
        }
        if (g_trace && budget())
            fprintf(stderr, "[demo-sound] stage=%s event=%d frame=%d id=0x%08X %s state=%u\n", g_stage, g_event,
                    cue->frame, cue->id, got ? "playing" : retrigger ? "ended (restart refused)" : "no sound", got ? mem_read8(cpu, sound + 5u) : 0u);
        else if (!got && !retrigger && g_missing <= kMissingPerDemo && budget())
            fprintf(stderr, "[demo-sound] stage=%s event=%d frame=%d id=0x%08X no sound\n", g_stage, g_event,
                    cue->frame, cue->id);
        if (!got && !retrigger && g_missing <= kMissingPerDemo)
            explain_missing(cpu, cue->id);
    }
    g_pending_count = 0;
}

static void end_demo(void) {
    if (g_quiet_run >= kSilentRetraces && budget())
        fprintf(stderr, "[audio-lost] stage=%s event=%d silent until the cutscene ended (%.1f s)\n", g_stage,
                g_event, (double)g_quiet_run / kRetracesPerSecond);
    if (budget())
        fprintf(stderr,
                "[demo] end stage=%s event=%d frames=%d cues=%u sounds=%u missing=%u silent=%.1fs of %.1fs retriggers=%u\n",
                g_stage, g_event, g_frame, g_cues, g_sounds, g_missing, (double)g_silent / kRetracesPerSecond,
                (double)g_retraces / kRetracesPerSecond, g_retriggers);
    g_active = false;
    g_pending_count = 0;
}

// True while JAIZelBasic's streamed track is playing (state 4), as the
// [music-stream] line in main.c reads it.
static bool stream_playing(CPUState* cpu) {
    const u32 slot = cpu->gpr[13] - 27088u;
    if (!in_ram(slot, 4u))
        return false;
    const u32 object = mem_read32(cpu, slot);
    if (!in_ram(object, 0x80u))
        return false;
    const u32 sound = mem_read32(cpu, object + 0x70u);
    return in_ram(sound, 8u) && mem_read8(cpu, sound + 5u) == 4u;
}

static void watch_stream(CPUState* cpu) {
    if (!stream_playing(cpu)) {
        g_stream_quiet = 0u;
        g_stream_lost = false;
        return;
    }
    const int peak = output_peak(cpu);
    if (peak < 0)
        return;
    if (peak >= kSilentPeak) {
        if (g_stream_lost && budget())
            fprintf(stderr, "[audio-lost] streamed music: sound back after %.1f s\n",
                    (double)g_stream_quiet / kRetracesPerSecond);
        g_stream_quiet = 0u;
        g_stream_lost = false;
    } else if (++g_stream_quiet == kSilentRetraces && !g_stream_lost) {
        g_stream_lost = true;
        read_scene(cpu);
        if (budget())
            fprintf(stderr, "[audio-lost] stage=%s streamed music playing but the output silent for 6 s\n",
                    g_stage);
    }
}

void bluewake_audio_watch_retrace(CPUState* cpu, unsigned long long retrace) {
    g_trace_retrace = retrace;
    if (cpu == NULL || cpu->ram == NULL)
        return;
    if (!g_trace_loaded)
        bluewake_sound_trace_init();
    if (!g_trace_loaded) {
        const char* trace = getenv("BLUEWAKE_AUDIO_WATCH_TRACE");
        g_trace = trace != NULL && trace[0] == '1';
        g_trace_loaded = true;
    }
    const u32 manager = mem_read32(cpu, kDemoManager);
    {
        static int probe = -1;
        static u32 last[12];
        if (probe < 0) probe = getenv("BLUEWAKE_DEMO_PROBE") != NULL;
        if (probe && in_ram(manager, 0x100u)) {
            u32 now[12];
            bool changed = false;
            for (int i = 0; i < 12; ++i) {
                now[i] = mem_read32(cpu, manager + 0xC0u + (u32)i * 4u);
                changed |= now[i] != last[i];
            }
            if (changed) {
                fprintf(stderr, "[demo-probe] retrace=%llu mgr=%#x +C0:", retrace, manager);
                for (int i = 0; i < 12; ++i) { fprintf(stderr, " %x", now[i]); last[i] = now[i]; }
                fprintf(stderr, "\n");
            }
        }
    }
    const bool running = in_ram(manager, kManagerFrame + 4u) && mem_read32(cpu, manager + kManagerFile) != 0u;
    if (!running) {
        if (g_active)
            end_demo();
        watch_stream(cpu);
        return;
    }
    if (!g_active) {
        g_active = true;
        read_scene(cpu);
        g_started = retrace;
        g_cues = g_sounds = g_missing = g_retriggers = g_silent = g_quiet_run = g_retraces = 0u;
        g_sounded_count = 0u;
        g_lost = false;
        g_pending_count = 0;
        if (budget())
            fprintf(stderr, "[demo] start stage=%s event=%d retrace=%llu\n", g_stage, g_event, retrace);
    }
    g_frame = (int)mem_read32(cpu, manager + kManagerFrame);
    settle_cues(cpu);
    g_retraces++;
    const int peak = output_peak(cpu);
    if (peak < 0)
        return;
    if (peak < kSilentPeak) {
        g_silent++;
        if (++g_quiet_run == kSilentRetraces && !g_lost) {
            g_lost = true;
            if (budget())
                fprintf(stderr,
                        "[audio-lost] stage=%s event=%d frame=%d output silent for 6 s in a cutscene "
                        "(cues=%u sounds=%u missing=%u, %.1f s in)\n",
                        g_stage, g_event, g_frame, g_cues, g_sounds, g_missing,
                        (double)(retrace - g_started) / kRetracesPerSecond);
        }
    } else {
        if (g_lost && budget())
            fprintf(stderr, "[audio-lost] stage=%s event=%d frame=%d sound back after %.1f s\n", g_stage, g_event,
                    g_frame, (double)g_quiet_run / kRetracesPerSecond);
        g_lost = false;
        g_quiet_run = 0u;
    }
}

bool bluewake_sound_trace_on;

void bluewake_sound_trace(CPUState* cpu, u32 address) {
    if (address != BLUEWAKE_JAI_SE_START && address != BLUEWAKE_JAI_BGM_START && address != BLUEWAKE_JAI_SUBBGM_START)
        return;
    const char* kind = address == BLUEWAKE_JAI_SE_START ? "se" : address == BLUEWAKE_JAI_BGM_START ? "bgm" : "subbgm";
    fprintf(stderr, "[sound] retrace=%llu %s 0x%08X lr=0x%08X%c", g_trace_retrace, kind, cpu->gpr[4], cpu->lr, 10);
}

void bluewake_sound_trace_init(void) {
    const char* on = getenv("BLUEWAKE_SOUND_TRACE");
    bluewake_sound_trace_on = on != NULL && on[0] == '1';
}
