#include "audio_volume.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum {
    kSeCategoryVolume = 0x803F7608u, // JAInter::SeMgr::seCategoryVolume (f32*)
    kSeqTrackInfo = 0x803F7620u,     // JAInter::SequenceMgr::seqTrackInfo (SeqUpdateData*)
    kSeCategories = 8,
    kSeqPlayTracks = 3,              // JAIZelParam::PLAY_TRACK_MAX
    kSeqUpdateSize = 0x50,
    kSeqActiveFlag = 0x08,           // SeqUpdateData::mActiveTrackFlag
    kSeqSound = 0x48,                // SeqUpdateData::mSequence (JAISound*)
    kSoundId = 0x0C,                 // JAISound::mSoundID
    kSoundSeqParameter = 0x3C,       // JAISound::field_0x3c (SeqParameter*, for a sequence)
    kSeqVolumes = 0x110,             // SeqParameter::mVolumes
    kScaleLine = 12,                 // the unused line of 0..14
    kMoveSize = 0x10,
    kMoveTarget = 0x0,               // MoveParaSet: target, current, amount, counter
    kMoveCurrent = 0x4,
    kMoveCounter = 0xC,
    kVolumeChanged = 0x40000,
    kSeSequenceId = 0x80000800u,
};

static _Atomic int g_music = 100, g_sfx = 100, g_fanfare = 100;

void bluewake_audio_volume_set(int music, int sfx, int fanfare) {
    atomic_store(&g_music, music < 0 ? 0 : music > 100 ? 100 : music);
    atomic_store(&g_sfx, sfx < 0 ? 0 : sfx > 100 ? 100 : sfx);
    atomic_store(&g_fanfare, fanfare < 0 ? 0 : fanfare > 100 ? 100 : fanfare);
}

int bluewake_audio_volume_sfx_percent(void) { return atomic_load(&g_sfx); }

static float read_float(CPUState* cpu, u32 address) {
    const u32 bits = mem_read32(cpu, address);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static void write_float(CPUState* cpu, u32 address, float f) {
    u32 bits;
    memcpy(&bits, &f, sizeof bits);
    mem_write32(cpu, address, bits);
}

static bool guest_pointer(u32 p) { return p >= 0x80000000u && p < 0x81800000u; }

// BGM ids (JAZelAudio_BGM.h) that are short jingles: item get 2, chest 9, game over 0xA, race goal and fail 0xC 0xD, heart
// 0x24, small item 0x25, song 0x27, pearl 0x4F, birdman goal and fail 0x51 0x52, treasure box 0x5D.
static bool is_fanfare(u32 id) {
    if ((id & 0xFFFFFF00u) != 0x80000000u)
        return false;
    switch (id & 0xFFu) {
    case 0x02: case 0x09: case 0x0A: case 0x0C: case 0x0D: case 0x24: case 0x25: case 0x27: case 0x4F: case 0x51:
    case 0x52: case 0x5D:
        return true;
    default:
        return false;
    }
}

static float g_se_base[kSeCategories], g_se_written[kSeCategories];
static bool g_se_have;
static float g_applied_sfx = 1.0f;
static unsigned g_logged;

static void apply_sfx(CPUState* cpu, float scale) {
    const u32 array = mem_read32(cpu, kSeCategoryVolume);
    if (!guest_pointer(array)) {
        g_se_have = false;
        return;
    }
    for (int i = 0; i < kSeCategories; ++i) {
        const float now = read_float(cpu, array + 4u * (u32)i);
        // A value that is not the one written here is the game's (a scene change, dialogue, pausing): the new base.
        if (!g_se_have || now != g_se_written[i])
            g_se_base[i] = now;
        const float wanted = g_se_base[i] * scale;
        if (now != wanted)
            write_float(cpu, array + 4u * (u32)i, wanted);
        g_se_written[i] = wanted;
    }
    g_se_have = true;
    if (scale != g_applied_sfx && g_logged++ < 50u)
        fprintf(stderr, "[audio-volume] sound effects x%.2f (category 6 base %.3f)\n", scale, g_se_base[6]);
    g_applied_sfx = scale;
}

static void apply_sequences(CPUState* cpu, float music, float fanfare) {
    const u32 info = mem_read32(cpu, kSeqTrackInfo);
    if (!guest_pointer(info))
        return;
    for (u32 i = 0; i < kSeqPlayTracks; ++i) {
        const u32 data = info + i * kSeqUpdateSize;
        const u32 sound = mem_read32(cpu, data + kSeqSound);
        if (!guest_pointer(sound))
            continue;
        const u32 id = mem_read32(cpu, sound + kSoundId);
        if ((id & 0xC0000000u) != 0x80000000u || id == kSeSequenceId)
            continue;
        const u32 param = mem_read32(cpu, sound + kSoundSeqParameter);
        if (!guest_pointer(param))
            continue;
        const u32 line = param + kSeqVolumes + kScaleLine * kMoveSize;
        const float scale = is_fanfare(id) ? fanfare : music;
        if (read_float(cpu, line + kMoveCurrent) == scale && mem_read32(cpu, line + kMoveCounter) == 0u)
            continue;
        write_float(cpu, line + kMoveTarget, scale);
        write_float(cpu, line + kMoveCurrent, scale);
        mem_write32(cpu, line + kMoveCounter, 0u);
        mem_write32(cpu, data + kSeqActiveFlag, mem_read32(cpu, data + kSeqActiveFlag) | kVolumeChanged);
        if (g_logged++ < 50u)
            fprintf(stderr, "[audio-volume] sequence 0x%08X %s x%.2f\n", id, is_fanfare(id) ? "fanfare" : "music", scale);
    }
}

void bluewake_audio_volume_retrace(CPUState* cpu) {
    const float music = (float)atomic_load(&g_music) / 100.0f;
    const float sfx = (float)atomic_load(&g_sfx) / 100.0f;
    const float fanfare = (float)atomic_load(&g_fanfare) / 100.0f;
    // Untouched while all three are at 100 (and nothing of ours is left in the game's memory).
    static bool touched;
    if (music == 1.0f && sfx == 1.0f && fanfare == 1.0f && !touched)
        return;
    {
        static unsigned reports;
        if ((reports++ % 600u) == 0u && reports < 4000u) {
            const u32 info = mem_read32(cpu, kSeqTrackInfo);
            fprintf(stderr, "[audio-volume] music %.2f sfx %.2f fanfare %.2f; seqTrackInfo %08X", music, sfx, fanfare, info);
            for (u32 i = 0; guest_pointer(info) && i < kSeqPlayTracks; ++i) {
                const u32 sound = mem_read32(cpu, info + i * kSeqUpdateSize + kSeqSound);
                fprintf(stderr, " [%u: sound %08X id %08X]", i, sound, guest_pointer(sound) ? mem_read32(cpu, sound + kSoundId) : 0u);
            }
            fprintf(stderr, "\n");
        }
    }
    apply_sfx(cpu, sfx);
    apply_sequences(cpu, music, fanfare);
    touched = !(music == 1.0f && sfx == 1.0f && fanfare == 1.0f);
}
