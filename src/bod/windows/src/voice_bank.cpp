// See voice_bank.h.
#include "voice_bank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace voice_bank {
namespace {

// Big-endian reads that fail instead of running past the end.
struct Reader {
    const std::vector<uint8_t>& b;
    bool u32(size_t at, uint32_t& v) const {
        if (at > b.size() || b.size() - at < 4)
            return false;
        v = static_cast<uint32_t>(b[at]) << 24 | static_cast<uint32_t>(b[at + 1]) << 16 | static_cast<uint32_t>(b[at + 2]) << 8 | b[at + 3];
        return true;
    }
    bool f32(size_t at, float& v) const {
        uint32_t u;
        if (!u32(at, u))
            return false;
        std::memcpy(&v, &u, 4);
        return true;
    }
};

bool fail(std::string* error, const char* message) {
    if (error != nullptr)
        *error = message;
    return false;
}

// The voice banks of AFC: the coefficient pairs of the 16 prediction filters.
const int kCoef[16][2] = {{0, 0},       {2048, 0},     {0, 2048},     {1024, 1024}, {4096, -2048}, {3584, -1536}, {3072, -1024}, {4608, -2560},
                          {4200, -2248}, {4800, -2300}, {5120, -3072}, {2048, -2048}, {1024, -1024}, {-1024, 1024}, {-1024, 0},    {-2048, 0}};

struct VoiceEntry {
    short wave;
    unsigned short ratio;
};
const VoiceEntry kVoice[kVoiceIds] = {
#include "voice_wave_map.inc"
};

} // namespace

bool ParseWaves(const std::vector<uint8_t>& aaf, size_t aw_size, std::vector<Wave>& out, std::string* error) {
    out.clear();
    const Reader r{aaf};
    if (aaf.size() < 16 || aaf.size() > (2u << 20) || aw_size == 0 || aw_size > (8u << 20))
        return fail(error, "the voice files have impossible sizes");
    // The record stream (ValidateAaf has the same walk): type 3 lists the wave systems as (offset, size, id).
    size_t at = 0;
    for (int record = 0; record < 64; ++record) {
        uint32_t type;
        if (!r.u32(at, type))
            return fail(error, "JaiInit.aaf ends early");
        at += 4;
        if (type == 0)
            return out.empty() ? fail(error, "no voice_0.aw group in JaiInit.aaf") : true;
        if (type == 2 || type == 3) {
            size_t entries = 0;
            for (;;) {
                uint32_t offset, size, id;
                if (!r.u32(at, offset))
                    return fail(error, "a JaiInit.aaf table runs off the end");
                if (offset == 0) {
                    at += 4;
                    break;
                }
                if (!r.u32(at + 4, size) || !r.u32(at + 8, id) || ++entries > 256)
                    return fail(error, "a JaiInit.aaf table is damaged");
                at += 12;
                if (type != 3 || out.size() != 0)
                    continue;
                // A wave system: 'WSYS', size, id, 0, WINF offset, WBCT offset; WINF: 'WINF', group count, group offsets;
                // a group: its .aw name (0x70 bytes), wave count, wave offsets (all relative to the system's start).
                if (size < 0x20 || offset > aaf.size() || size > aaf.size() - offset || std::memcmp(&aaf[offset], "WSYS", 4) != 0)
                    continue;
                uint32_t winf, groups;
                if (!r.u32(offset + 0x10, winf) || winf > size || size - winf < 8 || std::memcmp(&aaf[offset + winf], "WINF", 4) != 0 ||
                    !r.u32(offset + winf + 4, groups) || groups == 0 || groups > 64)
                    continue;
                for (uint32_t g = 0; g < groups; ++g) {
                    uint32_t group;
                    if (!r.u32(offset + winf + 8 + 4 * static_cast<size_t>(g), group) || group > size || size - group < 0x74)
                        break;
                    const char* name = reinterpret_cast<const char*>(&aaf[offset + group]);
                    if (strnlen(name, 0x70) >= 0x70 || std::strcmp(name, "voice_0.aw") != 0)
                        continue;
                    uint32_t count;
                    if (!r.u32(offset + group + 0x70, count) || count == 0 || count > kMaxWaves)
                        return fail(error, "the voice wave group is damaged");
                    for (uint32_t k = 0; k < count; ++k) {
                        uint32_t wave;
                        if (!r.u32(offset + group + 0x74 + 4 * static_cast<size_t>(k), wave) || wave > size || size - wave < 0x2c)
                            return fail(error, "a voice wave entry is outside its system");
                        Wave w;
                        uint32_t aw_offset, aw_len;
                        const size_t e = offset + wave;
                        if (!r.f32(e + 4, w.rate) || !r.u32(e + 8, aw_offset) || !r.u32(e + 12, aw_len))
                            return fail(error, "a voice wave entry is cut short");
                        w.format = aaf[e + 1];
                        if (!(w.rate >= 4000.0f && w.rate <= 48000.0f) || aw_len > kMaxWaveBytes || aw_offset > aw_size || aw_len > aw_size - aw_offset)
                            return fail(error, "a voice wave lies outside voice_0.aw or has an impossible rate");
                        w.offset = aw_offset;
                        w.size = aw_len;
                        out.push_back(w);
                    }
                    break;
                }
            }
            continue;
        }
        // The other records are skipped as the validator walks them.
        if (type == 1 || (type >= 5 && type <= 8) || type == 4) {
            at += 12;
        } else {
            uint32_t v;
            do {
                if (!r.u32(at, v))
                    return fail(error, "an unknown JaiInit.aaf record is not terminated");
                at += 4;
            } while (v != 0);
        }
    }
    return fail(error, "JaiInit.aaf has too many records");
}

