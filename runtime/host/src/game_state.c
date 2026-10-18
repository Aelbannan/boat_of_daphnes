// A snapshot of where the game is: see game_state.h.
#include "game_state.h"
#include "dev_tools.h"
#include "jabun_guard.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum {
    CUR_STAGE = 0x803C9D3Cu,  // g_dComIfG_gameInfo.play.mCurStage: name[8], point (s16), room (s8), layer (s8)
    NEXT_STAGE = 0x803C9D48u, // play.mNextStage (dStage_nextStage_c): as mCurStage, then +0xC enable, +0xD wipe
    PLAYER = 0x803CA754u,     // play.mpPlayerPtr[0]: Link's actor
    ACTOR_POS = 0x1F8u,       // fopAc_ac_c::current.pos (cXyz)
    ACTOR_SHAPE_ANGLE_Y = 0x20Eu, // fopAc_ac_c::shape_angle.y (s16)
    RESTART = 0x803C5D30u,    // save.mRestart (dSv_restart_c): room +0x0, angle +0x16, pos +0x18, param +0x24
    EVENT_MODE = 0x803C9EA2u, // play.mEvtCtrl.mMode: 0 when no event runs
    VIEW = 0x803CAB58u,       // drawlist.mpView (gameInfo +0x5D1C, +0x234): the camera's view_class
    VIEW_PROJ_VIEW = 0x1A0u,  // view_class::mProjViewMtx (Mtx44)
    STAY_ROOM = 0x803F6A78u,  // dStage_roomControl_c::mStayNo: the room Link is in
    EVENT_ID = 0x803C9EB8u,   // play.mEvtCtrl.mEventId (s16)
    SCENE_LIST = 0x80372150u, // g_fopScnTg_SceneList: tags whose data (+0xC) is the scene process, name at +0x8
    SCN_FS = 0x428u,          // dScnName_c::dFs_c (the dFile_select_c)
    SCN_MAIN_PROC = 0x554u,   // dScnName_c::mMainProc
    FS_NEW = 0x3914u,         // dFile_select_c::dataNew[3]
    FS_SLOT = 0x3922u,        // selectNum
    FS_SUB = 0x392Bu,         // the DataSelProc index (dFile_select_c::_move)
    FS_END = 0x392Cu,         // selectEnd
    STAGE_MAP_INFO = 0x803C4C08u + 0x3EB0u + 0x14u, // play.mStageData.mpMapInfo (stage_map_info_class*); mOceanXZ at +0x36
    DATA_NUM = 0x803C4C08u + 0x1290u, // g_dComIfG_gameInfo.info.mDataNum (u8), mNewFile after it
};

typedef struct Teleport {
    bool pending;
    char stage[8];
    int room;
    float pos[3];
    int angle_y;
    bool ship;         // arrive on the ship (point 100), then place it at ship_pos
    float ship_pos[3];
    int ship_angle_y;
} Teleport;
static Teleport g_teleport;
// After a teleport on the ship: the ship's place, applied once Link rides it in the new scene.
static struct { bool pending; float pos[3]; int angle_y; unsigned wait; } g_ship_fix;

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

// --- the file select's plates ---------------------------------------------------------------------------------------
// dFile_select_c keeps each file's plate as arrays of fopMsgM_pane_class (0x38 bytes, the J2DPicture* first): pane0 the
// small record tabs, pane1 the plate and its frame, pane2 the two Second Quest marker panes (tww d_file_select.h).
static atomic_uint g_rando_slots;
void bluewake_fs_set_rando_slots(unsigned mask) { atomic_store(&g_rando_slots, mask); }

enum { FS_PANE_STRIDE = 0x38, FS_PANE_MAX = 29 };
typedef struct FsPane {
    u32 pane;
    u8 slot, group, index;
} FsPane;
static FsPane g_fs_panes[3 * (16 + FS_PANE_MAX + 2)];
static unsigned g_fs_pane_count;

