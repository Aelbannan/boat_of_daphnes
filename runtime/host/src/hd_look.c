#include "hd_look.h"

#include <aurora/aurora.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool bluewake_hd_distant_islands;
static int g_clouds = -1;  // cloud shadows: 1, 0, or -1 left to the renderer
static bool g_clouds_shown;
static int g_sun = -1;     // sun shadows, the same
static bool g_sun_shown;
static bool g_islands_were_on;
static bool g_trace;

static int setting(const char* key) {  // 1, 0, or -1 when unset
    const char* value = getenv(key);
    if (value == NULL || value[0] == '\0')
        return -1;
    return value[0] == '1';
}

void bluewake_hd_look_reload(void) {
    const int all = setting("BLUEWAKE_HD_LOOK");
    const int islands = setting("BLUEWAKE_HD_DISTANT_ISLANDS");
    bluewake_hd_distant_islands = islands >= 0 ? islands == 1 : all == 1;
    g_trace = setting("BLUEWAKE_HD_TRACE") == 1;
    // The renderer's parts (Aurora's post_fx). Left to DOL_AURORA_BLOOM and
    // DOL_AURORA_FAR_BLUR and DOL_AURORA_AO while neither the switch nor the part is set.
    const int bloom = setting("BLUEWAKE_HD_BLOOM") >= 0 ? setting("BLUEWAKE_HD_BLOOM") : all;
    const int far_blur = setting("BLUEWAKE_HD_FAR_BLUR") >= 0 ? setting("BLUEWAKE_HD_FAR_BLUR") : all;
    const int ao = setting("BLUEWAKE_HD_AO") >= 0 ? setting("BLUEWAKE_HD_AO") : all;
    g_clouds = setting("BLUEWAKE_HD_CLOUD_SHADOWS") >= 0 ? setting("BLUEWAKE_HD_CLOUD_SHADOWS") : all;
    g_sun = setting("BLUEWAKE_HD_SUN_SHADOWS") >= 0 ? setting("BLUEWAKE_HD_SUN_SHADOWS") : all;
    if (bloom >= 0)
        aurora_set_bloom(bloom == 1);
    if (far_blur >= 0)
        aurora_set_far_blur(far_blur == 1);
    if (ao >= 0)
        aurora_set_ambient_occlusion(ao == 1);
    fprintf(stderr, "[hd-look] distant islands %s, ambient occlusion %s, sun shadows %s, cloud shadows %s, far blur %s, bloom %s\n",
            bluewake_hd_distant_islands ? "on" : "off", ao < 0 ? "unset" : ao ? "on" : "off",
            g_sun == 1 ? "on" : "off", g_clouds == 1 ? "on" : "off",
            far_blur < 0 ? "unset" : far_blur ? "on" : "off", bloom < 0 ? "unset" : bloom ? "on" : "off");
}

// GZLE01 revision 0. Every actor's create tag is on g_fopAcTg_Queue
// (node_list_class: head first); a tag is node_class (next at +0x08) with its
// actor at +0x0C. An actor's process name is at +0x08 and its scale at +0x214.
enum {
    kActorQueue = 0x80372028u,
    kNodeNext = 0x08u,
    kTagActor = 0x0Cu,
    kProcName = 0x08u,
    kScaleX = 0x214u,
    kScaleZ = 0x21Cu,
    kActorHeap = 0xF0u,
    kLodBg = 0x01C3u,  // fpcNm_LODBG_e, d_a_lod_bg
    kMaxActors = 4096u,
};

// d_a_lod_bg's constructor makes scale.x the distance within which the island's
// far model is shown (its stage scale times 20,000) and scale.z, 20,000 more,
// the distance past which it is unloaded; it reads both every frame. HD shows
// the models 100,000 units further out. A changed actor keeps 20,001 between
// the two, so it is changed once and can be changed back, also in a save state.
static const float kExtra = 100000.0f;
static const float kGap = 20000.0f;
static const float kChangedGap = 20001.0f;

