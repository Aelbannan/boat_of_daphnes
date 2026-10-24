#include "voice_pitch.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "guest_coro.h"
#define kHookSite 0x802A88FCu

static _Atomic float g_local = 1.0f;
static _Atomic float g_scope = 0.0f;
static unsigned g_logged;
unsigned long long bluewake_host_retrace_count(void);

static float clamp_ratio(float r) { return r < 0.5f ? 0.5f : r > 2.0f ? 2.0f : r; }

void bluewake_voice_pitch_set_local(float ratio) { atomic_store(&g_local, clamp_ratio(ratio)); }
void bluewake_voice_pitch_set_scope(float ratio) { atomic_store(&g_scope, ratio <= 0.0f ? 0.0f : clamp_ratio(ratio)); }
float bluewake_voice_pitch_local(void) { return atomic_load(&g_local); }

// seStart sends the ids 0x1800..0x1859 (Link's voice) down a path of its own (0x802A6AFC): JAIZelBasic's actor sound
// (0x802904B4) into this+0x2064, then the reverb, and the pitch f1 is never read. So the pitch is set on that sound after
// it exists: this hook (a coroutine, see guest_coro.h) makes the seStart call itself, calls JAISound::setPitch
// (0x80298864) on the new sound, and turns the game's own call into an early exit (id 0xFFFFFFFF).
enum { kSeStart = 0x802A6720u, kSetPitch = 0x80298864u, kSoundHandle = 0x2064u };

typedef struct {
    u32 self, id, pos, reverb;
    float pitch, volume, fxmix, pan;
    bool finished;
} VoiceCall;
static VoiceCall g_call;
static GuestCoro* g_co;

static void voice_body(GuestCoro* co, void* arg) {
    (void)arg;
    CPUState* cpu = bluewake_coro_cpu(co);
    VoiceCall* c = &g_call;
    cpu->fpr[1] = c->volume; cpu->ps1[1] = c->volume;  // seStart's own pitch (1.0): the voice path ignores it
    cpu->fpr[2] = c->volume; cpu->ps1[2] = c->volume;
    cpu->fpr[3] = c->fxmix; cpu->ps1[3] = c->fxmix;
    cpu->fpr[4] = c->pan; cpu->ps1[4] = c->pan;
    bluewake_guest_call(co, kSeStart, c->self, c->id, c->pos, 0u, c->reverb, 0u);
    const u32 sound = mem_read32(cpu, c->self + kSoundHandle);
    if (sound >= 0x80000000u && sound < 0x81800000u) {
        cpu->fpr[1] = c->pitch; cpu->ps1[1] = c->pitch;
        bluewake_guest_call(co, kSetPitch, sound, 0u, 0u, 0u, 0u, 0u);
    }
    c->finished = true;
}

u32 bluewake_voice_pitch_apply(CPUState* cpu) {
    if (g_co != NULL && bluewake_coro_active(g_co)) {
        const u32 next = bluewake_coro_step(g_co, cpu, kHookSite, NULL);
        if (next == 0u && g_call.finished) {
            g_call.finished = false;
            cpu->gpr[4] = 0xFFFFFFFFu;  // the game's own seStart call returns at once
        }
        return next;
    }
    const float scope = atomic_load(&g_scope);
    const float ratio = scope > 0.0f ? scope : atomic_load(&g_local);
    const u32 id = cpu->gpr[4];
    if (g_logged++ < 400u)
        fprintf(stderr, "[voice] retrace=%llu Link's voice started at pitch x%.3f (%s) se=0x%X this+0x201=%u\n", bluewake_host_retrace_count(), ratio,
                scope > 0.0f ? "a player's puppet" : "local", id, cpu->gpr[3] >= 0x80000000u ? mem_read8(cpu, cpu->gpr[3] + 0x201u) : 99u);
    if (ratio == 1.0f)
        return 0u;
    if (cpu->gpr[3] >= 0x80000000u && (id & 0xFFFFFF00u) == 0x1800u && (id & 0xFFu) < 0x5Au) {
        if (g_co == NULL)
            g_co = bluewake_coro_create(voice_body);
        float fx, pan;
        const u32 fx_bits = mem_read32(cpu, cpu->gpr[2] - 0x3880u);
        memcpy(&fx, &fx_bits, 4);
        pan = fx;
        g_call = (VoiceCall){cpu->gpr[3], id, cpu->gpr[5], cpu->gpr[7], (float)cpu->fpr[1] * ratio, (float)cpu->fpr[2], fx, pan, false};
        const u32 next = bluewake_coro_step(g_co, cpu, kHookSite, NULL);
        if (next == 0u && g_call.finished) {
            g_call.finished = false;
            cpu->gpr[4] = 0xFFFFFFFFu;
        }
        return next;
    }
    const float v = (float)cpu->fpr[1] * ratio;  // other ids: seStart reads f1 as the pitch
    cpu->fpr[1] = v; cpu->ps1[1] = v;
    return 0u;
}
