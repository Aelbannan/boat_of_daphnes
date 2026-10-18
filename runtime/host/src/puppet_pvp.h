// Other online players you can bump into, target and (when the room allows) fight: the puppets (puppet.h) as the
// game's collision system and attention know them (docs/fork/plan-anchor-interactivity.md, 2-3).
//
// Each drawn puppet gets a body: a cylinder collider made as a copy of Link's own (his mCyl, so every virtual table and
// default is the game's), put into the play scene's dCcS every frame before its checks run. Link is pushed out of the
// body (CoHit), and Link's own attacks (sword, arrows, bombs, items) hit it (TgHit): the host reads the hits back and
// the app sends PVP_HIT. A hit on this player is the same in reverse: for one frame an attack collider (as an enemy's)
// is put beside Link on the attacker's side, and the game does the rest: knockback, hurt animation and voice,
// invincibility frames, hearts, fairies, game over. A puppet also gets a "Dummy" actor with an attention point, so that
// Z targets it.
//
// The placing is native code at the start of dScnPly_Execute (hook 51), before the checks of the draw that follows; the
// calls into the game that make the objects go through the puppets' draw fiber (puppet.c).
#ifndef BLUEWAKE_PUPPET_PVP_H
#define BLUEWAKE_PUPPET_PVP_H

#include "core/cpu.h"
#include "guest_coro.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUEWAKE_PVP_HOOK_EXECUTE 51u // dScnPly_Execute, its first instruction (r3 the scene)

enum { BLUEWAKE_PVP_OFF = 0, BLUEWAKE_PVP_NO_DAMAGE = 1, BLUEWAKE_PVP_ON = 2 };

typedef struct BluewakePvpHit {
    int target;         // the player hit
    int damage;         // the attack's power in the game's own units (Link's sword is 1 or 2)
    uint32_t type;      // the game's attack type bits (AT_TYPE_*)
    float pos[3];       // where I was
    float direction[2]; // from me to the target (x, z), unit length
} BluewakePvpHit;

// The app, once a frame: whether puppets are solid and Z-targetable, the PvP mode and the damage scale (percent, 100
// normal) for hits taken.
void bluewake_pvp_set_rules(bool collision, int mode, int scale_percent);
// Whether my attacks may hit that player (the room's PvP, and a team's friendly-fire setting).
void bluewake_pvp_set_target(int id, bool hit_ok);
void bluewake_pvp_remove(int id);
void bluewake_pvp_clear(void);
// My attacks that hit a puppet since the last call (false: none). Sent as PVP_HIT.
bool bluewake_pvp_take_hit(BluewakePvpHit* hit);
// A hit on me by that player (from PVP_HIT); damage the attacker's power, applied with the mode and scale set above
// unless the game is in a state that takes no hits (an event, a menu, a load, the first seconds of a scene).
void bluewake_pvp_incoming(int from, int damage, uint32_t type, const float from_pos[3]);

// puppet.c: a puppet was drawn this frame at this place (its feet).
void bluewake_pvp_note(int id, const float pos[3]);
// puppet.c, the draw hook (every frame, before the fiber): the scene (its process id), dMenu_flag's result; reads the
// hits my attacks made on the bodies last frame.
void bluewake_pvp_draw(CPUState* cpu, uint32_t scene_id, uint32_t menu_flag);
// Whether the draw fiber has calls to make (bodies, actors to make or give back).
bool bluewake_pvp_wants_fiber(void);
void bluewake_pvp_fiber(GuestCoro* co, CPUState* cpu);
// puppet.c, the delete hook and its fiber: the scene goes. `has_block` says memory is still held.
bool bluewake_pvp_has_block(void);
void bluewake_pvp_scene_end(CPUState* cpu);
void bluewake_pvp_fiber_delete(GuestCoro* co, CPUState* cpu);
// Hook 51.
u32 bluewake_pvp_execute_hook(CPUState* cpu);
// Testing: the same from any site the game runs once a frame before the checks (BLUEWAKE_PVP_TEST_SITE in game_options.c).
void bluewake_pvp_test_execute(CPUState* cpu);

#ifdef __cplusplus
}
#endif

#endif