static void fs_collect_panes(CPUState* cpu, u32 fs) {
    static const u32 kPane0[3] = {0x1238u, 0x15B8u, 0x1938u}, kPane1[3] = {0x1CB8u, 0x2310u, 0x2968u},
                     kPane2[3] = {0x10E8u, 0x1158u, 0x11C8u};
    unsigned n = 0;
    for (unsigned slot = 0; slot < 3u; ++slot) {
        for (unsigned i = 0; i < 12u; ++i) {  // pane0[2..11] are tinted
            const u32 p = mem_read32(cpu, fs + kPane0[slot] + i * FS_PANE_STRIDE);
            if (p >= 0x80000000u && p < 0x81800000u)
                g_fs_panes[n++] = (FsPane){p, (u8)slot, 0, (u8)i};
        }
        for (unsigned i = 0; i < 21u; ++i) {  // pane1[3..20]
            const u32 p = mem_read32(cpu, fs + kPane1[slot] + i * FS_PANE_STRIDE);
            if (p >= 0x80000000u && p < 0x81800000u)
                g_fs_panes[n++] = (FsPane){p, (u8)slot, 1, (u8)i};
        }
        for (unsigned i = 0; i < 2u; ++i) {
            const u32 p = mem_read32(cpu, fs + kPane2[slot] + i * FS_PANE_STRIDE);
            if (p >= 0x80000000u && p < 0x81800000u)
                g_fs_panes[n++] = (FsPane){p, (u8)slot, 2, (u8)i};
        }
    }
    g_fs_pane_count = n;
}

// A plate's tint: the black and white of J2DPicture::setBlackWhite, written as the game writes them (the alpha bytes, which
// the game fades, are left alone). Crimson, a hue that reads on the green plates and on the gold Second Quest ones.
static void fs_tint(CPUState* cpu, u32 pane, u8 br, u8 bg, u8 bb, u8 wr, u8 wg, u8 wb) {
    mem_write8(cpu, pane + 0x108u, br);  // mColorBlack
    mem_write8(cpu, pane + 0x109u, bg);
    mem_write8(cpu, pane + 0x10Au, bb);
    mem_write8(cpu, pane + 0x104u, wr);  // mColorWhite
    mem_write8(cpu, pane + 0x105u, wg);
    mem_write8(cpu, pane + 0x106u, wb);
}

void bluewake_fs_pane(CPUState* cpu, u32 pane) {
    const unsigned mask = atomic_load(&g_rando_slots);
    if (mask == 0u)
        return;
    for (unsigned k = 0; k < g_fs_pane_count; ++k) {
        const FsPane* f = &g_fs_panes[k];
        if (f->pane != pane || (mask & (1u << f->slot)) == 0u)
            continue;
        if (f->group == 0 && f->index >= 2 && f->index <= 6)
            fs_tint(cpu, pane, 0x6E, 0x1E, 0x28, 0xC0, 0x3C, 0x4C);
        else if (f->group == 0 && f->index >= 7 && f->index <= 11)
            fs_tint(cpu, pane, 0xDC, 0x64, 0x78, 0xFF, 0x8C, 0xA0);
        else if (f->group == 1 && f->index >= 3 && f->index <= 11)
            fs_tint(cpu, pane, 0x5A, 0x14, 0x28, 0xA0, 0x28, 0x40);
        else if (f->group == 1 && f->index >= 12 && f->index <= 20)
            fs_tint(cpu, pane, 0xC8, 0x3C, 0x50, 0xF0, 0x6E, 0x82);
        return;
    }
}

// The file select's pane tree is hidden while the randomizer's own screens (rando_flow.cpp) are up, which draw the file select's
// own panes in their own places over the game's board: the J2DScreen (dDlst_FileSel_c::Scr, at dFile_select_c + 8) holds a
// background picture and the 'ROOT' pane, and everything of the list, the title strip and the buttons is under ROOT. The panes
// are found through the J2DPane tree (JSUTree: the first child's link at +0xB0, each link's object at +0 and next link at +0xC).
static atomic_bool g_fs_cover_want;
static u32 g_fs_covered_root;
void bluewake_fs_cover(bool on) { atomic_store(&g_fs_cover_want, on); }

