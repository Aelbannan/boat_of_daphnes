// A small snapshot of where the game is, taken every retrace on the game
// thread, for code on other threads (the online play panel, the menu).
#ifndef BLUEWAKE_GAME_STATE_H
#define BLUEWAKE_GAME_STATE_H

#include "core/cpu.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BluewakeLocation {
    char stage[9]; // the stage's name ("sea", "Adanmae", ...); "" before the game has one
    int room;
    int point;
    int layer;
    int stay_room;    // the room Link stands in (dStage_roomControl_c::mStayNo)
    bool has_player;  // Link is in the scene (pos and angle are his)
    float pos[3];
    int angle_y;      // s16, Link's facing
    // Where Link last stood on the ground in this stage (his BG check's GROUND_HIT, no event running): what a
    // teammate's teleport to him lands on, so it never arrives mid-jump or over a pit.
    bool sailing;           // Link rides the ship: ship_pos and ship_angle_y are the ship's
    float ship_pos[3];
    int ship_angle_y;
    // Where Link is on the Sea Chart (world x, z): his position at sea, else the centre of the chart square the stage's
    // map info puts its island in; sea_known is false when the stage has none.
    bool sea_known;
    float sea_x, sea_z;
    bool has_ground;
    float ground_pos[3];
    int ground_room;  // the room he stood in then (mStayNo)
    int ground_angle_y;
    bool has_view;    // the camera's projection times view (row-major 4x4), for markers
    float proj_view[16];
    int scene_proc;   // the running scene's process name (BLUEWAKE_SCENE_*), 0 when none is running
    int data_num;     // the save slot in play, 0 to 2 (dSv_info_c::mDataNum; valid once a file is started)
    int new_file;     // dSv_info_c::mNewFile: the file was just made
    // The file select scene (BLUEWAKE_SCENE_NAME), dScnName_c and its dFile_select_c; valid while scene_proc says so.
    int fs_main;      // dScnName_c::mMainProc: 4 the file select, 6/7 the name entry, 9 the game starting (-1: not the scene)
    int fs_sub;       // dFile_select_c's step: 0 the list of files, 1 its cursor moving, 2 and on the menu of one
    int fs_slot;      // the file under the cursor (selectNum), 0 to 2
    int fs_end;       // selectEnd: 0 running, 1 a file was chosen, 2 back out
    bool fs_new[3];   // dataNew: that file is empty
} BluewakeLocation;

// Scene process names (f_pc_name.h): the title screen, and the file select with its name entry (one scene).
#define BLUEWAKE_SCENE_PLAY 0x0007
#define BLUEWAKE_SCENE_TITLE 0x000A
#define BLUEWAKE_SCENE_NAME 0x000C
#define BLUEWAKE_SCENE_OPEN 0x000E

void bluewake_game_state_retrace(CPUState* cpu);
// The randomizer's files on the file select: bit n set for file n (0 to 2) makes its plate crimson, as Second Quest files
// are gold (dFile_select_c::changeExtraColor): the plate panes are retinted as they draw (bluewake_fs_pane, from the pane
// hook) and the two marker panes shown. Any thread.
void bluewake_fs_set_rando_slots(unsigned mask);
// A J2D pane about to draw (the pane hook, widescreen.c); the file select's plate panes of a randomizer file are recoloured.
void bluewake_fs_pane(CPUState* cpu, u32 pane);
// Hides (true) or shows the file select's list, title strip and buttons (the pane tree under J2DScreen's ROOT), while the
// randomizer's own screens draw the same panes themselves; the corner prompts and the 3D board stay.
void bluewake_fs_cover(bool on);
// The latest snapshot (consistent: copied under a lock).
BluewakeLocation bluewake_game_location(void);
// Online play's teleport: a scene change to that stage and room, Link placed at
// pos facing angle_y (the restart record, start point -1), at the next safe
// point (a stage in play, no event running).
void bluewake_request_teleport(const char* stage, int room, const float pos[3], int angle_y);
// To a player who is sailing: the sea sector's start on the ship (point 100), then the ship is moved to where theirs is (beside it),
// so the player arrives on the King of Red Lions and not in the water.
void bluewake_request_teleport_ship(const char* stage, int room, const float pos[3], int angle_y, const float ship_pos[3], int ship_angle_y);

#ifdef __cplusplus
}
#endif

#endif
