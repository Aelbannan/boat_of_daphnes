// A player's voice bank (JaiInit.aaf with voice_0.aw), decoded for the host to play: the wave table of the voice_0.aw group
// in the init file's wave system, the GameCube AFC (4-bit ADPCM) decoder, and the PCM of each of Link's voice sounds at the
// output rate. Everything in these files comes from other players (through the Anchor server), so every read is bounds
// checked and every size capped. Pure data code (no game or UI dependencies): tests/content_validate_test.cpp builds it
// alone. docs/fork/anchor-content-security.md lists the limits.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace voice_bank {

constexpr int kOutputRate = 32000;          // the game's audio output rate
constexpr size_t kMaxWaves = 256;           // waves in the group
constexpr size_t kMaxWaveBytes = 1u << 20;  // one wave's ADPCM
constexpr size_t kMaxWaveSamples = 1u << 19;// one wave decoded (16 s at 32 kHz)
constexpr size_t kMaxBankBytes = 6u << 20;  // all of a bank's PCM
constexpr int kVoiceIds = 90;               // Link's voice sounds 0x1800 + 0 .. 0x1800 + 89

// One wave of the group (a byte range of voice_0.aw).
struct Wave {
    uint32_t offset = 0, size = 0; // in voice_0.aw
    float rate = 0;                // Hz
    uint8_t format = 0;            // 0: AFC 4-bit ADPCM (the only one played)
};

// The waves of the "voice_0.aw" group of the wave systems of a JaiInit.aaf, every offset and size inside aw_size.
bool ParseWaves(const std::vector<uint8_t>& aaf, size_t aw_size, std::vector<Wave>& out, std::string* error);

// AFC: 9-byte frames of 16 samples. size must be a multiple of 9 and the output at most max_samples.
bool DecodeAfc(const uint8_t* data, size_t size, std::vector<int16_t>& pcm, size_t max_samples);

// Linear resampling of mono PCM from rate Hz to kOutputRate.
std::vector<int16_t> Resample(const std::vector<int16_t>& in, float rate);

// The wave Link's voice sound `id` plays in the game's own bank (the waves of a bank made for the game are in the same
// order), or -1.
int WaveForVoice(int id);
// The playback pitch of that voice relative to the wave's own rate (about 1.08 for most of Link's voices).
float PitchForVoice(int id);

struct Bank {
    std::vector<std::vector<int16_t>> voice; // [kVoiceIds]: mono PCM at kOutputRate (already at the voice's pitch), empty when the wave could not be decoded
    size_t Bytes() const;
};

// Decodes the waves of the voices of a bank. False when the files do not parse or no voice could be decoded.
bool Build(const std::vector<uint8_t>& aaf, const std::vector<uint8_t>& aw, Bank& out, std::string* error);

} // namespace voice_bank