static u32 fs_find_root(CPUState* cpu, u32 fs) {
    const u32 scr = mem_read32(cpu, fs + 8u);
    if (scr < 0x80000000u || scr >= 0x81800000u)
        return 0u;
    unsigned guard = 0;
    for (u32 link = mem_read32(cpu, scr + 0xB0u); link >= 0x80000000u && link < 0x81800000u && guard < 16u; ++guard) {
        const u32 pane = mem_read32(cpu, link);
        if (pane >= 0x80000000u && pane < 0x81800000u && mem_read32(cpu, pane + 8u) == 0x524F4F54u /* 'ROOT' */)
            return pane;
        link = mem_read32(cpu, link + 0xCu);
    }
    return 0u;
}

static BluewakeLocation g_location;
static atomic_flag g_lock = ATOMIC_FLAG_INIT;

static void lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) {
    }
}
static void unlock(void) { atomic_flag_clear_explicit(&g_lock, memory_order_release); }

void bluewake_game_state_retrace(CPUState* cpu) {
    if (cpu == NULL || cpu->ram == NULL)
        return;
    bluewake_dev_retrace(cpu);
    BluewakeLocation now;
    memset(&now, 0, sizeof now);
    for (int i = 0; i < 8; ++i) {
        const char c = (char)mem_read8(cpu, CUR_STAGE + (u32)i);
        if (c == '\0')
            break;
        now.stage[i] = c >= 32 && c < 127 ? c : '?';
    }
    {
        const u32 node = mem_read32(cpu, SCENE_LIST);
        const u32 scene = node >= 0x80000000u ? mem_read32(cpu, node + 0x0Cu) : 0u;
        now.scene_proc = scene >= 0x80000000u ? (int)mem_read16(cpu, scene + 0x08u) : 0;
        now.fs_main = -1;
        if (now.scene_proc != BLUEWAKE_SCENE_NAME) {
            g_fs_pane_count = 0;  // no stale pane pointers outside the file select
            g_fs_covered_root = 0u;
        }
        if (now.scene_proc == BLUEWAKE_SCENE_NAME) {
            now.fs_main = (int)mem_read8(cpu, scene + SCN_MAIN_PROC);
            const u32 fs = mem_read32(cpu, scene + SCN_FS);
            if (fs >= 0x80000000u && fs < 0x81800000u) {
                fs_collect_panes(cpu, fs);  // the panes are made again with the scene: read them each retrace (105 words)
                // The two marker panes of a randomizer file are shown, as Second Quest files show theirs.
                // A slot that stops being a randomizer file (erased, or copied over) has its markers hidden again; the
                // game's own changeExtraColor shows them for a real Second Quest file.
                static unsigned s_forced = 0u;
                const unsigned rando = atomic_load(&g_rando_slots);
                const unsigned dropped = s_forced & ~rando;
                for (unsigned k = 0; k < g_fs_pane_count; ++k) {
                    if (g_fs_panes[k].group != 2)
                        continue;
                    const unsigned bit = 1u << g_fs_panes[k].slot;
                    if ((rando & bit) != 0u)
                        mem_write8(cpu, g_fs_panes[k].pane + 0xAAu, 1u);  // J2DPane::mVisible
                    else if ((dropped & bit) != 0u)
                        mem_write8(cpu, g_fs_panes[k].pane + 0xAAu, 0u);
                }
                s_forced = rando;
                {
                    const bool want = atomic_load(&g_fs_cover_want);
                    if (want || g_fs_covered_root != 0u) {
                        const u32 root = fs_find_root(cpu, fs);
                        if (root != 0u && want) {
                            if (g_fs_covered_root != root)
                                fprintf(stderr, "[fileselect] the list's panes are hidden (ROOT at 0x%08X)\n", root);
                            mem_write8(cpu, root + 0xAAu, 0u);  // J2DPane::mVisible
                            g_fs_covered_root = root;
                        } else if (root != 0u && root == g_fs_covered_root) {
                            mem_write8(cpu, root + 0xAAu, 1u);
                            g_fs_covered_root = 0u;
                            fprintf(stderr, "[fileselect] the list's panes are shown again\n");
                        } else if (root == 0u) {
                            g_fs_covered_root = 0u;
                        }
                    }
                }
                now.fs_sub = (int)mem_read8(cpu, fs + FS_SUB);
                now.fs_slot = (int)mem_read8(cpu, fs + FS_SLOT);
                now.fs_end = (int)mem_read8(cpu, fs + FS_END);
                for (u32 i = 0; i < 3u; ++i)
                    now.fs_new[i] = mem_read8(cpu, fs + FS_NEW + i) != 0u;
            }
        }
    }
    {
        static int l_main = -2, l_sub = -2, l_slot = -2, l_end = -2, l_new = -2;
        const int nw = now.fs_new[0] | (now.fs_new[1] << 1) | (now.fs_new[2] << 2);
        if (now.fs_main >= 0 && (now.fs_main != l_main || now.fs_sub != l_sub || now.fs_slot != l_slot || now.fs_end != l_end || nw != l_new)) {
            fprintf(stderr, "[fileselect] main=%d sub=%d slot=%d end=%d new=%d\n", now.fs_main, now.fs_sub, now.fs_slot, now.fs_end, nw);
            l_main = now.fs_main; l_sub = now.fs_sub; l_slot = now.fs_slot; l_end = now.fs_end; l_new = nw;
        }
    }
    now.data_num = mem_read8(cpu, DATA_NUM);
    now.new_file = mem_read8(cpu, DATA_NUM + 1u);
    now.point = (s16)mem_read16(cpu, CUR_STAGE + 0x8u);
    now.room = (s8)mem_read8(cpu, CUR_STAGE + 0xAu);
    now.layer = (s8)mem_read8(cpu, CUR_STAGE + 0xBu);
    now.stay_room = (s8)mem_read8(cpu, STAY_ROOM);
    {
        // Sailing: the play status flag (daPyStts0_SHIP_RIDE_e) and the ship actor (play.mpPlayerPtr[2]).
        const u32 ship = mem_read32(cpu, 0x803CA75Cu);
        now.sailing = (mem_read32(cpu, 0x803CA8D0u) & 0x10000u) != 0u && ship >= 0x80000000u && ship < 0x81800000u;
        if (now.sailing) {
            for (int i = 0; i < 3; ++i)
                now.ship_pos[i] = read_float(cpu, ship + 0x1F8u + 4u * (u32)i);
            now.ship_angle_y = (s16)mem_read16(cpu, ship + 0x20Eu);
        }
        if (g_ship_fix.pending) {
            // The new scene's ship, with Link on it: put it (and with it Link) where the player's ship is.
            if (now.sailing && ++g_ship_fix.wait > 30u) {
                for (int i = 0; i < 3; ++i) {
                    write_float(cpu, ship + 0x1F8u + 4u * (u32)i, g_ship_fix.pos[i]); // current
                    write_float(cpu, ship + 0x1E4u + 4u * (u32)i, g_ship_fix.pos[i]); // old
                    write_float(cpu, ship + 0x1D0u + 4u * (u32)i, g_ship_fix.pos[i]); // home
                }
                mem_write16(cpu, ship + 0x206u, (u16)g_ship_fix.angle_y); // current.angle.y
                mem_write16(cpu, ship + 0x20Eu, (u16)g_ship_fix.angle_y); // shape_angle.y
                fprintf(stderr, "[teleport] the ship placed at %.0f %.0f %.0f\n", g_ship_fix.pos[0], g_ship_fix.pos[1], g_ship_fix.pos[2]);
                g_ship_fix.pending = false;
            } else if (++g_ship_fix.wait > 1800u) {
                g_ship_fix.pending = false; // never boarded
            }
        }
    }
    const u32 player = mem_read32(cpu, PLAYER);
    if (player >= 0x80000000u && player < 0x81800000u) {
        now.has_player = true;
        for (int i = 0; i < 3; ++i)
            now.pos[i] = read_float(cpu, player + ACTOR_POS + 4u * (u32)i);
        now.angle_y = (s16)mem_read16(cpu, player + ACTOR_SHAPE_ANGLE_Y);
        if (strcmp(now.stage, "sea") == 0) {
            now.sea_known = true;
            now.sea_x = now.pos[0];
            now.sea_z = now.pos[2];
        } else if (now.stage[0] != ' ') {
            // dMenu_Fmap_c::initialize: the stage's map info names the chart square (a nibble each, signed; 4 is the middle).
            const u32 info = mem_read32(cpu, STAGE_MAP_INFO);
            if (info >= 0x80000000u && info < 0x81800000u) {
                const u32 xz = mem_read8(cpu, info + 0x36u);
                int gx = (int)(xz & 0xFu), gz = (int)((xz >> 4) & 0xFu);
                gx = gx & 8 ? gx - 16 : gx;
                gz = gz & 8 ? gz - 16 : gz;
                gx = gx == 4 ? 0 : gx;
                gz = gz == 4 ? 0 : gz;
                now.sea_known = true;
                now.sea_x = 100000.0f * (float)gx;
                now.sea_z = 100000.0f * (float)gz;
            }
        }
    }
    {
        // daPy_lk_c::mAcch (dBgS_LinkAcch at +0x46C), its m_flags at +0x28: GROUND_HIT (0x20) when he stands on ground.
        static char ground_stage[9];
        static BluewakeLocation ground;
        if (strncmp(ground_stage, now.stage, 8) != 0) {
            memcpy(ground_stage, now.stage, 8);
            ground.has_ground = false;
        }
        if (now.has_player && (mem_read32(cpu, player + 0x46Cu + 0x28u) & 0x20u) != 0u &&
            mem_read8(cpu, EVENT_MODE) == 0u) {
            ground.has_ground = true;
            memcpy(ground.ground_pos, now.pos, sizeof now.pos);
            ground.ground_room = now.stay_room;
            ground.ground_angle_y = now.angle_y;
        }
        now.has_ground = ground.has_ground;
        memcpy(now.ground_pos, ground.ground_pos, sizeof now.ground_pos);
        now.ground_room = ground.ground_room;
        now.ground_angle_y = ground.ground_angle_y;
    }
    // The session log (logs\session-*.log): a line when the stage or the room
    // Link stands in changes, and when an event (a cutscene, a talk, a door)
    // starts and ends, for reading a log against what happened.
    {
        static char last_stage[9];
        static int last_room = -2, event_id = -1;
        static unsigned long long event_start, event_reported;
        static bool event_running; // an event's id can be -1 (one not from the stage's list), so the id cannot say this
        static unsigned long long retraces;
        ++retraces;
        const int stay = (s8)mem_read8(cpu, STAY_ROOM);
        bluewake_jabun_guard_retrace(cpu, now.stage, stay, retraces); // BR-0018 (jabun_guard.c)
        if (now.stage[0] != '\0' && (strcmp(now.stage, last_stage) != 0 || stay != last_room)) {
            fprintf(stderr, "[scene] stage=%s room=%d point=%d layer=%d%s\n", now.stage, stay, now.point, now.layer,
                    strcmp(now.stage, last_stage) != 0 ? " (stage change)" : "");
            memcpy(last_stage, now.stage, sizeof last_stage);
            last_room = stay;
        }
        // The main BGM (JAIZelBasic::mMainBgmNum, the audio interface at r13 - 0x69D0 = 0x803F7710): a line when it changes, with the audio
        // scene (field 0x224: 0x12 is the sea and its islands) and the stage, so a log says which music a stage got (the scenarios expect it).
        {
            static u32 last_bgm = 0xFFFFFFFEu;
            const u32 interface = mem_read32(cpu, 0x803F7710u);
            if (interface >= 0x80000000u && interface < 0x81800000u) {
                const u32 bgm = mem_read32(cpu, interface + 0x78u);
                if (bgm != last_bgm) {
                    fprintf(stderr, "[music] main BGM 0x%08X sub 0x%08X audio scene %u (set %u, scene lock %u) stage=%s room=%d\n", bgm,
                            mem_read32(cpu, interface + 0x74u), (unsigned)mem_read8(cpu, interface + 0x224u),
                            (unsigned)mem_read8(cpu, interface + 0x220u), (unsigned)mem_read8(cpu, interface + 0x22Cu), now.stage, stay);
                    last_bgm = bgm;
                }
            }
        }
        {   // JAIZelBasic::field_0x022c: set by setScene, cleared by load1stDynamicWave; while it is set a second setScene is ignored.
            static unsigned last_lock = 0xFFFFu;
            const u32 interface = mem_read32(cpu, 0x803F7710u);
            if (interface >= 0x80000000u && interface < 0x81800000u) {
                const unsigned lock = mem_read8(cpu, interface + 0x22Cu);
                if (lock != last_lock) {
                    fprintf(stderr, "[music] scene lock %u (audio scene %u, set %u) stage=%s room=%d\n", lock,
                            (unsigned)mem_read8(cpu, interface + 0x224u), (unsigned)mem_read8(cpu, interface + 0x220u), now.stage, stay);
                    last_lock = lock;
                }
            }
        }
        const bool running = mem_read8(cpu, EVENT_MODE) != 0u;
        const int id = (s16)mem_read16(cpu, EVENT_ID);
        if (running && !event_running) {
            event_running = true;
            event_id = id;
            event_start = event_reported = retraces;
            fprintf(stderr, "[event] start id=%d stage=%s room=%d\n", id, now.stage, stay);
        } else if (running && event_running && retraces >= event_reported + 1800u) {
            // A cutscene that never ends is a softlock: say so in the player's log every 30 s (BR-0018).
            event_reported = retraces;
            fprintf(stderr, "[event] id=%d still running after %llu s stage=%s room=%d\n", event_id,
                    (unsigned long long)((retraces - event_start) / 60u), now.stage, stay);
        } else if (!running && event_running) {
            event_running = false;
            fprintf(stderr, "[event] end id=%d after %.1f s\n", event_id, (double)(retraces - event_start) / 60.0);
            event_id = -1;
        }
    }
    const u32 view = mem_read32(cpu, VIEW);
    if (view >= 0x80000000u && view < 0x81800000u) {
        now.has_view = true;
        for (u32 i = 0; i < 16u; ++i)
            now.proj_view[i] = read_float(cpu, view + VIEW_PROJ_VIEW + 4u * i);
    }
    lock();
    g_location = now;
    Teleport t = g_teleport;
    g_teleport.pending = false;
    unlock();
    static u64 teleport_clock, last_teleport_fired; // this runs once a retrace
    const u64 retraces = ++teleport_clock;
    const bool fired_lately = last_teleport_fired != 0u && retraces - last_teleport_fired < 180u;
    if (t.pending && (fired_lately || mem_read8(cpu, NEXT_STAGE + 0xCu) != 0u)) {
        // One already under way (its fade, its load, the first moments there): a second one on top of it rewrote the
        // next stage mid-change and left the new room's loading waiting forever (a double click on the room list's
        // arrow). Dropped, not kept.
        fprintf(stderr, "[teleport] ignored: a scene change is already under way\n");
    } else if (t.pending && now.has_player && mem_read8(cpu, EVENT_MODE) == 0u) {
        last_teleport_fired = retraces;
        // Where Link appears: the restart record, read by dStage_playerInit
        // when the start point is -1 (d_stage.cpp:1449-1452).
        mem_write8(cpu, RESTART + 0x0u, (u8)(s8)t.room);
        mem_write16(cpu, RESTART + 0x16u, (u16)(s16)t.angle_y);
        for (int i = 0; i < 3; ++i)
            write_float(cpu, RESTART + 0x18u + 4u * (u32)i, t.pos[i]);
        // The restart's parameters become Link's own (dStage_playerInit, point -1): their low 6 bits are his start room
        // (daPy_lk_c::getStartRoomNo), packed as setParamData does: room | spawn type << 12 (0: standing) | event << 24
        // (0xFF: none). Written as 0 they made him room 0's: in a dungeon the target room was never his, did not load,
        // he fell through the void, and the void-out's restart (room 0, the target's position) never finished loading
        // (user 2026-10-17 16:39, Dragon Roost Cavern room 3; scenario anchor_teleport_dungeon).
        mem_write32(cpu, RESTART + 0x24u, ((u32)(t.room & 0x3F)) | 0xFF000000u);
        for (u32 i = 0; i < 8u; ++i)
            mem_write8(cpu, NEXT_STAGE + i, (u8)t.stage[i]);
        mem_write16(cpu, NEXT_STAGE + 0x8u, (u16)(s16)(t.ship ? 100 : -1)); // point 100 at sea: Link on the ship
        mem_write8(cpu, NEXT_STAGE + 0xAu, (u8)(s8)t.room);
        mem_write8(cpu, NEXT_STAGE + 0xBu, (u8)(s8)-1);
        mem_write8(cpu, NEXT_STAGE + 0xDu, 0u); // the plain fade
        mem_write8(cpu, NEXT_STAGE + 0xCu, 1u); // go
        if (t.ship) {
            g_ship_fix.pending = true;
            g_ship_fix.wait = 0u;
            memcpy(g_ship_fix.pos, t.ship_pos, sizeof t.ship_pos);
            g_ship_fix.angle_y = t.ship_angle_y;
        }
        fprintf(stderr, "[teleport] to %.8s room %d (%.0f, %.0f, %.0f)\n", t.stage, t.room, t.pos[0], t.pos[1], t.pos[2]);
    } else if (t.pending) {
        lock();
        if (!g_teleport.pending)
            g_teleport = t; // not now: keep it for a safe point
        unlock();
    }
}