bool DecodeAfc(const uint8_t* data, size_t size, std::vector<int16_t>& pcm, size_t max_samples) {
    pcm.clear();
    if (data == nullptr || size == 0 || size % 9 != 0 || size / 9 * 16 > max_samples)
        return false;
    pcm.reserve(size / 9 * 16);
    int h1 = 0, h2 = 0;
    for (size_t f = 0; f < size / 9; ++f) {
        const uint8_t* frame = data + f * 9;
        const int delta = 1 << (frame[0] >> 4);
        const int c1 = kCoef[frame[0] & 15][0], c2 = kCoef[frame[0] & 15][1];
        for (int i = 0; i < 16; ++i) {
            const uint8_t b = frame[1 + i / 2];
            int n = (i & 1) ? (b & 15) : (b >> 4);
            if (n >= 8)
                n -= 16;
            int s = ((delta * n) << 11) + h1 * c1 + h2 * c2;
            s >>= 11;
            s = std::max(-32768, std::min(32767, s));
            pcm.push_back(static_cast<int16_t>(s));
            h2 = h1;
            h1 = s;
        }
    }
    return true;
}

std::vector<int16_t> Resample(const std::vector<int16_t>& in, float rate) {
    if (in.empty() || !(rate > 0.0f))
        return {};
    const double step = static_cast<double>(rate) / kOutputRate;
    const size_t n = std::min<size_t>(kMaxWaveSamples, static_cast<size_t>(static_cast<double>(in.size()) / step));
    std::vector<int16_t> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double p = static_cast<double>(i) * step;
        const size_t k = static_cast<size_t>(p);
        const double f = p - static_cast<double>(k);
        const double a = in[std::min(k, in.size() - 1)], b = in[std::min(k + 1, in.size() - 1)];
        out[i] = static_cast<int16_t>(a + (b - a) * f);
    }
    return out;
}

int WaveForVoice(int id) { return id >= 0 && id < kVoiceIds ? kVoice[id].wave : -1; }

float PitchForVoice(int id) { return id >= 0 && id < kVoiceIds && kVoice[id].ratio != 0 ? static_cast<float>(kVoice[id].ratio) / 2048.0f : 1.0f; }

namespace {
std::atomic<size_t> g_decoded_total{0};
} // namespace

size_t DecodedTotal() { return g_decoded_total.load(); }

Bank::~Bank() { g_decoded_total -= decoded_bytes_.load(); }

size_t Bank::Bytes() const { return raw_bytes_ + decoded_bytes_.load(); }

int Bank::Decoded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int n = 0;
    for (int i = 0; i < kVoiceIds; ++i)
        n += state_[i] == 1 ? 1 : 0;
    return n;
}

const std::vector<int16_t>* Bank::Voice(int id) const {
    if (id < 0 || id >= kVoiceIds)
        return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t i = static_cast<size_t>(id);
    if (state_[i] == 1)
        return pcm_[i].get();
    if (state_[i] == 2)
        return nullptr;
    state_[i] = 2; // until it is decoded
    const int w = WaveForVoice(id);
    if (w < 0 || static_cast<size_t>(w) >= waves_.size() || static_cast<size_t>(w) >= wave_data_.size() || wave_data_[static_cast<size_t>(w)].empty())
        return nullptr;
    const Wave& wave = waves_[static_cast<size_t>(w)];
    std::vector<int16_t> pcm;
    if (!DecodeAfc(wave_data_[static_cast<size_t>(w)].data(), wave_data_[static_cast<size_t>(w)].size(), pcm, kMaxWaveSamples))
        return nullptr;
    auto out = std::make_unique<std::vector<int16_t>>(Resample(pcm, wave.rate * PitchForVoice(id))); // the game plays it that much faster
    const size_t bytes = out->size() * sizeof(int16_t);
    if (out->empty())
        return nullptr;
    if (decoded_bytes_.load() + bytes > kMaxBankBytes || g_decoded_total.load() + bytes > kMaxDecodedTotal) {
        std::fprintf(stderr, "[voice] voice %d not decoded: %zu KB decoded in this bank (cap %zu KB), %zu of %zu KB in all banks\n", id,
                     decoded_bytes_.load() >> 10, kMaxBankBytes >> 10, g_decoded_total.load() >> 10, kMaxDecodedTotal >> 10);
        return nullptr;
    }
    decoded_bytes_ += bytes;
    g_decoded_total += bytes;
    pcm_[i] = std::move(out);
    state_[i] = 1;
    return pcm_[i].get();
}

void Bank::DecodeAll() const {
    for (int id = 0; id < kVoiceIds; ++id)
        Voice(id);
}

bool Build(const std::vector<uint8_t>& aaf, const std::vector<uint8_t>& aw, Bank& out, std::string* error) {
    out.waves_.clear();
    out.wave_data_.clear();
    out.playable_ = 0;
    out.raw_bytes_ = 0;
    if (!ParseWaves(aaf, aw.size(), out.waves_, error))
        return false;
    out.wave_data_.resize(out.waves_.size());
    for (int id = 0; id < kVoiceIds; ++id) {
        const int w = WaveForVoice(id);
        if (w < 0 || static_cast<size_t>(w) >= out.waves_.size())
            continue;
        const Wave& wave = out.waves_[static_cast<size_t>(w)];
        if (wave.format != 0 || wave.size == 0 || wave.size % 9 != 0 || wave.size / 9 * 16 > kMaxWaveSamples)
            continue;
        auto& data = out.wave_data_[static_cast<size_t>(w)];
        if (data.empty()) {
            data.assign(aw.begin() + wave.offset, aw.begin() + wave.offset + wave.size);
            out.raw_bytes_ += data.size();
        }
        ++out.playable_;
    }
    return out.playable_ > 0 ? true : fail(error, "no voice of the bank could be decoded");
}

} // namespace voice_bank
