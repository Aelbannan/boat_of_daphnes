// Picking up other players (docs/fork/plan-anchor-interactivity.md): Link lifts a nearby player's puppet like a pot, with
// the game's own carry and throw, and the lifted player is held at his hands.
//
// The lifting side. A puppet's "Dummy" actor (puppet_pvp.c) is given the attention of a carryable object (fopAc_Attn_ACTION_CARRY,
// the barrel's distance entry) while the room allows carrying, so the game offers A "Lift" beside it and runs its own lift,
// hold, put-down and throw (procGrabReady .. procGrabThrow, which move the actor to Link's hands and give it a throw speed).
// The host only watches the actor: the game's carry status turning on is the lift (a CARRY_START to that player), the hands'
// place while held is sent as CARRY_POS, and the status turning off is the end: put down, or a throw whose arc the host flies
// (the speed Link gave the actor) and sends as CARRY_POS until it lands (CARRY_END).
//
// The lifted side. A CARRY_START from the player carrying me, when I may be carried (the room allows it, no event, no scene
// change, on my feet), makes my Link follow the position the CARRY_POS messages give, with no input (the pad is cleared, R is
// held down so that the game's own crouch action stays), in Link's crouch animation (daPy_lk_c::procCrouch_init, called in the
// draw fiber); the pose stream shows that to every other game. B ends the hold at once (CARRY_END breakfree). A scene or room
// change, an event, a menu, a missing CARRY_POS for 3 seconds, or the other side's CARRY_END ends it too.
//
// Threads: the app's thread talks through the functions marked (app); the rest runs on the game thread (hook 51, the draw hook).
#ifndef BLUEWAKE_PUPPET_CARRY_H
#define BLUEWAKE_PUPPET_CARRY_H

#include "core/cpu.h"
#include "guest_coro.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BLUEWAKE_CARRY_EV_START = 1, // to peer: I have lifted you
    BLUEWAKE_CARRY_EV_POS = 2,   // to peer: where you are held / flying
    BLUEWAKE_CARRY_EV_END = 3,   // to peer: the hold is over (reason)
};
enum {
    BLUEWAKE_CARRY_PHASE_HELD = 0,
    BLUEWAKE_CARRY_PHASE_THROWN = 1,
};
enum {
    BLUEWAKE_CARRY_REASON_PUT = 0,
    BLUEWAKE_CARRY_REASON_THROWN,
    BLUEWAKE_CARRY_REASON_REFUSED,
    BLUEWAKE_CARRY_REASON_BUSY,
    BLUEWAKE_CARRY_REASON_BREAKFREE,
    BLUEWAKE_CARRY_REASON_AREA,
    BLUEWAKE_CARRY_REASON_EVENT,
    BLUEWAKE_CARRY_REASON_TIMEOUT,
    BLUEWAKE_CARRY_REASON_LEFT,
    BLUEWAKE_CARRY_REASON_COUNT,
};

typedef struct BluewakeCarryEvent {
    int kind;
    int peer;
    float pos[3];
    int angle; // the game's s16 facing
    int phase;
    int reason;
    int hurt;
} BluewakeCarryEvent;

const char* bluewake_carry_reason_name(int reason);
int bluewake_carry_reason_from_name(const char* name);

// (app) The room's setting. Off: no puppet is offered for lifting, and a CARRY_START is refused.
void bluewake_carry_set_room(bool allowed);
// (app) A CARRY_START / CARRY_POS / CARRY_END from a player, or a player who left or changed area.
void bluewake_carry_peer_start(int from, const float pos[3], int angle);
void bluewake_carry_peer_pos(int from, const float pos[3], int angle, int phase);
void bluewake_carry_peer_end(int from, int reason, const float pos[3], int angle, bool hurt);
void bluewake_carry_peer_gone(int id, int reason);
// (app) Everything over (disconnected).
void bluewake_carry_reset(void);
// (app) The events for the other players, to send. False: none.
bool bluewake_carry_take_event(BluewakeCarryEvent* ev);
// (app) Where the puppet of a player I carry is shown (its feet), or false. Applied to the pose stream's base position.
bool bluewake_carry_puppet_pos(int id, float pos[3]);
// (app) The state, for the status line and the scenarios: 0 none, 1 I carry someone, 2 I am carried.
int bluewake_carry_state(int* peer);
// (app, taken once) A hit for the player thrown on me (PvP on) was due: the carrier's id.
bool bluewake_carry_take_hurt(int* from, float pos[3]);

// ---- hook 51 and puppet_pvp.c ----
// Once a frame at the start of the play scene's execute (after the pvp bodies): the carried side, the carrier's flights.
void bluewake_carry_execute(CPUState* cpu, uint32_t scene_id);
// Per target, from puppet_pvp.c's execute loop, with the target's Dummy actor (0: none yet):
// true when the host does not set the actor's place this frame (the game holds it, or a thrown player flies it).
bool bluewake_carry_target(CPUState* cpu, int id, uint32_t actor, bool here, const float pos[3]);
// The attention flags to add to the actor's (fopAc_Attn_ACTION_CARRY while the puppet may be lifted), and sets the carry distance.
uint32_t bluewake_carry_attention(CPUState* cpu, int id, uint32_t actor, bool here);
// Whether that player's body should not be put into the game's collision checks (held or flying).
bool bluewake_carry_no_body(int id);
// Whether carrying makes the pvp module keep its Dummy actors even with collision and PvP off.
bool bluewake_carry_wants_actors(void);
// The play scene ends: the holds end (area).
void bluewake_carry_scene_end(CPUState* cpu);
// The draw fiber: a guest call to make (my Link to crouch; the hold to let go).
bool bluewake_carry_wants_fiber(void);
void bluewake_carry_fiber(GuestCoro* co, CPUState* cpu);

#ifdef __cplusplus
}
#endif

#endif