static bool guest_pointer(u32 address) { return address >= 0x80000000u && address < 0x81800000u; }

static float read_float(CPUState* cpu, u32 address) {
    const u32 bits = mem_read32(cpu, address);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static void write_float(CPUState* cpu, u32 address, float value) {
    u32 bits;
    memcpy(&bits, &value, sizeof bits);
    mem_write32(cpu, address, bits);
}

static void islands(CPUState* cpu, bool on, unsigned long long retrace) {
    unsigned count = 0, changed = 0, loaded = 0;
    u32 node = mem_read32(cpu, kActorQueue);
    for (u32 n = 0; n < kMaxActors && guest_pointer(node); ++n, node = mem_read32(cpu, node + kNodeNext)) {
        const u32 actor = mem_read32(cpu, node + kTagActor);
        if (!guest_pointer(actor) || mem_read16(cpu, actor + kProcName) != kLodBg)
            continue;
        count++;
        loaded += mem_read32(cpu, actor + kActorHeap) != 0u;
        const float x = read_float(cpu, actor + kScaleX);
        const float gap = read_float(cpu, actor + kScaleZ) - x;
        if (on && gap == kGap) {
            write_float(cpu, actor + kScaleX, x + kExtra);
            write_float(cpu, actor + kScaleZ, x + kExtra + kChangedGap);
            changed++;
            if (g_trace)
                fprintf(stderr, "[hd-look] island model 0x%08X (room %u): shown within %.0f, now %.0f\n", actor,
                        mem_read32(cpu, actor + 0xB0u), x, x + kExtra);
        } else if (!on && gap == kChangedGap) {
            write_float(cpu, actor + kScaleX, x - kExtra);
            write_float(cpu, actor + kScaleZ, x - kExtra + kGap);
            changed++;
        }
    }
    static unsigned last_count = ~0u, last_loaded = ~0u;
    if (g_trace && (count != last_count || loaded != last_loaded || changed)) {
        fprintf(stderr, "[hd-look] retrace=%llu island models: %u, loaded %u, changed now %u\n", retrace, count,
                loaded, changed);
        last_count = count;
        last_loaded = loaded;
    }
}

// BLUEWAKE_HD_TRACE=1: every 300 retraces, each island model within its show
// distance or loaded: its room, Link's distance, the distance, whether it is
// loaded and its fade (0 to 255).
static void trace_islands(CPUState* cpu, unsigned long long retrace) {
    float px = 0.0f, pz = 0.0f;
    bool player = false;
    u32 node = mem_read32(cpu, kActorQueue);
    for (u32 n = 0; n < kMaxActors && guest_pointer(node) && !player; ++n, node = mem_read32(cpu, node + kNodeNext)) {
        const u32 actor = mem_read32(cpu, node + kTagActor);
        if (guest_pointer(actor) && mem_read16(cpu, actor + kProcName) == 0x00A9u) {  // fpcNm_PLAYER_e
            px = read_float(cpu, actor + 0x1F8u);
            pz = read_float(cpu, actor + 0x200u);
            player = true;
        }
    }
    if (!player)
        return;
    node = mem_read32(cpu, kActorQueue);
    for (u32 n = 0; n < kMaxActors && guest_pointer(node); ++n, node = mem_read32(cpu, node + kNodeNext)) {
        const u32 actor = mem_read32(cpu, node + kTagActor);
        if (!guest_pointer(actor) || mem_read16(cpu, actor + kProcName) != kLodBg)
            continue;
        const float dx = read_float(cpu, actor + 0x1F8u) - px, dz = read_float(cpu, actor + 0x200u) - pz;
        const float dist = sqrtf(dx * dx + dz * dz), shown = read_float(cpu, actor + kScaleX);
        const u32 model = mem_read32(cpu, actor + 0x29Cu);
        if (dist < shown || model != 0u)
            fprintf(stderr, "[hd-look] retrace=%llu room %u: %.0f away, shown within %.0f, %s, fade %u, pause %u mount 0x%08X step 0x%08X\n", retrace,
                    mem_read32(cpu, actor + 0xB0u), dist, shown, model ? "loaded" : "not loaded",
                    mem_read8(cpu, actor + 0x2A8u), mem_read8(cpu, actor + 0x0Bu), mem_read32(cpu, actor + 0x2B4u),
                    mem_read32(cpu, actor + 0x298u));
    }
}

// Cloud shadows: on the Great Sea only (stage "sea"), with the game's camera.
// GZLE01: g_dComIfG_gameInfo.play is at 0x803C5EA8; its mCurStage (the stage's
// name first) at +0x3E94 and mCurrentView (view_class*) at +0x4A58. In the
// view: mNear +0xC8, mFovy (degrees) +0xD0, mAspect +0xD4, mInvViewMtx +0x170.
enum {
    kCurStageName = 0x803C9D3Cu,
    kCurrentView = 0x803CA900u,
    kViewNear = 0xC8u,
    kViewFovy = 0xD0u,
    kViewAspect = 0xD4u,
    kViewInvMtx = 0x170u,
    kSunPos = 0x803E5568u,  // g_env_light.mSunPos: the camera's eye plus the way to the sun
};

static void clouds(CPUState* cpu, unsigned long long retrace) {
    bool show = false;
    if (g_clouds == 1 || g_sun == 1) {
        char stage[8];
        for (u32 i = 0; i < sizeof stage; ++i)
            stage[i] = (char)mem_read8(cpu, kCurStageName + i);
        const u32 view = mem_read32(cpu, kCurrentView);
        if (memcmp(stage, "sea", 4) == 0 && guest_pointer(view)) {
            float inv[12];
            for (u32 i = 0; i < 12; ++i)
                inv[i] = read_float(cpu, view + kViewInvMtx + i * 4u);
            const float fovy = read_float(cpu, view + kViewFovy);
            const float aspect = read_float(cpu, view + kViewAspect);
            const float near_plane = read_float(cpu, view + kViewNear);
            if (fovy > 1.f && fovy < 170.f && aspect > 0.1f && near_plane > 0.f) {
                if (g_trace && retrace % 300u == 0u)
                    fprintf(stderr, "[hd-look] camera eye %.0f %.0f %.0f fovy %.1f aspect %.3f near %.2f\n", inv[3],
                            inv[7], inv[11], fovy, aspect, near_plane);
                aurora_set_world_view(inv, tanf(fovy * 0.5f * 3.14159265f / 180.f), aspect, near_plane, 0.f,
                                      (float)retrace / 59.94f);
                const float sx = read_float(cpu, kSunPos) - inv[3];
                const float sy = read_float(cpu, kSunPos + 4u) - inv[7];
                const float sz = read_float(cpu, kSunPos + 8u) - inv[11];
                aurora_set_sun_direction(sx, sy, sz);
                if (g_trace && retrace % 300u == 0u)
                    fprintf(stderr, "[hd-look] sun from the eye %.0f %.0f %.0f\n", sx, sy, sz);
                show = true;
            }
        }
    }
    const bool clouds_on = show && g_clouds == 1, sun_on = show && g_sun == 1;
    if (clouds_on != g_clouds_shown && g_clouds >= 0) {
        aurora_set_cloud_shadows(clouds_on);
        g_clouds_shown = clouds_on;
    }
    if (sun_on != g_sun_shown && g_sun >= 0) {
        aurora_set_sun_shadows(sun_on);
        g_sun_shown = sun_on;
    }
}

void bluewake_hd_look_retrace(CPUState* cpu) {
    static unsigned long long retrace;
    retrace++;
    if (cpu == NULL || cpu->ram == NULL)
        return;
    if (bluewake_hd_distant_islands || g_islands_were_on)
        islands(cpu, bluewake_hd_distant_islands, retrace);
    g_islands_were_on = bluewake_hd_distant_islands;
    clouds(cpu, retrace);
    if (g_trace && retrace % 300u == 0u)
        trace_islands(cpu, retrace);
}
