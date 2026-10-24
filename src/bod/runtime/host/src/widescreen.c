#include "widescreen.h"

#include "cosmetics.h"
#include "game_options.h"
#include "game_state.h"
#include "gxruntime/aurora_backend.h"
#include "gxruntime/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool bluewake_widescreen_active;

static float g_inverse = 1.f; // 1/s: the window's width in units of the game's 4:3 frame
static float g_edge;          // E for the whole picture (fills)
static float g_hud_edge;      // E for the HUD: 0 unless the HUD option is on
static u32 g_epoch;           // retraces, for the per-frame pane marks below

// --- helpers ---------------------------------------------------------------------

static float f32_from_bits(u32 bits) {
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static u32 bits_from_f32(float value) {
    u32 bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static bool guest_pointer(u32 address) { return address >= 0x80000000u && address < 0x81800000u; }

static float read_f32(CPUState* cpu, u32 address) { return f32_from_bits(mem_read32(cpu, address)); }
static void write_f32(CPUState* cpu, u32 address, float value) { mem_write32(cpu, address, bits_from_f32(value)); }

static void set_single(CPUState* cpu, u32 reg, float value) {
    cpu->fpr[reg] = value;
    cpu->ps1[reg] = value;
}

static s16 clamp_s16(float value) {
    if (value > 32767.f)
        return 32767;
    if (value < -32768.f)
        return -32768;
    return (s16)lrintf(value);
}

// --- per retrace -------------------------------------------------------------------

// Vanilla values of what is written below (constructor defaults and .sdata2
// constants of GZLE01; docs/fork/research/aspect-hud.md section 2.2).
enum {
    MAP_HIO = 0x803E68DCu, // g_meter_mapHIO: +0x8 map x, +0xC hidden x, +0x14 free-camera icon x (s16)
    WIPE_BOX = 0x803F89B8u, // @6224-@6227 (d_drawlist.cpp): the wipe quad x, y, w, h (f32)
    FADER = 0x803F6898u,    // mDoGph_gInf_c::mFader: JUTFader*, its mBox (f32 x1, y1, x2, y2) at +0x10
    FRAME_BUFFER_TEX = 0x803F68A0u, // mDoGph_gInf_c::mFrameBufferTex: void*, the 320x240 texture the capture draws
    ZBUFFER_TEX = 0x803F68A4u,      // mDoGph_gInf_c::mZbufferTex: void*, where the scene-change wipe saves its blurred frames
    MAP_ICON_FREE_X = 0x803F6AA2u,   // dMap_c::mIconFreePosX (s16)
    MAP_SCISSOR_X = 0x803F6AE4u,     // dMap_c::mScissorOrigX (s32)
    MAP_SCISSOR_WIDTH = 0x803F6AE8u, // dMap_c::mScissorWidth (s32)
};

// The rupee sparkle emitter and the HUD edge it was last placed for (BR-0027).
static u32 g_rupy_meter;
static u32 g_rupy_emitter;
static float g_rupy_applied;

static void follow_rupy_sparkle(CPUState* cpu) {
    if (!guest_pointer(g_rupy_meter))
        return;
    const u32 emitter = mem_read32(cpu, g_rupy_meter + 0x2F20u);
    if (!guest_pointer(emitter)) {
        g_rupy_emitter = 0u;
        return;
    }
    if (emitter != g_rupy_emitter) {
        g_rupy_emitter = emitter; // placed by the game for the current edge
        g_rupy_applied = g_hud_edge;
        return;
    }
    if (g_rupy_applied != g_hud_edge) {
        // JPABaseEmitter::mGlobalTranslation.x (+0x1E4)
        write_f32(cpu, emitter + 0x1E4u, read_f32(cpu, emitter + 0x1E4u) + (g_hud_edge - g_rupy_applied));
        fprintf(stderr, "[widescreen] rupee sparkle %08X moved by %.1f\n", emitter, g_hud_edge - g_rupy_applied);
        g_rupy_applied = g_hud_edge;
    }
}

void bluewake_widescreen_attach(CPUState* cpu) {
    (void)cpu;
    g_inverse = 1.f;
    g_edge = g_hud_edge = 0.f;
    g_rupy_meter = g_rupy_emitter = 0u;
}

void bluewake_widescreen_retrace(CPUState* cpu) {
    ++g_epoch;
    const float squeeze = dol_aurora_widescreen_squeeze();
    const float inverse = squeeze > 0.f ? 1.f / squeeze : 1.f;
    g_inverse = inverse;
    g_edge = 329.5f * (inverse - 1.f);
    g_hud_edge = bluewake_game_options_hud_edges() ? g_edge : 0.f;
    bluewake_widescreen_active = inverse > 1.001f;
    if (cpu == NULL || cpu->ram == NULL)
        return;
    follow_rupy_sparkle(cpu);

    // The pause and game-over captures draw mFrameBufferTex, which holds the
    // game's last copy of the scene (drawDepth takes it every frame, from the
    // game's 4:3 slice of the EFB). The renderer is told its address (not in
    // the GX stream: a retrace can come in the middle of a command), so that
    // each copy into it also keeps a window-wide one, which the backdrop shows.
    {
        const u32 texture = mem_read32(cpu, FRAME_BUFFER_TEX);
        dol_aurora_set_backdrop_source(bluewake_widescreen_active && guest_pointer(texture) ? texture & 0x01FFFFFFu : 0u);
        const u32 zbuffer = mem_read32(cpu, ZBUFFER_TEX);
        dol_aurora_set_backdrop_source2(bluewake_widescreen_active && guest_pointer(zbuffer) ? zbuffer & 0x01FFFFFFu : 0u);
    }

    // The minimap: raw GX at s16 x positions in the 2D box (HUD, left; its
    // free-camera icon, right).
    mem_write16(cpu, MAP_HIO + 0x8u, (u16)clamp_s16(35.f - g_hud_edge));
    // Hidden: the game's -180 leaves the right edge of the Great Sea's map
    // (wider than an island's) inside the frame, where a TV's overscan hid
    // it; in a window it showed as a strip at the left edge. -320 hides it.
    mem_write16(cpu, MAP_HIO + 0xCu, (u16)clamp_s16(-320.f - g_hud_edge));
    mem_write16(cpu, MAP_HIO + 0x14u, (u16)clamp_s16(590.f + g_hud_edge));
    // The map copies that x into dMap_c::mIconFreePosX only when the HUD is
    // made (initMapCtrlDisp), so the icon drawn there (the free-camera icon,
    // and the eye that says C-up looks around) kept the place it had then: a
    // window resized later, or a state saved at another size, left it behind.
    mem_write16(cpu, MAP_ICON_FREE_X, (u16)clamp_s16(590.f + g_hud_edge));

    // The scene-change wipe: a textured quad over the 2D box (-9, -21, 659,
    // 524), scaled about its centre to the window (with 2 percent to spare),
    // so the pattern keeps its shape. Wider windows only.
    const float wide = inverse > 1.f ? inverse * 1.02f : 1.f;
    const float w = 659.f * wide, h = 524.f * wide;
    write_f32(cpu, WIPE_BOX + 0x0u, wide > 1.f ? 320.5f - w * 0.5f : -9.f);
    write_f32(cpu, WIPE_BOX + 0x4u, wide > 1.f ? 241.f - h * 0.5f : -21.f);
    write_f32(cpu, WIPE_BOX + 0x8u, w);
    write_f32(cpu, WIPE_BOX + 0xCu, h);

    // The fade to and from black: an untextured box drawn with a 0..640 ortho
    // (JFWDisplay::endGX); widened to the window.
    const u32 fader = mem_read32(cpu, FADER);
    if (guest_pointer(fader)) {
        const float half = 320.f * (inverse > 1.f ? inverse : 1.f);
        write_f32(cpu, fader + 0x10u, 320.f - half);
        write_f32(cpu, fader + 0x18u, 320.f + half);
    }
}

// --- markers ----------------------------------------------------------------------

void bluewake_widescreen_enter(CPUState* cpu, u32 address) {
    (void)cpu;
    static u32 seq;
    seq = (seq + 1u) & 0xFFFFFu;
    if (seq == 0u)
        seq = 1u;
    // Kind 1: the next EFB copy is a frozen backdrop of the whole window.
    // Kind 2: black side margins now (the views mask the scene in 4:3).
    const u32 kind = (address == BLUEWAKE_WS_MENU_CAPTURE || address == BLUEWAKE_WS_GAMEOVER_CAPTURE) ? 1u : 2u;
    static u32 logged[4];
    for (u32 i = 0; i < 4u; ++i) {
        if (logged[i] == address)
            break;
        if (logged[i] == 0u) {
            logged[i] = address;
            fprintf(stderr, "[widescreen] marker kind %u first at 0x%08X\n", kind, address);
            break;
        }
    }
    dol_platform_gx_write(0x61u, 1u);
    dol_platform_gx_write((0x7Au << 24) | (seq << 4) | kind, 4u);
}

// --- HUD panes (J2DPane::draw) --------------------------------------------------

// The HUD's screens (d_meter.cpp): sMainParts1/2/3 and the timers. The choice
// road (0x803F708C) stays centred.
// dMeter_menuPlusMove's `static s16 moveStatus` (symbols.txt: the fifth moveStatus, moveStatus$9965).
#define MENU_PLUS_MOVE_STATUS 0x803F7188u
static const u32 kHudRoots[] = {0x803F7078u, 0x803F707Cu, 0x803F7080u, 0x803F7084u, 0x803F7088u};

// Panes that stay where they are: the boss bar (centred), the small enemy bar
// (it follows the targeted enemy), and the minimap's arrows and wind arrow,
// which dMeter_menuPlusMove places from the map's own position each frame
// (d_meter.cpp:4780-4802), already at the edge through the map HIO.
// Tags are four characters, big-endian ('yup' has three: a zero high byte).
static const u32 kKeepTags[] = {
    0x62656E31u, 0x62656E32u, 0x62656E33u, 0x62656E34u, // ben1-ben4
    0x656E656Du, 0x656E6D32u, 0x62657931u, 0x62657932u, // enem enm2 bey1 bey2
    0x656E6531u, 0x656E6532u, 0x656E6533u, 0x656E656Bu, // ene1-ene3 enek
    0x796C6566u, 0x79726967u, 0x00797570u, 0x77697931u, // ylef yrig yup wiy1
};

// Per-frame marks by pane address: a member of a HUD screen that kept its
// place (a full-width group: its children decide for themselves), or a pane
// already moved (its children follow through its draw matrix).
enum { MARK_MEMBER = 1, MARK_MOVED = 2, MARK_SLOTS = 2048 };
static u32 g_mark_pane[MARK_SLOTS];
static u32 g_mark_epoch[MARK_SLOTS];
static u8 g_mark_kind[MARK_SLOTS];
static float g_mark_dx[MARK_SLOTS]; // how far a moved pane (or the pane it follows) was moved

static u32 slot_of(u32 pane) { return ((pane >> 2) * 2654435761u) >> 21; }

// A mark lives for this retrace and the one before: the game draws at 30
// frames a second, so a retrace can fall in the middle of its HUD's draw, and
// the panes drawn after it must still see their parents' marks (with a mark of
// one retrace, those children were skipped: the hearts or the buttons left at
// their 4:3 places for a frame now and then).
static bool live(u32 i) { return g_epoch - g_mark_epoch[i] <= 1u; }

static u8 mark_dx_of(u32 pane, float* dx) {
    for (u32 i = slot_of(pane), n = 0; n < 16u; i = (i + 1u) & (MARK_SLOTS - 1u), ++n) {
        if (!live(i))
            return 0u;
        if (g_mark_pane[i] == pane) {
            if (dx != NULL)
                *dx = g_mark_dx[i];
            return g_mark_kind[i];
        }
    }
    return 0u;
}

static u8 mark_of(u32 pane) { return mark_dx_of(pane, NULL); }

static void mark_dx(u32 pane, u8 kind, float dx) {
    for (u32 i = slot_of(pane), n = 0; n < 16u; i = (i + 1u) & (MARK_SLOTS - 1u), ++n) {
        if (!live(i) || g_mark_pane[i] == pane) {
            g_mark_epoch[i] = g_epoch;
            g_mark_pane[i] = pane;
            g_mark_kind[i] = kind;
            g_mark_dx[i] = dx;
            return;
        }
    }
}

static void mark(u32 pane, u8 kind) { mark_dx(pane, kind, 0.f); }

// After PSMTXConcat(parent.mDrawMtx, mMtx, mDrawMtx) in J2DPane::draw: r27 is
// the pane, r30 its parent. J2DPane: mTag +0x8, mBounds (x1, y1, x2, y2) +0xC,
// mDrawMtx +0x6C (row 0 translation at +0x78).
// Full-frame masks: black or dimming panes that cover the 4:3 frame in their
// screen (game over's 'blak', the pause menus' 'msk'/'msk0', the save
// screen's 'wp00'-'wp07', the options' 'blak'). Under widescreen they are
// widened about the frame's centre to cover the window, or the game over's
// black and the menus' dimming stop at the 4:3 edges.
static const u32 kFillTags[] = {
    0x626C616Bu, 0x006D736Bu, 0x6D736B30u,             // blak msk msk0
    0x77703030u, 0x77703031u, 0x77703032u, 0x77703033u, // wp00-wp03
    0x77703034u, 0x77703035u, 0x77703036u, 0x77703037u, // wp04-wp07
    0x0000636Cu,                                       // cl: the Ballad of Gales chart's backing (-10..650)
};

static bool fill_pane(CPUState* cpu) {
    const u32 pane = cpu->gpr[27];
    if (!guest_pointer(pane))
        return false;
    const u32 tag = mem_read32(cpu, pane + 0x8u);
    bool fill = false;
    for (u32 i = 0; i < sizeof kFillTags / sizeof kFillTags[0] && !fill; ++i)
        fill = kFillTags[i] == tag;
    if (!fill)
        return false;
    // The pane's draw matrix after the concat: m00 at +0x6C, m03 at +0x78.
    // An edge at the frame's edge moves to the window's; a strip that does not
    // reach an edge (the Pictobox's frame around the photo) stays. So a full
    // frame mask covers the window, and the Pictobox's side strips cover the
    // margins as they cover the sides of the 4:3 frame (drawn last, over the
    // wind streaks and rigging lines that once showed there).
    const float width = read_f32(cpu, pane + 0x14u) - read_f32(cpu, pane + 0xCu);
    const float scale = read_f32(cpu, pane + 0x6Cu);
    const float left = read_f32(cpu, pane + 0x78u);
    const float right = left + width * scale;
    if (!(width > 0.f) || !(scale > 0.f))
        return false;
    const bool at_left = left <= 1.f, at_right = right >= 639.f;
    if (!at_left && !at_right)
        return false;
    const float new_left = at_left ? 320.5f - 329.5f * g_inverse : left;
    const float new_right = at_right ? 320.5f + 329.5f * g_inverse : right;
    write_f32(cpu, pane + 0x6Cu, (new_right - new_left) / width);
    write_f32(cpu, pane + 0x78u, new_left);
    return true;
}

// BLUEWAKE_WS_PANE_LOG=1 (debugging): each pane drawn, once per tag and
// parent, with where it is and what the HUD rule did with it.
static void log_pane(CPUState* cpu, u32 pane, u32 parent, bool in_hud, u8 parent_mark, const char* what) {
    static int enabled = -1;
    if (enabled < 0) {
        const char* env = getenv("BLUEWAKE_WS_PANE_LOG");
        enabled = env != NULL && env[0] == '1';
    }
    if (!enabled)
        return;
    const u32 tag = mem_read32(cpu, pane + 0x8u);
    static u64 seen[1024];
    static u32 count;
    const u64 key = ((u64)tag << 32) | parent;
    for (u32 i = 0; i < count; ++i)
        if (seen[i] == key)
            return;
    if (count < 1024u)
        seen[count++] = key;
    char name[5] = {0};
    for (int i = 0; i < 4; ++i) {
        const char c = (char)(tag >> (24 - 8 * i));
        name[i] = c >= 32 && c < 127 ? c : '.';
    }
    const float width = read_f32(cpu, pane + 0x14u) - read_f32(cpu, pane + 0xCu);
    fprintf(stderr,
            "[ws-pane] tag=%s pane=%08X parent=%08X hud=%d mark=%u x=%.1f w=%.1f scale=%.3f %s white=%08X black=%08X\n",
            name, pane, parent, in_hud ? 1 : 0, parent_mark, read_f32(cpu, pane + 0x78u), width,
            read_f32(cpu, pane + 0x6Cu), what, mem_read32(cpu, pane + 0x104u), mem_read32(cpu, pane + 0x108u));
}

// BLUEWAKE_WS_PANE_HIDE=tag,tag (debugging): those panes drawn at zero size,
// to see which pane is which on screen.
static void hide_pane(CPUState* cpu) {
    static int count = -1;
    static u32 tags[16];
    if (count < 0) {
        count = 0;
        const char* env = getenv("BLUEWAKE_WS_PANE_HIDE");
        for (const char* at = env; at != NULL && *at != '\0' && count < 16;) {
            u32 tag = 0;
            int n = 0;
            for (; at[n] != '\0' && at[n] != ',' && n < 4; ++n)
                tag = tag << 8 | (u8)at[n];
            tags[count++] = tag;
            at = strchr(at, ',');
            if (at != NULL)
                ++at;
        }
    }
    const u32 pane = cpu->gpr[27];
    if (count == 0 || !guest_pointer(pane))
        return;
    const u32 tag = mem_read32(cpu, pane + 0x8u);
    for (int i = 0; i < count; ++i)
        if (tags[i] == tag)
            for (u32 k = 0; k < 12u; ++k)
                write_f32(cpu, pane + 0x6Cu + 4u * k, 0.f);
}

static void hud_pane(CPUState* cpu) {
    const u32 pane = cpu->gpr[27];
    const u32 parent = cpu->gpr[30];
    if (!guest_pointer(pane) || !guest_pointer(parent))
        return;
    bool in_hud = false;
    for (u32 i = 0; i < sizeof kHudRoots / sizeof kHudRoots[0] && !in_hud; ++i)
        in_hud = mem_read32(cpu, kHudRoots[i]) == parent;
    const u8 parent_mark = in_hud ? 0u : mark_of(parent);
    log_pane(cpu, pane, parent, in_hud, parent_mark, in_hud || parent_mark != 0u ? "hud" : "other");
    if (!in_hud && parent_mark == 0u)
        return;
    if (parent_mark == MARK_MOVED) {
        float dx = 0.f;
        mark_dx_of(parent, &dx);
        mark_dx(pane, MARK_MOVED, dx); // it follows its parent's move
        return;
    }
    const u32 tag = mem_read32(cpu, pane + 0x8u);
    // The map's three arrows ('yup', 'ylef', 'yrig'): while the map sits at its small place they follow the map's own
    // position, already at the window's edge through the map HIO, and stay. While the clock and compass are out
    // (dMeter_menuPlusMove's moveStatus 4 and 5, the function's static at 0x803F7188) dMeter_menuPlusMove places them
    // from the logical places of those panes, which this draw moves to the edge: the arrows move with them, each to
    // its own side, or they stay at their 4:3 places beside icons that went to the window's edge.
    const bool arrow = tag == 0x00797570u || tag == 0x796C6566u || tag == 0x79726967u;
    const u16 move_status = mem_read16(cpu, MENU_PLUS_MOVE_STATUS);
    const bool follow_panes = arrow && (move_status == 4u || move_status == 5u);
    for (u32 i = 0; i < sizeof kKeepTags / sizeof kKeepTags[0] && !follow_panes; ++i) {
        if (kKeepTags[i] == tag) {
            mark(pane, MARK_MOVED); // kept, and so are its children
            return;
        }
    }
    const float width = read_f32(cpu, pane + 0x14u) - read_f32(cpu, pane + 0xCu);
    if (width >= 400.f) {
        mark(pane, MARK_MEMBER);
        return;
    }
    const float scale = read_f32(cpu, pane + 0x6Cu);
    const float x = read_f32(cpu, pane + 0x78u);
    const float centre = x + 0.5f * width * scale;
    const float dx = centre < 320.5f ? -g_hud_edge : g_hud_edge;
    write_f32(cpu, pane + 0x78u, x + dx);
    mark_dx(pane, MARK_MOVED, dx);
}

// Hook 15, where J2DPane::draw has made the pane's clip rectangle (mDrawBounds,
// +0x2C: x1, y1, x2, y2) from its bounds (+0xC) at its parent's place
// (mGlobalBounds, +0x1C) cut to the parent's own clip rectangle, r29 the clip
// flag: a screen drawn with clipping (the swim timer: its water under a mask)
// scissors each pane to that rectangle, which knows nothing of the HUD move
// (the draw matrix), so a moved pane was cut away (the swim timer's water,
// past x = 640). A moved pane's rectangle moves with it: one moved itself is
// not cut to its unmoved parent; one that follows a moved parent is cut to
// the parent's moved rectangle.
static void clip_pane(CPUState* cpu) {
    const u32 pane = cpu->gpr[27], parent = cpu->gpr[30];
    if ((cpu->gpr[29] & 0xFFu) == 0u || !guest_pointer(pane) || !guest_pointer(parent))
        return;
    // BLUEWAKE_WS_CLIP_LOG=1 (debugging): each clipped pane's rectangle as the
    // game made it, and what the move made of it (the first 400).
    static int clip_log = -1;
    static int clip_logged;
    if (clip_log < 0) {
        const char* env = getenv("BLUEWAKE_WS_CLIP_LOG");
        clip_log = env != NULL && env[0] == '1';
    }
    float dx = 0.f;
    const u8 kind = mark_dx_of(pane, &dx);
    if (clip_log && clip_logged < 400) {
        ++clip_logged;
        const u32 tag = mem_read32(cpu, pane + 0x8u);
        fprintf(stderr, "[ws-clip] tag=%c%c%c%c pane=%08X parent=%08X mark=%u dx=%.2f game=(%.2f,%.2f,%.2f,%.2f)\n",
                (char)(tag >> 24), (char)(tag >> 16), (char)(tag >> 8), (char)tag, pane, parent, kind, dx,
                read_f32(cpu, pane + 0x2Cu), read_f32(cpu, pane + 0x30u), read_f32(cpu, pane + 0x34u),
                read_f32(cpu, pane + 0x38u));
    }
    if (kind != MARK_MOVED || dx == 0.f)
        return;
    float x1 = read_f32(cpu, pane + 0x0Cu) + read_f32(cpu, parent + 0x1Cu) + dx;
    float y1 = read_f32(cpu, pane + 0x10u) + read_f32(cpu, parent + 0x20u);
    float x2 = read_f32(cpu, pane + 0x14u) + read_f32(cpu, parent + 0x1Cu) + dx;
    float y2 = read_f32(cpu, pane + 0x18u) + read_f32(cpu, parent + 0x20u);
    float parent_dx = 0.f;
    if (mark_dx_of(parent, &parent_dx) == MARK_MOVED && parent_dx == dx) {
        x1 = fmaxf(x1, read_f32(cpu, parent + 0x2Cu));
        y1 = fmaxf(y1, read_f32(cpu, parent + 0x30u));
        x2 = fminf(x2, read_f32(cpu, parent + 0x34u));
        y2 = fminf(y2, read_f32(cpu, parent + 0x38u));
    }
    write_f32(cpu, pane + 0x2Cu, x1);
    write_f32(cpu, pane + 0x30u, y1);
    write_f32(cpu, pane + 0x34u, x2);
    write_f32(cpu, pane + 0x38u, y2);
}

// --- the hooks ---------------------------------------------------------------------

u32 bluewake_widescreen_native_hook(CPUState* cpu, u32 id) {
    switch (id) {
    case 6u:
        // Before bl mDoLib_clipper::setup in view_setup (0x8017BF90): f2 is the
        // view's aspect. A wider frustum, so actors at the sides draw and run
        // (SoH's "Widescreen Actor Culling"; max(A / (4/3), 1)).
        if (g_inverse > 1.f)
            set_single(cpu, 2, (float)cpu->fpr[2] * g_inverse);
        return 0u;
    case BLUEWAKE_WIDESCREEN_CLOTH_HOOK:
        // Before bl C_MTXPerspective in the pause menu's cloth draw
        // (dDlst_MENU_CLOTH_c, 0x801DFA04): f2 is the aspect. The renderer
        // widens every full-width 3D draw (the squeeze), which showed the
        // cloth's edges inside the window; the cloth takes the squeeze back,
        // so it covers the window as it covers the 4:3 frame. Not the file
        // screen's cloth (dDlst_FLSEL_CLOTH_c), a panel behind the files that
        // is meant to keep its shape (a module built with that site too).
        if (g_inverse > 1.f && cpu->pc == 0x801DFA04u)
            set_single(cpu, 2, (float)cpu->fpr[2] / g_inverse);
        return 0u;
    case BLUEWAKE_WIDESCREEN_CLIP_HOOK:
        if (g_hud_edge != 0.f)
            clip_pane(cpu);
        return 0u;
    case BLUEWAKE_WIDESCREEN_SCISSOR_HOOK:
        // After lfs f0, 1024.0 in J2DGrafContext::setScissor (0x802CD4BC):
        // the limit of the scissor's right edge. A pane moved to the window's
        // right edge past 32:9 has its clip rectangle beyond 1024 (the swim
        // timer at 3840x1080: x 1082-1146), which the limit made empty, so the
        // water and the swirl were not drawn. The limit becomes the largest
        // the GX scissor holds (2047 - 342); the game itself never goes past 640.
        if (g_hud_edge != 0.f && g_inverse > 1.f)
            set_single(cpu, 0, 1705.f);
        return 0u;
    case 7u:
        hide_pane(cpu);
        if (guest_pointer(cpu->gpr[27]))
            bluewake_cosmetics_pane(cpu, cpu->gpr[27]);
        if (guest_pointer(cpu->gpr[27]))
            bluewake_fs_pane(cpu, cpu->gpr[27]);
        if (g_inverse > 1.f && fill_pane(cpu))
            return 0u;
        if (g_hud_edge != 0.f)
            hud_pane(cpu);
        return 0u;
    case 8u:
        // Before bl dDlst_2Dm_c::setPos(x0, y0, x1, y1) in dMeter_clockMultiMove
        // (0x801FF068): the sun and moon swirl, left.
        if (g_hud_edge != 0.f) {
            const s16 dx = clamp_s16(g_hud_edge);
            cpu->gpr[4] = (u32)(s32)(s16)((s16)cpu->gpr[4] - dx);
            cpu->gpr[6] = (u32)(s32)(s16)((s16)cpu->gpr[6] - dx);
        }
        return 0u;
    case 9u:
        // After lfs f4, REG6_F(0) in dMeter_swimTekariScroll (0x80201D8C): the
        // swim gauge's shine sprite, right.
        if (g_hud_edge != 0.f)
            set_single(cpu, 4, (float)cpu->fpr[4] + g_hud_edge);
        return 0u;
    case 10u:
        // After lfs f0, 320.0 in dMeter_rupyInit (0x801FC28C): the rupee
        // sparkle is placed at the pane's centre - f0; the pane moved right.
        // The meter is found by its pane: mPosCenter.x of field_0x2a20 (+0x2A3C)
        // is a plausible HUD x in the register that holds it. Its sparkle
        // emitter (mpRupyParticle, +0x2F20) is then followed by the retrace.
        for (u32 r = 31u; r >= 26u; --r) {
            const u32 m = cpu->gpr[r];
            if (guest_pointer(m) && guest_pointer(mem_read32(cpu, m + 0x2A20u))) {
                const float px = read_f32(cpu, m + 0x2A3Cu);
                if (px > 300.f && px < 700.f) {
                    g_rupy_meter = m;
                    break;
                }
            }
        }
        if (g_hud_edge != 0.f)
            set_single(cpu, 0, 320.f - g_hud_edge);
        return 0u;
    case 11u:
        // Before the blr of dMap_c::calcScissor (0x8004A474): it made the map
        // points' scissor from the map's place, which at the window's edge is
        // left of the 4:3 frame, so x < 0 left it zero wide and the points
        // (Link's cursor among them) were not drawn. The frame cannot hold a
        // scissor there, so it spans the window across; the height stays.
        // dMap_c::drawPoint already keeps each point within the map's view.
        if (g_hud_edge != 0.f) {
            mem_write32(cpu, MAP_SCISSOR_X, 0u);
            mem_write32(cpu, MAP_SCISSOR_WIDTH, 640u);
        }
        return 0u;
    default:
        return 0u;
    }
}