void bluewake_request_teleport(const char* stage, int room, const float pos[3], int angle_y) {
    Teleport t;
    memset(&t, 0, sizeof t);
    t.pending = true;
    for (int i = 0; i < 8 && stage[i] != '\0'; ++i)
        t.stage[i] = stage[i];
    t.room = room;
    memcpy(t.pos, pos, sizeof t.pos);
    t.angle_y = angle_y;
    lock();
    g_teleport = t;
    unlock();
}

void bluewake_request_teleport_ship(const char* stage, int room, const float pos[3], int angle_y, const float ship_pos[3], int ship_angle_y) {
    Teleport t;
    memset(&t, 0, sizeof t);
    t.pending = true;
    for (int i = 0; i < 8 && stage[i] != '\0'; ++i)
        t.stage[i] = stage[i];
    t.room = room;
    memcpy(t.pos, pos, sizeof t.pos);
    t.angle_y = angle_y;
    t.ship = true;
    memcpy(t.ship_pos, ship_pos, sizeof t.ship_pos);
    t.ship_angle_y = ship_angle_y;
    lock();
    g_teleport = t;
    unlock();
}

BluewakeLocation bluewake_game_location(void) {
    lock();
    const BluewakeLocation copy = g_location;
    unlock();
    return copy;
}
