# Game code map

This page lists the places in the game that Boat of Daphnes intercepts, reads or writes, and says what the port
does at each one. It is meant for people who want to add a feature, port a patch, or check a claim in another
document here.

The game is *The Legend of Zelda: The Wind Waker*, GameCube USA, `GZLE01` revision 0. Names come from the
[zeldaret/tww](https://github.com/zeldaret/tww) decompilation: its `config/GZLE01/symbols.txt` and
`config/GZLE01/splits.txt` for functions and data in `main.dol`, and the per-module copies under
`config/GZLE01/rels/<module>/` for the RELs. Source paths are paths in that repository, linked to its `main`
branch.

## How to read the tables

- **Address** is a `main.dol` address (`0x8017BF90`) or a module and an offset, written `d_a_ship.rel+0xBA00`.
  A REL offset is an offset *in the REL file*, which is how `mods/betterww/options.txt` writes them. The
  decomp's symbol files use offsets inside the section instead, so the symbol column shows the function and the
  distance into it (`+0x1DC0`) after the conversion.
- **Decomp symbol** is the symbol that contains the address, with C++ names unmangled for reading. Where a row
  is a data field, the path is written from `g_dComIfG_gameInfo`.
- **Hook N** is the id of a native hook: a line `hook <where> <option> before|after <id>` in
  `mods/betterww/options.txt`. The translator puts a call to the host at that instruction, and the host's
  `native_hook` in `runtime/host/src/game_options.c` runs the code for that id. See
  [how-it-works.md](how-it-works.md).
- A `site` line changes an instruction (or its low 16 bits) and a `write` line sets a value at boot. They are
  switched by the option they belong to.
- `g_dComIfG_gameInfo` is at `0x803C4C08`. In the decomp (`include/d/d_com_inf_game.h`) its `save` member
  (`dSv_info_c`) is at +0x0000, `play` (`dComIfG_play_c`) at +0x12A0, `drawlist` at +0x5D1C and `mResControl`
  at +0x1BFC0. Several rows give an address inside it, for example `0x803C9D3C` for `play.mCurStage`
  (0x803C4C08 + 0x12A0 + 0x3E94).
- Offsets inside actors (`daPy_lk_c`, `fopAc_ac_c`, `daShip_c`) were checked against the `/* 0x... */`
  comments in the decomp's headers. Where a row says **uncertain**, it was not confirmed.

Everything here was matched against the decomp's symbol tables with a script that reads the REL headers; no
disc data is needed to check it. Only the `GZLE01` revision 0 layout is covered.

## Hook ids

| Id | Option | Where |
| --- | --- | --- |
| 1 | `swing_turn` | Rope swing |
| 2 | `invert_camera_x` | Camera pad update |
| 3-5 | `swift_sail` | Ship wind and braking |
| 6-11, 15-17 | `widescreen_culling`, `hud_edges` | Camera, HUD panes, minimap, scissors |
| 12 | `treasure_glow` | Salvage beam |
| 13 | `player_models` | Play scene loading |
| 14 | `intro_skip_start` | Opening scene |
| 18-26 | `hero_mode`, `no_low_hp_beep`, `fix_vanilla_bugs` | 18 is every `main.dol` site, told apart by address; 19-26 are REL sites |
| 30, 31 | `anchor_puppets` | Play scene draw and delete |
| 33, 35 | `gx_array_source` | GX array and texture setup |
| 34 | `link_voice_pitch` | Link's voice |
| 36, 40 | `native_search` | Actor search by id |
| 37, 38 | `anchor_switch_sync` | Switch set and clear |
| 39, 41, 42 | `runtime_fixes` | Disc check, console reset, idle loop |
| 50 | `rando_file_select` | File select |
| 51 | `anchor_pvp` | Play scene execute |
| 70-73 | `bunny_hood` | Item get, walking speed, Quest Status screen |

## Anchor co-op and puppets

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x80234F5C` | `dScnPly_Draw` +0x3C0 | [`src/d/d_s_play.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_s_play.cpp) | Hook 30 (`anchor_puppets`), after the loop that draws every process and before `dMenu_flag()` is tested. The camera has set this frame's view matrix and the draw lists are still open. The host reads Link's joint matrices here (to send them) and enters the other players' models (to draw them). The Bunny Hood draw and the autosave share this site. |
| `0x802350BC` | `dScnPly_Delete` | [`src/d/d_s_play.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_s_play.cpp) | Hook 31, first instruction: gives the puppets' heap (and the Bunny Hood's and PvP objects') back before the play scene goes. |
| `0x803CA754` | `g_dComIfG_gameInfo.play.mpPlayerPtr` | [`include/d/d_com_inf_game.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_com_inf_game.h) | Link's actor: joint matrices, equipment models, current action. Element 2 (0x803CA75C) is the ship actor. |
| `0x803CA924` | `g_dComIfG_gameInfo.drawlist` | [`include/d/d_drawlist.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_drawlist.h) | The draw lists the puppets enter: `mpOpaListP1` (+0xC), `mpXluList` (+0x20), `mp2DOpa` (+0x19C, used for map and chart arrows), `mpView` (+0x234, the camera view). |
| `0x803F6A78` | `dStage_roomControl_c::mStayNo` | [`src/d/d_stage.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_stage.cpp) | The room Link is in. A puppet is drawn only when its player is in the same stage and room. |
| `0x803E4AB4` | `g_env_light` | [`src/d/d_kankyo.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_kankyo.cpp) | Environment light: lighting a puppet's materials with the scene's tint. |
| `0x803EDA58` | `j3dSys` | [`src/JSystem/J3DGraphBase/J3DSys.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphBase/J3DSys.cpp) | Current J3D model and draw buffers while a puppet is entered. |
| (field) | `daPy_lk_c` | [`include/d/actor/d_a_player_main.h`](https://github.com/zeldaret/tww/blob/main/include/d/actor/d_a_player_main.h) | Offsets read from Link's actor on the sending side and the model pointers the puppet code copies the look of: `mpCLModel` 0x32C, `mpHandsModel` 0x954, `mpEquippedSwordModel` 0x958, `mpEquippedShieldModel` 0x980, `mpEquipItemModel` 0x2E98, `mCurProc` 0x31D8 (the action, which also picks a voice and a sound), `mEquipItem` 0x3560, `mNoResetFlg0` 0x29C and `mNoResetFlg1` 0x2A0 (heavy boots, casual clothes). All offsets match the decomp header. |
| `0x80016BB8` | `mDoExt_J3DModel__create` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Called to make each puppet's own `J3DModel` from the resident Link archive's body model (a second model, not the player actor). |
| `0x80011CC0` | `mDoExt_createSolidHeapFromGameToCurrent` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Called to open the puppets' solid heap (the heap is adjusted and destroyed with the next three). |
| `0x80011D0C` | `mDoExt_adjustSolidHeap` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Shrinks the puppet heap to what was used. |
| `0x80011D48` | `mDoExt_destroySolidHeap` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Frees the puppet heap when the scene ends. |
| `0x80011DDC` | `mDoExt_restoreCurrentHeap` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Restores the game's current heap after the puppet code made objects in its own. |
| `0x80011BE4` | `mDoExt_createSolidHeapToCurrent` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Variant that makes a heap under a given parent, for equipment models. |
| `0x800118C0` | `mDoExt_getZeldaHeap` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | The game's main heap, used as a parent for those heaps. |
| `0x802B0C38` | `operator new` | [`src/JSystem/JKernel/JKRHeap.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JKernel/JKRHeap.cpp) | Operator new, called to allocate guest objects (puppet state, PvP bodies). |
| `0x802B0C60` | `operator new` | [`src/JSystem/JKernel/JKRHeap.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JKernel/JKRHeap.cpp) | Operator new with alignment. |
| `0x802FB94C` | `J3DModelLoaderDataBase::loadBinaryDisplayList` | [`src/JSystem/J3DGraphLoader/J3DModelLoader.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphLoader/J3DModelLoader.cpp) | Loads a custom body model (a validated `cl.bdl` from another player's shared Link.arc) the way the resource loader does. |
| `0x802EE8C0` | `J3DModel::calc` | [`src/JSystem/J3DGraphAnimator/J3DModel.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphAnimator/J3DModel.cpp) | Calculates the matrices of an equipment model, after the joints' calc callbacks are cleared so the game's own callbacks do not run on the copy. |
| `0x802EE67C` | `J3DModel::calcWeightEnvelopeMtx` | [`src/JSystem/J3DGraphAnimator/J3DModel.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphAnimator/J3DModel.cpp) | Called for each puppet model after the host has written the interpolated joint matrices: computes the envelope matrices of skinned meshes. |
| `0x802EE28C` | `J3DModel::unlock` | [`src/JSystem/J3DGraphAnimator/J3DModel.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphAnimator/J3DModel.cpp) | Called before a puppet's draw passes, as `daPy_lk_c::draw` does before its own. |
| `0x802F58D8` | `J3DJoint::entryIn` | [`src/JSystem/J3DGraphAnimator/J3DJoint.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphAnimator/J3DJoint.cpp) | Called for the eye, eyebrow and root joints in the puppet's draw passes, as the player's draw does. |
| `0x802ECCC4` | `J3DDrawBuffer::entryImm` | [`src/JSystem/J3DGraphBase/J3DDrawBuffer.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphBase/J3DDrawBuffer.cpp) | Queues Link's own static state packets (the four at 0x803E46A4-0x803E46F8) around the puppet's draw passes. |
| `0x802ECAF0` | `J3DDrawBuffer::entryZSort` | [`src/JSystem/J3DGraphBase/J3DDrawBuffer.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphBase/J3DDrawBuffer.cpp) | Z-sorted entry into the translucent list, used for the ship's sail cloth and the hookshot chain. |
| `0x802DB494` | `J3DMatPacket::draw` | [`src/JSystem/J3DGraphBase/J3DPacket.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphBase/J3DPacket.cpp) | Observed so the renderer knows whose puppet a material draw belongs to (texture variants per player). |
| `0x8000F974` | `mDoExt_modelEntryDL` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Queues a model's display lists for drawing: the puppet's body, hands, sword, shield and hair. |
| `0x80196EB4` | `dKy_tevstr_init` | [`src/d/d_kankyo.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_kankyo.cpp) | Initialises a lighting struct for a puppet model. |
| `0x80193028` | `dScnKy_env_light_c::settingTevStruct` | [`src/d/d_kankyo.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_kankyo.cpp) | Fills that struct from the scene light at the puppet's position. |
| `0x80193A34` | `dScnKy_env_light_c::setLightTevColorType` | [`src/d/d_kankyo.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_kankyo.cpp) | Applies the light colour to the model. |
| `0x8006D990` | `setToonTex` | [`src/d/d_resorce.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_resorce.cpp) | Sets the toon texture on a model, as the game does for Link. |
| `0x8006F208` | `dRes_control_c::getRes` | [`src/d/d_resorce.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_resorce.cpp) | Looks up a resource by archive and index (equipment models in the Link archive and others). |
| `0x8006F298` | `dRes_control_c::getRes` | [`src/d/d_resorce.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_resorce.cpp) | Same, by archive and resource name. |
| `0x800A4564` | `dBgS_ObjGndChk_Func` | [`src/d/d_bg_s_func.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_bg_s_func.cpp) | Ground height below a point, for a puppet's shadow. |
| `0x80085170` | `dDlst_shadowControl_c::setReal2` | [`src/d/d_drawlist.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_drawlist.cpp) | Real shadow of a puppet's model. |
| `0x8008520C` | `dDlst_shadowControl_c::addReal` | [`src/d/d_drawlist.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_drawlist.cpp) | Adds the puppet model to that shadow. |
| `0x80085274` | `dDlst_shadowControl_c::setSimple` | [`src/d/d_drawlist.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_drawlist.cpp) | Simple (blob) shadow. |
| `0x802A86A8` | `JAIZelBasic::getLinkVoiceVowel` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | Gets the voice vowel for a puppet's cry. |
| `0x802A8748` | `JAIZelBasic::linkVoiceStart` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | Starts Link's voice for a puppet when it begins an action (a table maps `mCurProc` to the voice id). |
| `0x800F0D10` | `mDoAud_seStart` | [`include/m_Do/m_Do_audio.h`](https://github.com/zeldaret/tww/blob/main/include/m_Do/m_Do_audio.h) | Plays a sound effect for a puppet (roll crash, cut swish, damage). An inline helper in the decomp header; the address is its out-of-line copy. |
| `0x80015328` | `mDoExt_3DlineMat1_c::init` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Sets up a rope strip for the hookshot chain and grappling-hook rope of another player. |
| `0x80015764` | `mDoExt_3DlineMat1_c::update` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Updates that strip. |
| `0x80016518` | `mDoExt_3DlineMatSortPacket::setMat` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Adds it to the sorted translucent list. |
| `0x802F40C0` | `J3DSkinDeform::__ct` | [`src/JSystem/J3DGraphAnimator/J3DCluster.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphAnimator/J3DCluster.cpp) | Creates a skin-deform object for a puppet with a skinned model. |
| `0x802EE4BC` | `J3DModel::setSkinDeform` | [`src/JSystem/J3DGraphAnimator/J3DModel.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphAnimator/J3DModel.cpp) | Attaches it to the model. |
| `0x802F4CD8` | `J3DSkinDeform::deform` | [`src/JSystem/J3DGraphAnimator/J3DCluster.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphAnimator/J3DCluster.cpp) | Deforms the skin after the pose is written. |
| `0x800241C0` | `fopAcM_SearchByID` | [`src/f_op/f_op_actor_mng.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_actor_mng.cpp) | Looks up an actor by process id (also answered natively by the host: see Quality of life, performance). |
| (field) | `daShip_c::mpBodyAnm` | [`include/d/actor/d_a_ship.h`](https://github.com/zeldaret/tww/blob/main/include/d/actor/d_a_ship.h) | Ship fields read for the ship's body and head models (`mpBodyAnm` 0x298) and the sail cloth (`daGrid_c` at ship +0x414, its packet at +0x2A0). The sender transmits the sail's 85 points; the receiver draws a copy of the packet. |
| `0x803CA8D0` | `g_dComIfG_gameInfo.play.mPlayerStatus` | [`include/d/d_com_inf_game.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_com_inf_game.h) | Player status word `mPlayerStatus[0][0]` (gameInfo.play +0x4A28): bit 0x10000 `daPyStts0_SHIP_RIDE_e` tells that Link is on the ship. |
| `0x803C35A4` | `dMap_c::mPoint` | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | Minimap point list. A puppet in the same stage and room is drawn as an arrow on the minimap in its colour, placed the way the game places Link's own. |
| `0x803C3984` | `dMap_c::mCursor` | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | Minimap cursor object, copied for each remote player's arrow and pushed into the 2D opaque list while the map draws. |
| `0x803F6A8A` | `dMap_c::mDrawPointCntPlayer` | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | Count of player points on the minimap (`mDrawPointCntPlayer`). |
| `0x803F6AD8` | `dMap_c::mDispPosLeftUpX` | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | Minimap top-left position (`mDispPosLeftUpX`), used to place the arrows. |
| `0x803F6AF8` | `dMap_c::mNowCenterX` | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | Minimap centre and scale (`mNowCenterX` and the fields after it). |
| `0x803F6B18` | `dMap_c::mNowRoomInfoP` | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | Minimap room info pointer (`mNowRoomInfoP`). |
| `0x803C4C16` | `g_dComIfG_gameInfo.save.mSavedata.mPlayer.mPlayerStatusA.mSelectEquip` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Equipped sword and shield slot bytes in the save (`dSv_player_status_a_c::mSelectEquip`), read to choose the puppet's gear. |
| (field) | `dSv_player_collect_c::mCollect` (uncertain) | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | puppet.c reads the Hero's Charm "worn" bit at `0x803C4C08 + 0xB2 + 4` = 0x803C4CBE. In the decomp's in-memory layout `mCollect` starts at +0xB4 (+0xB2 is the memory-card layout, `dSv_player_c_PACKED`), so `mCollect[4]` is at 0x803C4CC0, which is the byte the Bunny Hood code uses. **Uncertain: the two code paths read different bytes, and which one is intended was not confirmed.** |

## Anchor player interaction (PvP)

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x80234FD0` | `dScnPly_Execute` | [`src/d/d_s_play.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_s_play.cpp) | Hook 51 (`anchor_pvp`), first instruction of `dScnPly_Execute` (r3 the play scene). Puts the puppets' body colliders and, for one frame, the attack collider of a hit taken into the collision lists. That is after the previous draw cleared them and before the checks the next draw starts with. |
| `0x803C6EAC` | `g_dComIfG_gameInfo.play.mCcS` | [`include/d/d_com_inf_game.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_com_inf_game.h) | The game's collision system, `dCcS`: pointer arrays for attack (0x0), target (0x400), collide (0x1000) and object (0x1400) colliders and their counts at 0x2800-0x280C. The host adds its colliders to these lists. |
| (field) | `daPy_lk_c::mCyl` | [`include/d/actor/d_a_player_main.h`](https://github.com/zeldaret/tww/blob/main/include/d/actor/d_a_player_main.h) | Link's own status (`mStts` at +0x3FE8) and body cylinder (`mCyl` at +0x4024, size 0x130). Each puppet body is a byte copy of Link's cylinder, so every vtable and default is the game's. Link is pushed out of the body and his own attacks hit it. |
| `0x80024598` | `fopAcM_create` | [`src/f_op/f_op_actor_mng.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_actor_mng.cpp) | Creates a "Dummy" actor beside each puppet so the Z-target (attention) system has something to lock on to. |
| `0x800244B8` | `fopAcM_delete` | [`src/f_op/f_op_actor_mng.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_actor_mng.cpp) | Deletes those actors when a puppet goes. |
| `0x802B0D28` | `operator delete` | [`src/JSystem/JKernel/JKRHeap.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JKernel/JKRHeap.cpp) | Operator delete for the host-allocated bodies. |
| (field) | `fopAc_ac_c` | [`include/f_op/f_op_actor.h`](https://github.com/zeldaret/tww/blob/main/include/f_op/f_op_actor.h) | `home` 0x1D0, `old` 0x1E4, `current` 0x1F8, `speed` 0x220, `eye` 0x260, attention position 0x274 and flags 0x280, set on the Dummy actor so Z targets it. |
| `0x803C4C0A` | `g_dComIfG_gameInfo.save.mSavedata.mPlayer.mPlayerStatusA.mLife` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Link's hearts in quarter hearts, read to decide whether a hit lands (PvP mode "On" subtracts them through the game's own damage path). |
| `0x803C9EA2` | `g_dComIfG_gameInfo.play.mEvtCtrl.mMode` | [`include/d/d_event.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_event.h) | Event mode (`mEvtCtrl.mMode`, play +0x3F38 then +0xC2). Hits are not applied while an event runs. |
| `0x803C9D48` | `g_dComIfG_gameInfo.play.mNextStage` | [`include/d/d_stage.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_stage.h) | Next-stage record; its enable byte (+0xC) blocks hits during a scene change. |

## Sea Chart (other players on the chart)

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x80392594` | `__vt__12dDlst_FMAP_c` | [`src/d/d_menu_fmap.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_menu_fmap.cpp) | Vtable of `dDlst_FMAP_c`. The host makes a copy of the chart's draw object and puts it at the end of the 2D list so it draws arrows over the chart. The game's own pane tree is never changed. |
| (field) | `dMenu_Fmap_c` | [`include/d/d_menu_fmap.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_menu_fmap.h) | `fmapDl` 0x1C, `fmapSv` 0x2878 (zoom, current square), `mAreaPane` 0x2DCC (the whole-sea area), `mLnk1Pane` 0x2E04 (Link's arrow on the whole sea), `mLnk2Pane` 0x2EE4 (his arrow inside a square), `mClbPane` 0x30A4, `mMainProcIdx` 0x5113. Each remote arrow is a copy of the pane `lnk1` or `lnk2`, placed with the same maths as Link's own and tinted with the player's colour. All offsets match the decomp header. |
| `0x8039D680` | `__vt__7J2DPane` | [`src/JSystem/J2DGraph/J2DPane.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J2DGraph/J2DPane.cpp) | Vtable of `J2DPane`; the copies are made as plain panes in a copy of the parent chain. |
| `0x8039D6B8` | `__vt__9J2DScreen` | [`src/JSystem/J2DGraph/J2DScreen.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J2DGraph/J2DScreen.cpp) | Vtable of `J2DScreen`. |
| (field) | `stage_map_info_class::mOceanXZ` | [`include/d/d_stage.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_stage.h) | Read through the stage data (play.mStageData +0x14 gives `mpMapInfo`; the byte is at +0x36). Its two nibbles name the Sea Chart square of an island or dungeon stage (signed, with 4 meaning the middle), so a player ashore appears at the centre of that square. The decomp's `dMenu_Fmap_c::initialize` uses it the same way (`src/d/d_stage.cpp` lines 1531 and 1540). |
| `0x803CA754` | `g_dComIfG_gameInfo.play.mpPlayerPtr` | [`include/d/d_com_inf_game.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_com_inf_game.h) | At sea, the player's `current.pos` x and z are the chart position (the "sea" stage). Ashore the square centre is used instead. |

## Teleport to a teammate

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x803C9D3C` | `g_dComIfG_gameInfo.play.mCurStage` | [`include/d/d_stage.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_stage.h) | Current stage record (`mCurStage`: name[8], start point s16, room s8, layer s8). Read to say where Link is. |
| `0x803C9D48` | `g_dComIfG_gameInfo.play.mNextStage` | [`include/d/d_stage.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_stage.h) | Next-stage record (`mNextStage`: same layout, then `mEnable` at +0xC and `mWipe` at +0xD). A teleport writes the destination stage, room, start point and sets the enable byte to 1, which makes the game change scene exactly as an exit does. Start point -1 selects the restart record; point 100 on the "sea" stage puts Link on the ship. |
| `0x803C5D30` | `g_dComIfG_gameInfo.save.mRestart` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | The restart record (`dSv_info_c::mRestart`, info +0x1128): room (+0), angle (+0x16), position (+0x18) and parameter (+0x24, whose low six bits are Link's start room). `dStage_playerInit` reads it when the start point is -1, so the teleport writes the target there. |
| `0x80041AF4` | `dStage_playerInit` | [`src/d/d_stage.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_stage.cpp) | The function that reads the start point (see the `point == -1` branch around line 1450 of the decomp source). Also a hook site of Fix vanilla bugs. |
| `0x803F6A78` | `dStage_roomControl_c::mStayNo` | [`src/d/d_stage.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_stage.cpp) | Room Link is in; also the room reported to the other player. |
| (field) | `fopAc_ac_c` | [`include/f_op/f_op_actor.h`](https://github.com/zeldaret/tww/blob/main/include/f_op/f_op_actor.h) | After a teleport onto the ship the host writes the ship actor's position (+0x1F8 current, +0x1E4 old, +0x1D0 home) and facing (+0x206 current angle, +0x20E shape angle) so the arriving player appears beside the sender's ship. |
| (field) | `daPy_lk_c::mAcch` | [`include/d/actor/d_a_player_main.h`](https://github.com/zeldaret/tww/blob/main/include/d/actor/d_a_player_main.h) | The ground-check object `dBgS_LinkAcch` (at +0x46C), whose flags at +0x28 hold the "ground hit" bit (0x20). The host remembers the last place Link stood on the ground, and a teammate teleports there, not to a mid-air position. |
| `0x803C9EA2` | `g_dComIfG_gameInfo.play.mEvtCtrl.mMode` | [`include/d/d_event.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_event.h) | A teleport is deferred while an event runs (mode non-zero). |

## Item, flag and team sync

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| (field) | `dSv_player_status_a_c::mMaxLife` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Max life (two bytes), merged as a maximum. |
| (field) | `dSv_player_status_a_c::mSelectEquip` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Selected sword, shield, bracelet (`mSelectEquip`); 0xFF means none and is never allowed to overwrite a real item. |
| (field) | `dSv_player_status_a_c` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Wallet size and max magic. |
| (field) | `dSv_player_c::mPlayerItem` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Item slots (`dSv_player_item_c`); filled when empty or upgraded (Picto Box to Deluxe, Bow to Fire and Ice, then Light Arrows). Bottle slots (14-17) are never synced. |
| (field) | `dSv_player_c::mGetItem` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Item-get bits (`mGetItem`), merged by OR. |
| (field) | `dSv_player_c::mItemMax` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Ammo maxima (`mItemMax`) and bag-get bits (`mGetBagItem`). |
| (field) | `dSv_player_c::mCollect` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Collection (swords, shields, songs, Triforce shards, pearls) and the charts / sea map, merged by OR. |
| (field) | `dSv_save_c::mMemory` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | The 16 stage memories (`dSv_memory_c`: chests, switches, items, visited rooms, map, compass, big key, boss). Offset 0x20 of each is the small-key count, which is sent as signed gains and uses instead. The live copy of the stage in play (`dSv_info_c::mMemory`, info +0x778) is merged too. |
| (field) | `dSv_save_c::mOcean` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Ocean flags (`dSv_ocean_c`) and event flags (`dSv_event_c`). Event bytes 0x79-0xFF are registers (counters, scores) and are not synced because OR would corrupt them. |
| `0x803C9DA0` | `g_dComIfG_gameInfo.play.mStageData` | [`include/d/d_stage.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_stage.h) | Stage data pointer `mpStagInfo` (play.mStageData +0x48), whose byte at +9 gives the save table of the stage in play, used to know which memory is live. |
| `0x803C9EA2` | `g_dComIfG_gameInfo.play.mEvtCtrl.mMode` | [`include/d/d_event.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_event.h) | Merges happen only when no event is running and a stage is loaded. |

## Live switch sync

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x8005DB24` | `dSv_info_c::onSwitch` | [`src/d/d_save.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_save.cpp) | Hook 37 (`anchor_switch_sync`), entry of `dSv_info_c::onSwitch(no, room)`. Switches this game sets are sent to teammates. Switches a teammate set are written without calling the function, so there is no echo. |
| `0x8005DCEC` | `dSv_info_c::offSwitch` | [`src/d/d_save.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_save.cpp) | Hook 38, entry of `offSwitch`. Cleared switches 0-127 (the stage memory's) stay the local player's own; cleared dungeon and zone switches are shared. |
| (field) | `dSv_info_c` | [`include/d/d_save.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_save.h) | Where an incoming switch is written: the stage memory's switch bytes (info +0x778, switch bits 0x00-0x7F), the dungeon visit's (`mDan`, +0x79C, bits 0x80-0xBF) and the zone's (`mZone[32]` at +0x7A8, 0x4C bytes each, `mRoomNo` at +0 and the switch bits at +2). |
| `0x803C9D3C` | `g_dComIfG_gameInfo.play.mCurStage` | [`include/d/d_stage.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_stage.h) | Current stage name, sent with the switch so it applies only if the receiver is in the same stage. |

## Console reset, disc and idle loop

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x80312704` | `DVDCheckDisk` | [`src/dolphin/dvd/dvd.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/dvd/dvd.c) | Hook 39 (`runtime_fixes`): `DVDCheckDisk` returns TRUE (disc present) and returns at once. Without it, the three-button reset (B + X + Start) saw "no disc" and took the hard reset path into the boot ROM. |
| `0x80306750` | `OSResetSystem` | [`src/dolphin/os/OSReset.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/os/OSReset.c) | Hook 41: `OSResetSystem`. A console reset (a failed heap check at a scene change, a memory-card error that says "go to IPL", the logo scene's reset) reboots a GameCube. The host asks the app to start the game again and parks the guest in `PPCHalt` (0x8030150C) until then. |
| `0x80307EF4` | `SelectThread` +0x14C | [`src/dolphin/os/OSThread.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/os/OSThread.c) | Hook 42: the idle loop of `SelectThread` (`while (RunQueueBits == 0);`). With no thread to run and interrupts on, only an interrupt can end the loop, so the host moves the clock straight to the next device deadline (at most 8,000,000 cycles, about one retrace) instead of spinning to it. |
| `0x8030150C` | `PPCHalt` | [`src/dolphin/base/PPCArch.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/base/PPCArch.c) | Address the host returns to in order to park the guest after a reset request. |

## Widescreen and HUD

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x8017BF90` | `view_setup` +0xE0 | [`src/d/d_camera.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_camera.cpp) | Hook 6 (`widescreen_culling`), before the call that sets up the clipper: the view aspect in f2 is widened to the window, so actors at the sides are not culled. |
| `0x802D02B8` | `J2DPane::draw` +0x240 | [`src/JSystem/J2DGraph/J2DPane.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J2DGraph/J2DPane.cpp) | Hook 7 (`hud_edges`), after the matrix concat in `J2DPane::draw`: the HUD's panes are moved to the window's left or right edge (r27 the pane, r30 its parent). |
| `0x802D0358` | `J2DPane::draw` +0x2E0 | [`src/JSystem/J2DGraph/J2DPane.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J2DGraph/J2DPane.cpp) | Hook 15: where `J2DPane::draw` has computed the draw bounds, the clip rectangle follows a moved pane (otherwise the swim timer's water was cut away). |
| `0x801FF068` | `dMeter_clockMultiMove` +0x128 | [`src/d/d_meter.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_meter.cpp) | Hook 8: the sun and moon swirl (`dMeter_clockMultiMove`). |
| `0x80201D8C` | `dMeter_swimTekariScroll` +0x60 | [`src/d/d_meter.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_meter.cpp) | Hook 9: the swim gauge's shine. |
| `0x801FC28C` | `dMeter_rupyInit` +0xFC | [`src/d/d_meter.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_meter.cpp) | Hook 10: the rupee sparkle. |
| `0x8004A474` | `dMap_c::calcScissor` +0xD0 | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | Hook 11, before the `blr` of `dMap_c::calcScissor`: the scissor of the minimap's points reaches across the window, since the map sits left of the 4:3 frame. |
| `0x801DFA04` | `dDlst_MENU_CLOTH_c::draw` +0x44 | [`src/d/d_menu_window.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_menu_window.cpp) | Hook 16, before `C_MTXPerspective` in the pause menu cloth: takes back the renderer's aspect widening so the cloth (a 3D mesh under its own perspective) covers the window. |
| `0x802CD4BC` | `J2DGrafContext::setScissor` +0x17C | [`src/JSystem/J2DGraph/J2DGrafContext.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J2DGraph/J2DGrafContext.cpp) | Hook 17: the limit of the scissor's right edge (1024.0) is lifted so panes moved to the edge of a 32:9 window are not cut. |
| `0x801DF684` | `dDlst_MENU_CAPTURE_c::draw` | [`src/d/d_menu_window.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_menu_window.cpp) | Entry of `dDlst_MENU_CAPTURE_c::draw`; marked in the GX stream so the pause backdrop captures the whole window. |
| `0x8018E1CC` | `dDlst_Gameover_CAPTURE_c::draw` | [`src/d/d_gameover.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_gameover.cpp) | Entry of `dDlst_Gameover_CAPTURE_c::draw`; the same for the game-over backdrop. |
| `0x80237568` | `dDlst_2DSCP_c::draw` | [`src/d/d_scope.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_scope.cpp) | Entry of `dDlst_2DSCP_c::draw` (the telescope view): black side margins. |
| `0x8022B320` | `dJle_Pb_c::draw` | [`src/d/d_picture_box.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_picture_box.cpp) | Entry of `dJle_Pb_c::draw` (the Pictobox view): black side margins. |
| `0x803F6898` | `mDoGph_gInf_c::mFader` | [`src/m_Do/m_Do_graphic.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_graphic.cpp) | The screen fader (`mDoGph_gInf_c::mFader`), widened to the window each retrace. |
| `0x803F68A0` | `mDoGph_gInf_c::mFrameBufferTex` | [`src/m_Do/m_Do_graphic.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_graphic.cpp) | Scene copy texture for the pause backdrop (`mFrameBufferTex`); its address is told to the renderer each retrace. |
| `0x803F68A4` | `mDoGph_gInf_c::mZbufferTex` | [`src/m_Do/m_Do_graphic.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_graphic.cpp) | Depth copy texture (`mZbufferTex`). |
| `0x803E68DC` | `g_meter_mapHIO` | [`src/d/d_meter.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_meter.cpp) | Minimap HUD parameters (`g_meter_mapHIO`). |
| `0x803F6AA2` | `dMap_c::mIconFreePosX` | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | Minimap icon position (`mIconFreePosX`). |
| `0x803F6AE4` | `dMap_c::mScissorOrigX` | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | Minimap scissor origin and size (`mScissorOrigX`, `mScissorWidth` at 0x803F6AE8), widened or moved to the window edges. |

## Renderer support

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x80322568` | `GXSetArray` | [`src/dolphin/gx/GXAttr.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/gx/GXAttr.c) | Hook 33 (`gx_array_source`), entry of `GXSetArray` (r3 attribute, r4 pointer). The CP register keeps only the physical address, which loses the 0xC0000000 bits of a REL's linked data (the pirate flag's texcoord array); the hook tells the renderer the full pointer. |
| `0x803248AC` | `GXInitTexObj` | [`src/dolphin/gx/GXTexture.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/gx/GXTexture.c) | Hook 35, entry of `GXInitTexObj` (r4 the image pointer): the same for textures that live in a module's data (the flags' pictures). |
| `0x80324B20` | `GXInitTexObjCI` | [`src/dolphin/gx/GXTexture.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/gx/GXTexture.c) | Hook 35, `GXInitTexObjCI`. |
| `0x80324F3C` | `GXInitTlutObj` | [`src/dolphin/gx/GXTexture.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/gx/GXTexture.c) | Hook 35, `GXInitTlutObj`. |
| `0x80322B20` | `GXSetDrawSync` +0x90 | [`src/dolphin/gx/GXMisc.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/gx/GXMisc.c) | GXSetDrawSync: the host publishes the pixel-engine token when it flushes. |
| `0x80322BC8` | `GXSetDrawDone` +0x80 | [`src/dolphin/gx/GXMisc.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/gx/GXMisc.c) | GXSetDrawDone: the host publishes the finish interrupt. |
| `0x803FCB9C` | `@27` | [`src/dolphin/gx/GXTransform.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/gx/GXTransform.c) | Float literal 1.0 in `GXTransform.c`, read by `GXProject`. The option "Blur off" writes 0.0 here (the same value as a well-known Dolphin patch code); the host puts 1.0 back when the option is turned off. |

## Quality of life (Better Wind Waker options)

Each option below is a switch in `mods/betterww/options.txt`. The table lists every distinct address. "Kind" is `hook` (native code), `site` (an instruction or immediate the translator can switch) or `write` (a value written into guest memory at boot).

| Option | Address | Kind | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- | --- | --- |
| `instant_text` | `0x801E6A0C` | site | `dMesg_outwaitProc` +0x38 | [`src/d/d_mesg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_mesg.cpp) | Reads the held buttons instead of the just-pressed ones when a message waits (0x31 instead of 0x33 in the pad state), so holding B advances text. The message data is also patched in memory when it loads, so text draws at once. Site: lbz r0, 0x33(r3) in dMesg_outwaitProc |
| `` | `0x801E6BF0` | site | `dMesg_closewaitProc` +0x38 | [`src/d/d_mesg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_mesg.cpp) |  |
| `` | `0x80211EF8` | site | `dMsg_stopProc` +0x158 | [`src/d/d_msg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_msg.cpp) |  |
| `` | `0x8021211C` | site | `dMsg_selectProc` +0x80 | [`src/d/d_msg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_msg.cpp) |  |
| `` | `0x80212E14` | site | `dMsg_continueProc` +0x54 | [`src/d/d_msg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_msg.cpp) |  |
| `` | `0x80213718` | site | `dMsg_closewaitProc` +0xC8 | [`src/d/d_msg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_msg.cpp) |  |
| `` | `0x80213858` | site | `dMsg_finishProc` +0x28 | [`src/d/d_msg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_msg.cpp) |  |
| `swift_sail` | `d_a_ship.rel+0xBA00` | hook | `daShip_c::execute` +0x1DC0 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | Sail speed and wind: cruising speed 10 to 30, acceleration and braking changed by instruction edits and boot-time float writes. Hooks 3-5 turn the wind to blow from behind and set the brake steps. Site: b (after bl setShipSailState) |
| `` | `d_a_ship.rel+0x3A74` | hook | `daShip_c::setSelfMove` +0x13C | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | bl cLib_addCalc: braking with A |
| `` | `d_a_ship.rel+0x3BC8` | hook | `daShip_c::procWait` +0x34 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | bl cLib_addCalc: braking when idle |
| `` | `d_a_ship.rel+0x3A7C` | site | `daShip_c::setSelfMove` +0x144 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | lfs f1, 0x154(r31): cruising speed 10 -> 30 |
| `` | `d_a_ship.rel+0x2DD0` | site | `daShip_c::decrementShipSpeed` +0x18 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | lfs f3, 0xEC(r4): acceleration 0.1 -> 0.2 |
| `` | `d_a_ship.rel+0x2E18` | site | `daShip_c::firstDecrementShipSpeed` +0x28 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | lfs f3, 0x80(r31): 5 -> 10 |
| `` | `d_a_ship.rel+0x2E1C` | site | `daShip_c::firstDecrementShipSpeed` +0x2C | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | lfs f4, 0x88(r31): 1 -> 2 |
| `` | `d_a_ship.rel+0xDC28` | write | rodata constant in the same unit | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | acceleration step 0.015 |
| `` | `d_a_ship.rel+0xDBE8` | write | rodata constant in the same unit | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | sailing speed 55 |
| `` | `d_a_ship.rel+0xDBC0` | write | rodata constant in the same unit | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | initial speed 80 |
| `faster_movement` | `0x8035DB94` | write | `daPy_HIO_crouch_c0::m` +0x3C | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | Boot-time float writes into the player's tuning tables (`daPy_HIO_roll_c0::m`, `daPy_HIO_crouch_c0::m`): rolling speed and base speed. |
| `` | `0x8035D3D0` | write | `daPy_HIO_roll_c0::m` +0x18 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | rolling speed per walking speed, 6/17 |
| `` | `0x8035D3D4` | write | `daPy_HIO_roll_c0::m` +0x1C | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | rolling base speed |
| `faster_grapple` | `0x800EE0E4` | site | `new_himo2_move` +0x9F0 | [`src/d/actor/d_a_himo2.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_himo2.cpp) | Instruction and constant edits in the grappling hook rope's movement function (timers 40 to 20 and 20 to 10, a speed constant). Site: lfs f0, -0x5FD8(r2): read 0x803F9DAC |
| `` | `0x800EDB74` | site | `new_himo2_move` +0x480 | [`src/d/actor/d_a_himo2.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_himo2.cpp) | addi r0, r3, 40 -> 20 |
| `` | `0x800EDEA4` | site | `new_himo2_move` +0x7B0 | [`src/d/actor/d_a_himo2.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_himo2.cpp) | addi r0, r3, 20 -> 10 |
| `` | `0x800EEC40` | site | `new_himo2_move` +0x154C | [`src/d/actor/d_a_himo2.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_himo2.cpp) | lfs f3, -0x60BC(r2): read 0x803F9D28 |
| `` | `0x800EECA8` | site | `new_himo2_move` +0x15B4 | [`src/d/actor/d_a_himo2.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_himo2.cpp) | addi r5, r3, 1 -> 6 |
| `` | `0x803F9D60` | write | rodata constant in the same unit | [`src/d/actor/d_a_himo2.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_himo2.cpp) |  |
| `faster_block_push` | `0x8035DBB0` | write | `daPy_HIO_pushpull_c0::m` +0x8 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | Push and pull animation speed in the player's tuning table, and 12 frames instead of 20 for each of the 13 block types in the `daObjMovebox::Act_c::M_attr` table (26 u16 writes in the REL's data; only the first and last are listed). |
| `` | `0x8035DBB8` | write | `daPy_HIO_pushpull_c0::m` +0x10 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) |  |
| `` | `d_a_obj_movebox.rel+0x54B4` | write | `daObjMovebox::Act_c::M_attr` +0x4 | [`src/d/actor/d_a_obj_movebox.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_obj_movebox.cpp) |  |
| `` | `d_a_obj_movebox.rel+0x5C0A` | write | `daObjMovebox::Act_c::M_attr` +0x75A | [`src/d/actor/d_a_obj_movebox.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_obj_movebox.cpp) |  |
| `faster_animations` | `0x8035D738` | write | `daPy_HIO_wallCatch_c0::m` +0x1C | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | Float writes into the tuning tables for crawling, climbing and wall-catch animations, and the NPC chat camera zoom time (20 to 10 frames). |
| `` | `0x8035DB38` | write | `daPy_HIO_ladder_c0::m` +0x24 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) |  |
| `` | `0x8035DB18` | write | `daPy_HIO_ladder_c0::m` +0x4 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) |  |
| `` | `0x8035DB20` | write | `daPy_HIO_ladder_c0::m` +0xC | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) |  |
| `` | `0x8035D6AC` | write | `daPy_HIO_wall_c0::m` +0x14 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) |  |
| `` | `0x8016DA2C` | site | `dCamera_c::talktoCamera` +0x15C | [`src/d/d_camera.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_camera.cpp) | li r0, 20 -> 10 |
| `tingle_chests` | `d_a_agbsw0.rel+0x1CA4` | site | `daAgbsw0_c::ExeSubT` +0xF0 | [`src/d/actor/d_a_agbsw0.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_agbsw0.cpp) | Tingle Chests open to any bomb and show on the maps once the compass is owned: the Tingle Tuner checks are skipped and the minimap and dungeon map compare against a different value. Site: skip the Tingle Tuner checks |
| `` | `d_a_agbsw0.rel+0x1D0C` | site | `daAgbsw0_c::ExeSubT` +0x158 | [`src/d/actor/d_a_agbsw0.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_agbsw0.cpp) | any bomb, not only a Tingle Bomb |
| `` | `0x8004C68C` | site | `dMap_c::drawPointGc` +0x548 | [`src/d/d_map.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_map.cpp) | cmplwi r0, 15: minimap |
| `` | `0x801A9F8C` | site | `dMenu_Dmap_c::treasureSet` +0x6A0 | [`src/d/d_menu_dmap.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_menu_dmap.cpp) | cmpwi r23, 15: dungeon map |
| `unrestricted_boat` | `d_a_ship.rel+0x29EC` | site | `daShip_c::checkOutRange` +0xD4 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | King of Red Lions no longer turns the player back for story progress; four branches in `checkOutRange` and in the boarding check are made unconditional. Site: bne: before Dragon Roost |
| `` | `d_a_ship.rel+0x2A08` | site | `daShip_c::checkOutRange` +0xF0 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | bne: before Farore's Pearl |
| `` | `d_a_ship.rel+0x2A24` | site | `daShip_c::checkOutRange` +0x10C | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | beq: before the Master Sword |
| `` | `d_a_ship.rel+0xB2D8` | site | `daShip_c::execute` +0x1698 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | bne: boarding before Dragon Roost |
| `no_song_replays` | `0x8014ECE0` | site | `daPy_lk_c::procTactPlay` +0x140 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | Two conditional branches in the conducting state become no-ops, so Link does not conduct the song again. Site: bne: waiting for the text box |
| `` | `0x8014EF28` | site | `daPy_lk_c::procTactPlay` +0x388 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | bge: waiting for the animation |
| `swing_turn` | `0x80145648` | hook | `daPy_lk_c::procRopeSwing` +0x9F0 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | Hook 1: the stick turns Link while he swings on a rope. Site: lfs f0, -0x5BA8(r2) in procRopeSwing |
| `skip_intro_movie` | `0x80232C78` | site | `dScnOpen_c::execute` +0xB4 | [`src/d/d_s_open.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_s_open.cpp) | Two instructions of the opening scene are no-oped (a call and a branch) to skip the movie. Site: original 0x48000F6D: bl 0x80233BE4 |
| `` | `0x80232C88` | site | `dScnOpen_c::execute` +0xC4 | [`src/d/d_s_open.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_s_open.cpp) | original 0x4082000C: bne 0x80232C94 |
| `faster_ballad` | `d_a_ship.rel+0x7A10` | site | `daShip_c::procTactWarp` +0x298 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | Two instructions in the ship's warp state are no-oped: the wait for the boat to rise, and the warp music. Site: ble: KoRL's height |
| `` | `d_a_ship.rel+0x7680` | site | `daShip_c::procTactWarp_init` +0xB8 | [`src/d/actor/d_a_ship.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ship.cpp) | bl: the warp music |
| `invert_camera_x` | `0x80162488` | hook | `dCamera_c::updatePad` +0xE8 | [`src/d/d_camera.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_camera.cpp) | Hook 2, after the C-stick X axis is loaded in `dCamera_c::updatePad`: negates it. (A testing environment variable can reuse this site to stand in for hook 51.) Site: lfs f1, 0x10(r3) in dCamera_c::updatePad |
| `reveal_sea_chart` | `0x8005B2CC` | site | `dSv_player_map_c::init` +0x3C | [`src/d/d_save.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_save.cpp) | A new game's map init sets the "reveal all" argument to 1. Site: li r4, 0 -> 1 |
| `remove_music` | `0x802A33D0` | site | `JAIZelBasic::bgmStreamPlay` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | The four background-music start functions return at once (first instruction replaced with `blr`). Site: stwu r1, -0x10(r1) -> blr |
| `` | `0x802A34A4` | site | `JAIZelBasic::bgmStart` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | stwu r1, -0x20(r1) -> blr |
| `` | `0x802A47B8` | site | `JAIZelBasic::subBgmStart` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | stwu r1, -0x10(r1) -> blr |
| `` | `0x802A4EB8` | site | `JAIZelBasic::bgmNowBattle` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | stwu r1, -0x20(r1) -> blr |
| `invert_sea_compass_x` | `0x801FD37C` | site | `dMeter_compassGetOnProc` +0x278 | [`src/d/d_meter.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_meter.cpp) | The negation of the compass x in the meter code is a no-op. Site: neg r0, r0 -> nop |
| `hero_mode` | `0x801102D0` | hook | `daPy_lk_c::setDamagePoint` +0x34 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | Hook 18 (by address) multiplies damage taken; hearts become rupees, and Keese, Jalhalla's skulls and disappear smoke drop a green rupee instead of a heart. Site: lis r4 in daPy_lk_c::setDamagePoint: f1 (negative = damage) times the multiplier |
| `` | `0x800C7D4C` | site | `getItemNoByLife` +0x24 | [`src/d/d_item.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_item.cpp) | bnelr -> nop in getItemNoByLife: hearts become rupees even when hurt |
| `` | `0x8038B0C0` | write | `ki_item_d` | [`src/d/actor/d_a_disappear.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_disappear.cpp) | ki_item_d (Keese/disappear smoke drop table): heart -> green rupee |
| `` | `d_a_ki.rel+0x326C` | site | `ki_fail_move` +0x64 | [`src/d/actor/d_a_ki.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ki.cpp) | bgt 0x3278 -> b: ki_fail_move always takes the above-2-hearts path |
| `` | `d_a_bl.rel+0x50C4` | site | `action_normal_skull` +0x22C | [`src/d/actor/d_a_bl.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_bl.cpp) | li r4, 0 -> 1 in action_normal_skull (Jalhalla's skulls drop a green rupee) |
| `no_low_hp_beep` | `0x802A2FDC` | hook | `JAIZelBasic::processHeartGaugeSound` +0x88 | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | Hook 18: no beep for the one-heart alarm when the maximum is one heart, and no low-health animation unless two quarter hearts are missing. Site: li r4, 0xD1 in JAIZelBasic::processHeartGaugeSound |
| `` | `0x80111EE0` | hook | `daPy_lk_c::checkRestHPAnime` +0x8C | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | b in daPy_lk_c::checkRestHPAnime (the return-true branch) |
| `unrestricted_camera` | `0x80356D34` | write | `dCamParam_c::styles` +0x1D9C | [`src/d/d_cam_style.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_cam_style.cpp) | Writes the farthest zoom (700 to 2048) and highest tilt (30 to 88) into the manual camera style's parameters in `dCamParam_c::styles`. |
| `` | `0x80356D48` | write | `dCamParam_c::styles` +0x1DB0 | [`src/d/d_cam_style.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_cam_style.cpp) |  |
| `fix_vanilla_bugs` | `d_a_npc_ba1.rel+0x16DC` | site | `daNpc_Ba1_c::next_msgStatus` +0x8C | [`src/d/actor/d_a_npc_ba1.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_npc_ba1.cpp) | Hooks 18-26 and instruction edits that fix the listed bugs: Grandma's soup message, the hookshot crash on Helmaroc King's mask shards (null `r30`), Orca's sword counter, Beedle and Blue Chu Jelly, the Deluxe Picto Box equip, Morth deletion, light-arrow handling of Stalfos, Miniblins, Poes and Magtails, Phantom Ganon's trigger height, the island level-of-detail heap estimate, the missing start point panic, arrow hit-callback clearing, the sunrise jingle keeping the BGM, the Skull Hammer guarding, Ganondorf phase-3 arrow reflection, Helmaroc King's landing timer, and a store in `daBoko_c::procCarry`. Site: li r0, 2041 -> 2037: Grandma's soup message always exists |
| `` | `0x800F13A8` | hook | `daHookshot_rockLineCallback` +0x64 | [`src/d/actor/d_a_hookshot.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_hookshot.cpp) | lwz r0, 0x1C4(r30) in daHookshot_rockLineCallback: r30 null for Helmaroc's mask shards |
| `` | `d_a_npc_ji1.rel+0xC914` | site | `daNpc_Ji1_c::battleAction` +0x3C | [`src/d/actor/d_a_npc_ji1.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_npc_ji1.cpp) | Orca's counter: cmpwi r0, 0x38 |
| `` | `d_a_npc_ji1.rel+0xC918` | site | `daNpc_Ji1_c::battleAction` +0x40 | [`src/d/actor/d_a_npc_ji1.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_npc_ji1.cpp) | beq 0xC938 (Hero's Sword icon) |
| `` | `d_a_npc_ji1.rel+0xC91C` | site | `daNpc_Ji1_c::battleAction` +0x44 | [`src/d/actor/d_a_npc_ji1.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_npc_ji1.cpp) | cmpwi r0, 0xFF (no sword) |
| `` | `d_a_npc_ji1.rel+0xC920` | site | `daNpc_Ji1_c::battleAction` +0x48 | [`src/d/actor/d_a_npc_ji1.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_npc_ji1.cpp) | beq 0xC96C: no counter at all |
| `` | `d_a_npc_ji1.rel+0xC924` | site | `daNpc_Ji1_c::battleAction` +0x4C | [`src/d/actor/d_a_npc_ji1.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_npc_ji1.cpp) | b 0xC948 (Master Sword icon) |
| `` | `d_a_npc_bs1.rel+0x214C` | site | `daNpc_Bs1_c::getMsg` +0xDC | [`src/d/actor/d_a_npc_bs1.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_npc_bs1.cpp) | b 0x2170 -> 0x21DC: Beedle does not buy Blue Chu Jelly |
| `` | `0x800C3420` | hook | `item_func_camera2` +0x30 | [`src/d/d_item.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_item.cpp) | stb r0, 0x44(r3) in item_func_camera2: equip the Deluxe Picto Box |
| `` | `d_a_ks.rel+0x678` | site | `naraku_check` +0x2C | [`src/d/actor/d_a_ks.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_ks.cpp) | beq 0x6FC -> 0x6B0 in naraku_check: Morths with no floor are deleted |
| `` | `d_a_st.rel+0x85D0` | hook | `daSt_Execute` +0x1E4 | [`src/d/actor/d_a_st.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_st.cpp) | cmplwi r3, 0 after bl fopAcIt_Judge: Stalfos lower body dies to light arrows |
| `` | `d_a_pt.rel+0x4B48` | hook | `daPt_Create` +0x238 | [`src/d/actor/d_a_pt.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_pt.cpp) | addi r0, r29, 0x52C after bl Set(dCcD_Sph): Miniblin death switch for light arrows |
| `` | `d_a_pw.rel+0x8CC` | site | `Big_pow_down_check` +0x1C | [`src/d/actor/d_a_pw.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_pw.cpp) | ble 0x8D8 -> b: Big_pow_down_check tests the HP itself |
| `` | `d_a_pw.rel+0x904` | hook | `Big_pow_down_check` +0x54 | [`src/d/actor/d_a_pw.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_pw.cpp) | cmpwi r3, 0 after bl fopAcM_SearchByID: Poe dead if HP <= 0 or hit by light arrows |
| `` | `d_a_pw.rel+0x970` | hook | `Big_pow_down_check` +0xC0 | [`src/d/actor/d_a_pw.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_pw.cpp) | stb r0, 0x285(r31): an unkilled Poe's light-arrow counter is cleared |
| `` | `d_a_mt.rel+0x6004` | hook | `damage_check` +0x2BC | [`src/d/actor/d_a_mt.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_mt.cpp) | stw r3, 0x30(r1) after bl GetTgHitObj: Magtail head hit by light arrows respawns |
| `` | `d_a_fganon.rel+0x4F50` | hook | `start` +0xA8 | [`src/d/actor/d_a_fganon.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_fganon.cpp) | fmuls f1, f1, f1: Phantom Ganon's trigger checks Link's height |
| `` | `d_a_lod_bg.rel+0xDCC` | site | `daLodbg_c::execReadWait` +0x19C | [`src/d/actor/d_a_lod_bg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_lod_bg.cpp) | li r5, 0 -> 0xEF0: island LoD model heap estimate |
| `` | `d_a_lod_bg.rel+0x478` | site | `daLodbg_c::deleteModelData` +0x78 | [`src/d/actor/d_a_lod_bg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_lod_bg.cpp) | bl JKRHeap::free on a solid heap |
| `` | `d_a_lod_bg.rel+0x4D0` | site | `daLodbg_c::deleteModelData` +0xD0 | [`src/d/actor/d_a_lod_bg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_lod_bg.cpp) |  |
| `` | `0x80235340` | hook | `dScnPly_Delete` +0x284 | [`src/d/d_s_play.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_s_play.cpp) | mr r3, r30 in dScnPly_Delete: subBgmStop |
| `` | `0x80041AF4` | hook | `dStage_playerInit` | [`src/d/d_stage.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_stage.cpp) | stwu r1 at dStage_playerInit: a start point the room lacks starts Link at its first (was OSPanic) |
| `` | `0x800D6194` | hook | `daArrow_c::procMove` +0x674 | [`src/d/actor/d_a_arrow.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_arrow.cpp) | li r0, 2 in daArrow_c::procMove: clear the arrow's hit callback |
| `` | `0x802ABEF8` | site | `JAIZelBasic::processTime` +0x84 | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | li r6, 0 -> 1 in JAIZelBasic::processTime: sunrise jingle keeps the BGM |
| `` | `0x8010E288` | hook | `daPy_lk_c::checkNextActionFromButton` +0x730 | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | daPy_lk_c::checkNextActionFromButton: Skull Hammer can guard |
| `` | `0x8010E504` | hook | `daPy_lk_c::setShieldGuard` +0xBC | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | daPy_lk_c::setShieldGuard: same |
| `` | `0x800D4EBC` | hook | `daArrow_c::ShieldReflect` +0xFC | [`src/d/actor/d_a_arrow.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_arrow.cpp) | lfs f0, 0x1F8(r4) in daArrow_c::ShieldReflect: Ganondorf phase 3 only |
| `` | `d_a_bdk.rel+0x24D4` | hook | `landing` +0x78 | [`src/d/actor/d_a_bdk.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_bdk.cpp) | mr r3, r30 in landing(): Helmaroc King landing timer starts |
| `` | `d_a_bdk.rel+0x2AD0` | hook | `landing` +0x674 | [`src/d/actor/d_a_bdk.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_bdk.cpp) | addi r11, r1, 0xA0 at the end of landing(): timer, give up after 10 s |
| `` | `d_a_boko.rel+0x26A8` | site | `daBoko_c::procCarry` +0x3FC | [`src/d/actor/d_a_boko.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_boko.cpp) | stfs f0, 0x1FC(r25) -> nop in daBoko_c::procCarry |

## Quality of life (the port's own)

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x802356F8` | `phase_0` | [`src/d/d_s_play.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_s_play.cpp) | Hook 13 (`player_models`), after the wave-bank status call in `phase_0` of the play scene. While a player-model swap is pending the host calls `dRes_control_c::deleteRes`, `setRes` and `syncRes` for the "Link" archive and keeps r3 non-zero so the scene waits. The result is a model change without restarting. |
| `0x8006EF34` | `dRes_control_c::setRes` | [`src/d/d_resorce.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_resorce.cpp) | Called by that sequence: loads the Link archive again, from the DVD overlay. |
| `0x8006F01C` | `dRes_control_c::syncRes` | [`src/d/d_resorce.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_resorce.cpp) | Polls the load. |
| `0x8006F074` | `dRes_control_c::deleteRes` | [`src/d/d_resorce.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_resorce.cpp) | Unloads the Link archive. |
| `0x803E0BC8` | `dComIfG_inf_c::mResControl` | [`include/d/d_com_inf_game.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_com_inf_game.h) | The resource table (64 entries of 0x24 bytes); the host finds the "Link" entry and waits for its count to drop to one. |
| `d_a_salvage.rel+0x1570` | `daSalvage_c::calcAlpha` +0x100 | [`src/d/actor/d_a_salvage.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_salvage.cpp) | Hook 12 (`treasure_glow`), before the multiply that forms `near * 100` in `daSalvage_c::calcAlpha` (f30 is the near distance, r28 the salvage slot). The host sets f30 to 0 for the kinds of treasure chosen in the menu, so the beam stays lit close up. The far limit and all salvage logic are untouched. |
| `0x80232C80` | `dScnOpen_c::execute` | [`src/d/d_s_open.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_s_open.cpp) | Hook 14 (`intro_skip_start`), in `dScnOpen_c::execute` after the process runs and before its state is compared with 44: pressing Start skips the intro story. |
| `0x802A88FC` | `JAIZelBasic::linkVoiceStart` +0x1B4 | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | Hook 34 (`link_voice_pitch`), right after `linkVoiceStart` loads the pitch into f2. The host makes the sound start itself and calls `JAISound::setPitch` on it (0x80298864), so Link's voice follows the Cosmetics Editor's pitch. Puppets use their owner's pitch. |
| `0x802A6720` | `JAIZelBasic::seStart` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | The `seStart` the voice path calls, made by the host in a coroutine. |
| `0x802A6AFC` | `JAIZelBasic::seStart` +0x3DC | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | The voice path inside `seStart`, which ignores the pitch argument: the reason the pitch is set on the sound after it exists. |
| `0x80298864` | `JAISound::setPitch` | [`src/JSystem/JAudio/JAISound.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JAudio/JAISound.cpp) | Called to set the pitch of that sound. |
| `0x802904B4` | `JAIBasic::startSoundVec` | [`src/JSystem/JAudio/JAIBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JAudio/JAIBasic.cpp) | `JAIBasic::startSoundVec`, the actor sound call that creates the voice sound. |
| `0x800241C0` | `fopAcM_SearchByID` | [`src/f_op/f_op_actor_mng.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_actor_mng.cpp) | Hook 36 (`native_search`): `fopAcM_SearchByID(id, &actor)` is answered natively at its entry. In the translation it walks two queues through two calls a node, and `dAttention_c::calcWeight` calls it for every actor twice a frame (7-10 percent of the game thread in Windfall's crowd). |
| `0x80023FB4` | `fopAcIt_Judge` | [`src/f_op/f_op_actor_iter.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_actor_iter.cpp) | Hook 40: `fopAcIt_Judge(fpcSch_JudgeByID, &id)`, the inline one-argument search that `calcWeight` uses, answered by the same native code. |
| `0x800C2E08` | `execItemGet` | [`src/d/d_item.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_item.cpp) | Hook 70 (`bunny_hood`), in `execItemGet`: item 0x44 (an unused item number) sets the Bunny Hood's "owned" bit in the Hero's Charm's collect byte (`mCollect[4]` at 0x803C4CC0), so a vanilla save stays valid. |
| `0x8010E950` | `daPy_lk_c::checkNextMode` | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | Hook 71, in `daPy_lk_c::checkNextMode` after the maximum walking speed is chosen: multiplied by 1.5 while the hood is worn and Link is not locked on. |
| `0x801A55D0` | `dMenu_Collect_c::_move` | [`src/d/d_menu_collect.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_menu_collect.cpp) | Hook 72, entry of `dMenu_Collect_c::_move` (the Quest Status screen): holding A and pressing left or right swaps the Hero's Charm slot between the charm and the hood. |
| `0x801A5498` | `dMenu_Collect_c::_delete` | [`src/d/d_menu_collect.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_menu_collect.cpp) | Hook 73, entry of `dMenu_Collect_c::_delete`: puts the slot's real bits back. |
| `0x803842B0` | `dItem_data::item_resource` | [`src/d/d_item_data.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_item_data.cpp) | Item resource table (`dItem_data::item_resource`, 0x24 bytes a row). A row is written for item 0x44 so the hood has a model and an icon. |
| `0x803866B0` | `dItem_data::field_item_res` | [`src/d/d_item_data.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_item_data.cpp) | Field item resource table (0x1C bytes a row), same purpose. |
| `0x803882B0` | `dItem_data::item_info` | [`src/d/d_item_data.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_item_data.cpp) | Item info table (4 bytes a row), same purpose. |
| `0x803F6FF0` | `dMc_c` | [`src/d/d_menu_window.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_menu_window.cpp) | Pointer to the Quest Status screen object (`dMc_c`); the host reads `mItemTexBuffer` (+0x24CC) and the selected slot (+0x27ED) to swap the icon. |
| `0x803A4DF0` | `g_mDoCPd_cpadInfo` | [`src/m_Do/m_Do_controller_pad.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_controller_pad.cpp) | Pad state of the first controller (`g_mDoCPd_cpadInfo`): hold at +0x30, trigger at +0x32. Read for the A + left/right gesture. |
| `0x80011AB4` | `mDoExt_getArchiveHeap` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | The archive heap, where the hood's and the name replacement's texts and textures are allocated. |
| `0x802B0434` | `JKRHeap::alloc` | [`src/JSystem/JKernel/JKRHeap.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JKernel/JKRHeap.cpp) | Heap allocation call used for those buffers. |
| `0x8002B8A4` | `fopMsgM_messageSet` | [`src/f_op/f_op_msg_mng.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_msg_mng.cpp) | Called to show a test message when checking the text replacement. |
| `0x8002AEF4` | `fopMsgM_SearchByName` | [`src/f_op/f_op_msg_mng.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_msg_mng.cpp) | Finds the message process by name. |
| `0x8003BD50` | `fopMsgM_paneScaleXY` | [`src/f_op/f_op_msg_mng.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_msg_mng.cpp) | Scales a message pane, used to fit the hood's item-get text. |
| `0x802D3B78` | `J2DPicture::changeTexture` | [`src/JSystem/J2DGraph/J2DPicture.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/J2DGraph/J2DPicture.cpp) | Replaces a picture's texture (`J2DPicture::changeTexture`), used for the hood's icon. |
| `0x80026ADC` | `fopAcM_createItem` | [`src/f_op/f_op_actor_mng.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_actor_mng.cpp) | Creates an item actor (used by the hood's test and give paths). |
| `0x80393798` | `g_profile_MSG` | [`src/d/d_msg.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_msg.cpp) | Message process profile (`g_profile_MSG`). |
| `0x8005987C` | `dSv_player_item_record_c::resetTimer` | [`src/d/d_save.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_save.cpp) | Hook-free: the host's Forest Water experiment watches `dSv_player_item_record_c::resetTimer`, called from `procBottleSwing` (0x80153134), to make a fresh scoop last 30 minutes instead of 20. |
| `0x8005CB58` | `dSv_event_c::setEventReg` | [`src/d/d_save.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_save.cpp) | The same experiment watches `dSv_event_c::setEventReg` when called from `daPy_lk_c::execute` (0x801218F0) to keep the eight watered-tree bits. |
| `d_a_obj_ftree.rel .text+0x176C` | `daObjFtree::Act_c::action_waitM_main +0x30` | [`src/d/actor/d_a_obj_ftree.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_obj_ftree.cpp) | The Forest Water experiment also watches the watered-tree timer check in the Forest Haven tree actor, found by an offset in the REL rather than a fixed address. The offset is 0x30 into the function (which starts at 0x173C). That it sits just after a getTimer call is the host source description and was not checked against the instructions. |
| `0x803F6160` | `l_fopOvlpM_overlap` | [`src/f_op/f_op_overlap_mng.cpp`](https://github.com/zeldaret/tww/blob/main/src/f_op/f_op_overlap_mng.cpp) | The scene-change overlap request (`l_fopOvlpM_overlap[0]`): the host shortens the fade (6 frames each way instead of 26) and fast-forwards while the screen is black. |
| `0x803F6104` | `mDoGph_gInf_c::mFadeColor` | [`src/m_Do/m_Do_graphic.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_graphic.cpp) | Fade colour (`mFadeColor`), read to know whether the door fade is white or black. |
| `0x803F68AB` | `mDoGph_gInf_c::mFade` | [`src/m_Do/m_Do_graphic.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_graphic.cpp) | Fade flag, rate and speed (`mFade` 0x803F68AB, `mFadeRate` 0x803F68AC, `mFadeSpeed` 0x803F68B0). |
| `0x80041E84` | `dStage_cameraInit` | [`src/d/d_stage.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_stage.cpp) | Camera init in the stage loader; with the player init it is one of the places the quick-door code watches while placing Link in a new stage. |
| `0x803C9ED4` | `g_dComIfG_gameInfo.play.mEvtManager` | [`include/d/d_event_manager.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_event_manager.h) | Event manager list (`mEvtManager.mList`), 0x803C9EF8 and 0x803C9F00 fields follow; the quick-door code reads the door events' ids (DEFAULT_KNOB_DOOR_*). |
| `0x803C9EB8` | `g_dComIfG_gameInfo.play.mEvtCtrl.mEventId` | [`include/d/d_event.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_event.h) | Current event id (`mEvtCtrl.mEventId`, s16). |
| `0x803C5EA8` | `g_dComIfG_gameInfo.play` | [`include/d/d_com_inf_game.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_com_inf_game.h) | Start of `dComIfG_play_c` (info +0x12A0): "play" fields such as `mItemNowLife` (+0x4924) are addressed from here. |
| `0x803F7710` | `JAIZelBasic::zel_basic` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | The audio interface `zel_basic` (`JAIZelBasic`). The host logs the main BGM number (+0x78), sub BGM (+0x74), audio scene (+0x224) and scene lock (+0x22C) when they change. |

## Autosave and card

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x80055580` | `dComIfGs_exchangePlayerRecollectionData` | [`src/d/d_com_inf_game.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_com_inf_game.cpp) | Autosave step 1: `dComIfGs_exchangePlayerRecollectionData` (also run again at the end, to swap back). |
| `0x8005D988` | `dSv_info_c::putSave` | [`src/d/d_save.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_save.cpp) | Step 2: `dSv_info_c::putSave` copies the stage memory into the stage's slot. |
| `0x800548FC` | `dComIfGs_setGameStartStage` | [`src/d/d_com_inf_game.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_com_inf_game.cpp) | Step 3: `dComIfGs_setGameStartStage`. |
| `0x8005E780` | `dSv_info_c::memory_to_card` | [`src/d/d_save.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_save.cpp) | Step 4: `dSv_info_c::memory_to_card` writes all three files into the card image. |
| `0x8001A454` | `mDoMemCdRWm_SetCheckSumGameData` | [`src/m_Do/m_Do_MemCardRWmng.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_MemCardRWmng.cpp) | Step 5: `mDoMemCdRWm_SetCheckSumGameData`. |
| `0x800191C4` | `mDoMemCd_Ctrl_c::save` | [`src/m_Do/m_Do_MemCard.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_MemCard.cpp) | Step 6: `mDoMemCd_Ctrl_c::save` queues the write on the card thread. |
| `0x8001931C` | `mDoMemCd_Ctrl_c::SaveSync` | [`src/m_Do/m_Do_MemCard.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_MemCard.cpp) | Then `SaveSync` each frame until the thread is done. |
| `0x803B39A0` | `g_mDoMemCd_control` | [`src/m_Do/m_Do_MemCard.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_MemCard.cpp) | The card controller (`g_mDoMemCd_control`): the host checks the thread is idle before and after. |

## Randomizer support

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x8017FD6C` | `dFile_select_c::_move` | [`src/d/d_file_select.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_file_select.cpp) | Hook 50 (`rando_file_select`), top of `dFile_select_c::_move`, which runs every frame of the file select. The host plays the game's own sound that the app queued, and, after an in-place switch, asks the game to run `cCc_Init` again to rebuild the dynamic module controls. |
| `0x800227A0` | `cCc_Init` | [`src/c/c_dylink.cpp`](https://github.com/zeldaret/tww/blob/main/src/c/c_dylink.cpp) | Called by the host after an in-place switch: `cCc_Init` rebuilds the dynamic module table (the randomizer's code keeps its own larger table). |
| `0x803F69C0` | `DMC_initialized` | [`src/c/c_dylink.cpp`](https://github.com/zeldaret/tww/blob/main/src/c/c_dylink.cpp) | Flag that the dynamic-link controls are initialised (`DMC_initialized`), reset so they are built again. |
| `0x802A34A4` | `JAIZelBasic::bgmStart` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | File select and rando music: `JAIZelBasic::bgmStart` is called to start a sequence (the file select theme, the pirate theme). |
| `0x802A47B8` | `JAIZelBasic::subBgmStart` | [`src/JAZelAudio/JAIZelBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelBasic.cpp) | `JAIZelBasic::subBgmStart`, used for the chart-filled jingle. |
| `0x800073D8` | `mDoAud_setSceneName` | [`src/m_Do/m_Do_audio.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_audio.cpp) | `mDoAud_setSceneName`, used to set the audio scene (the pirate ship's) on the randomizer's screens. |
| `0x80007424` | `mDoAud_load1stDynamicWave` | [`src/m_Do/m_Do_audio.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_audio.cpp) | `mDoAud_load1stDynamicWave`. |
| `0x8039C5B8` | `JAIZelBasic::spot_dir_name` | [`src/JAZelAudio/JAIZelScene.cpp`](https://github.com/zeldaret/tww/blob/main/src/JAZelAudio/JAIZelScene.cpp) | `JAIZelBasic::spot_dir_name`, the audio scene table (index 83 is "PShip"). |
| `0x802B8008` | `JKRArchive::mount` | [`src/JSystem/JKernel/JKRArchivePub.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JKernel/JKRArchivePub.cpp) | `JKRArchive::mount`, used to mount the randomizer's title logo archive from the overlay. |
| `0x802B6854` | `JKRFileLoader::unmount` | [`src/JSystem/JKernel/JKRFileLoader.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JKernel/JKRFileLoader.cpp) | `JKRFileLoader::unmount`. |
| (field) | `dFile_select_c` (uncertain) | [`include/d/d_file_select.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_file_select.h) | File-select scene fields: `dFs_c` at +0x428, `mMainProc` +0x554; `dataNew[3]` +0x3914, `selectNum` +0x3922, the DataSelProc index +0x392B and `selectEnd` +0x392C; pane arrays for the three plates. The host reads them to know the state of the file select, tints randomizer files' plates and shows the Second Quest markers on them. The panes are found through the `J2DScreen` tree (first child link at +0xB0). **The offsets come from the host source; only a few were re-checked in the decomp header.** |
| (field) | ? | - | A seed's items reach the game through hooks after instructions that hold an item id (45 sites in the DOL and RELs) and 42 data bytes in item tables. The list is generated from the patched code by `scripts/rando/variant_sites.py` (`seed_sites.json`) and is not repeated here. |
| `d_a_obj_ajav.rel (sea, room 44)` | `daObjAjav::Act_c::check_all_wait (and check_end)` | [`src/d/actor/d_a_obj_ajav.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_obj_ajav.cpp) | Jabun guard: in the "sea" stage, room 44, `daObjAjav` (process 0x0088) can wait forever for its six rock parts to report "nothing to do" in a randomizer game, because the randomizer's copy of the REL is laid out differently. After five seconds stuck in the final wait the host sets the parts' step to the value the REL itself uses for "nothing to do". Fields: `mRockParts` +0x890 (6 x 0x98), exec step +0x80, draw step +0x8C, `M_status` +0xC22, `mActionIdx` +0xC24. Matches `check_all_wait` / `check_end` in the decomp source (`no_proc` at line 191, `check_all_wait` at 594). |

## Device boundary (operating system, disc, card)

| Address | Decomp symbol | Decomp source | What the port does |
| --- | --- | --- | --- |
| `0x8030F2B0` | `DVDConvertPathToEntrynum` | [`src/dolphin/dvd/dvdfs.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/dvd/dvdfs.c) | `DVDConvertPathToEntrynum`: recognised by the host's edge-intercept table so file lookups are served from the disc image and the mod overlay. |
| `0x8030F5A4` | `DVDFastOpen` | [`src/dolphin/dvd/dvdfs.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/dvd/dvdfs.c) | `DVDFastOpen`. |
| `0x8030F618` | `DVDOpen` | [`src/dolphin/dvd/dvdfs.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/dvd/dvdfs.c) | `DVDOpen`. |
| `0x8030FADC` | `DVDReadAsyncPrio` | [`src/dolphin/dvd/dvdfs.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/dvd/dvdfs.c) | `DVDReadAsyncPrio`. |
| `0x8030FBCC` | `DVDReadPrio` | [`src/dolphin/dvd/dvdfs.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/dvd/dvdfs.c) | `DVDReadPrio`. |
| `0x80017FD8` | `my_DVDConvertPathToEntrynum` | [`src/m_Do/m_Do_dvd_thread.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_dvd_thread.cpp) | `my_DVDConvertPathToEntrynum`, the archive path lookup. |
| `0x80240744` | `DynamicModuleControl::do_load` | [`src/DynamicLink.cpp`](https://github.com/zeldaret/tww/blob/main/src/DynamicLink.cpp) | `DynamicModuleControl::do_load`: a REL is materialised from the host's copy. |
| `0x803193AC` | `__DSP_boot_task` | [`src/dolphin/dsp/dsp_task.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/dsp/dsp_task.c) | `__DSP_boot_task`: the DSP task boot handshake. |
| `0x80308A9C` | `OSSleepThread` | [`src/dolphin/os/OSThread.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/os/OSThread.c) | `OSSleepThread`: observed so the GX draw-done return can publish its finish. |
| `0x8031A6E8` | `CARDFreeBlocks` | [`src/dolphin/card/CARDBios.c`](https://github.com/zeldaret/tww/blob/main/src/dolphin/card/CARDBios.c) | Memory-card calls the host answers from its card file: `CARDFreeBlocks`; the others follow in this row group: `CARDProbe` 0x8031D400, `CARDProbeEx` 0x8031D438, `CARDMount` 0x8031DC9C, `CARDMountAsync` 0x8031DAFC, `CARDUnmount` 0x8031DD80, `CARDCheck` 0x8031D2E0, `CARDCheckExAsync` 0x8031CD50, `CARDFormat` 0x8031E5C8, `CARDOpen` 0x8031E74C, `CARDClose` 0x8031E8C4, `CARDCreate` 0x8031EC68 (`Async` 0x8031EA48), `CARDRead` 0x8031F0E0 (`Async` 0x8031EF98), `CARDWrite` 0x8031F45C (`Async` 0x8031F348), `CARDGetStatus` 0x8031F69C, `CARDSetStatus` 0x8031F93C (`Async` 0x8031F7C8), `CARDGetSerialNo` 0x8031F984. |
| `0x80120188` | `daPy_lk_c::checkLavaFace` (uncertain) | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | `daPy_lk_c::checkLavaFace` and the actors `daBg_Draw` (0x800D8DB8), `daSea_Draw` (0x8015D80C), `daVrbox_Draw` (0x8015E3F0), `daVrbox2_Draw` (0x8015EA5C) and `dBgS::GetAttributeCode` (0x800A0B60) are observed by the edge service. **Uncertain: the purpose is diagnostic observation; not every one is active in the current module.** |
| `0x802315A8` | `dScnName_c::FileSelectOpen` | [`src/d/d_s_name.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_s_name.cpp) | Title and file-select scene functions that the host observes: `dScnName_c::FileSelectOpen` here, `create` 0x8022F9FC, `execute` 0x802305E0, `MemCardCheckMain` 0x80230A14, and the file select's `dataSelect` 0x80181634, `menuSelect` 0x80182A90, `dFile_error_c::msgDispWait` 0x8017E798 and `yesNoSelectWait` 0x8017E86C. Used to tell where in the boot and file-select flow a session is. |
| `0x801DD960` | `dMs_Execute` | [`src/d/d_menu_window.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_menu_window.cpp) | `dMs_Execute` and `dMs_collect_create` (0x801DBA58): counted to tell whether a Start press reached the pause menu. |
| `0x80328F84` | `__restore_gpr` | [`src/PowerPC_EABI_Support/Runtime/Src/runtime.c`](https://github.com/zeldaret/tww/blob/main/src/PowerPC_EABI_Support/Runtime/Src/runtime.c) | `__restore_gpr` inside the runtime: observed without ending the turn. |
| `0x80260BAC` | `JPADrawExecBillBoard::exec` | [`src/JSystem/JParticle/JPADrawVisitor.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JParticle/JPADrawVisitor.cpp) | The particle draw routines (`JPADrawExec*`, 0x80260BAC to 0x80264C4C): tagged so the renderer can tell particle types apart (`draw_tags.c`). |
| `0x803F7404` | `lastRspMadep` | [`src/JSystem/JAudio/JASAiCtrl.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JAudio/JASAiCtrl.cpp) | `JASystem::Kernel::lastRspMadep`, watched by the audio diagnostics. |
| `0x80290628` | `JAIBasic::startSoundBasic` +0xAC | [`src/JSystem/JAudio/JAIBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JAudio/JAIBasic.cpp) | `JAIBasic::startSoundBasic` (+0xAC), watched by the audio diagnostics. |
| `0x80294A10` | `JAInter::SeMgr::storeSeBuffer` | [`src/JSystem/JAudio/JAISeMgr.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JAudio/JAISeMgr.cpp) | `JAInter::SeMgr::storeSeBuffer`, watched by the audio diagnostics. |
| `0x803F7608` | `JAInter::SeMgr::seCategoryVolume` | [`src/JSystem/JAudio/JAISeMgr.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JAudio/JAISeMgr.cpp) | `seCategoryVolume` in the SE manager: the host's per-category volume sliders write here. |
| `0x803F7620` | `JAInter::SequenceMgr::seqTrackInfo` | [`src/JSystem/JAudio/JAISequenceMgr.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JAudio/JAISequenceMgr.cpp) | `seqTrackInfo` of the sequence manager: sequence volume. |
| `0x80352FE0` | `dVibration_c::MS_patt` | [`src/d/d_vib_pattern.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_vib_pattern.cpp) | Rumble patterns (`dVibration_c::MS_patt`, with `MQ_patt` 0x80353180 and `CQ_patt` 0x803531E0): translated to controller haptics. |
| `0x8037D460` | `__vt__12dVibration_c` | [`src/d/d_vibration.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_vibration.cpp) | Vtable of `dVibration_c`. |
| `0x803CA5A8` | `g_dComIfG_gameInfo.play.mVibration` | [`include/d/d_com_inf_game.h`](https://github.com/zeldaret/tww/blob/main/include/d/d_com_inf_game.h) | The vibration object `mVibration` (play +0x4700), read to turn the game rumble requests into controller haptics. |
| `0x803F7878` | `JUTGamePad::sRumbleSupported` | [`src/JSystem/JUtility/JUTGamePad.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JUtility/JUTGamePad.cpp) | `JUTGamePad::sRumbleSupported` (with `CRumble::mEnabled` at 0x803F7880): set so the game sends rumble commands. |
| `0x803F62AC` | `n_color0` | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | Link's tunic colour constants (`n_color0`, with `y_color0` 0x803F62B0 and `g_color0` 0x803F62B4), written by the Cosmetics Editor for the hero, casual and other palettes. |
| `0x803F6268` | `color0` (uncertain) | [`src/d/actor/d_a_boomerang.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_boomerang.cpp) | Boomerang trail colour (`color0`), written by the Cosmetics Editor. **Uncertain which trail it is.** |
| `0x803F7578` | `JAIBasic::msBasic` | [`src/JSystem/JAudio/JAIBasic.cpp`](https://github.com/zeldaret/tww/blob/main/src/JSystem/JAudio/JAIBasic.cpp) | `JAIBasic::msBasic`, the game's audio interface pointer, used by the puppet voice code. |
| `0x803F62A0` | `l_arcName` | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | `l_arcName`, the "Link" archive name string pointer. |
| `0x803E229C` | `dDlst_shadowControl_c::mSimpleTexObj` | [`src/d/d_drawlist.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_drawlist.cpp) | Simple-shadow texture object (`dDlst_shadowControl_c::mSimpleTexObj`). |
| `0x803E46A4` | `l_offCupOnAupPacket1` | [`src/d/actor/d_a_player_main.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_player_main.cpp) | Link's static state packets (`l_offCupOnAupPacket1`, with `...Packet2` 0x803E46C0, `l_onCupOffAupPacket1` 0x803E46DC and `...Packet2` 0x803E46F8): the puppet draws the same packets so its materials are set as Link's. |
| `0x80371B44` | `__vt__19mDoExt_3DlineMat1_c` | [`src/m_Do/m_Do_ext.cpp`](https://github.com/zeldaret/tww/blob/main/src/m_Do/m_Do_ext.cpp) | Vtable of `mDoExt_3DlineMat1_c`, copied for rope strips. |
| `0x8038B880` | `l_DL` | [`src/d/actor/d_a_grid.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/actor/d_a_grid.cpp) | Display list `l_DL` of the sail grid actor, used to draw a copy of the sail. |
| `0x803F6B44` | `daSalvage_c::mTagData_p` | [`src/d/d_com_static.cpp`](https://github.com/zeldaret/tww/blob/main/src/d/d_com_static.cpp) | `daSalvage_c::mTagData_p`, the salvage point table read by Treasure Light Up Close (`mInfo[160]`, 0x38 bytes each; kind at +0x2F, item at +0x30; Triforce Shards are 0x61-0x68). |

## What is not listed or not verified

- **Randomizer seed hooks.** A seed's item ids reach the game through 45 hook sites and 42 data bytes. They are
  generated from wwrando's patched code by `scripts/rando/variant_sites.py` (inputs in
  `scripts/rando/seed_sites.json`) and change with the pinned wwrando release, so they are not copied here.
- **File select layout.** The `dFile_select_c` and `dScnName_c` offsets in `runtime/host/src/game_state.c` are
  taken from the host source. Only a few were compared with `include/d/d_file_select.h`.
- **Hero's Charm bit.** Two parts of the host read different bytes of `mCollect` (see the row in the first
  table). It is not confirmed which is intended.
- **Observation-only addresses.** The "Device boundary" table lists addresses that the host's edge-intercept
  table or its diagnostics recognise. Which of them are active depends on how the module was built; that was
  not checked for each one.
- **Inline functions.** Some host calls go to the out-of-line copy of a function that the decomp defines in a
  header (`mDoAud_seStart`). The listed source file is the header, and the address belongs to a translation
  unit that happened to emit a copy.
- **Other game versions.** Nothing here was checked for the other regions' builds in the decomp.
