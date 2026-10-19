# Online play protocol, version 1

This is the protocol of Ship of Harkinian's Anchor (see `docs/anchor.md`), carried over
WebSockets for The Wind Waker.

**Connection.** Connect to `wss://wwserver.viosarcade.xyz/ws?room=<name>`. The name (1 to 64
characters) is the room's password: anyone with it joins, and nobody can list a room unless its owner
lists it. `ww-global` is the public room by convention.

**Messages.** Each WebSocket text message is one JSON object with a `type`. The server stamps
`clientId` (the sender) on everything it relays. `{"type":"PING"}` is answered `{"type":"PONG"}`; send
it about every 30 seconds.

## Routing

The server handles some types itself (below). Any other type is relayed according to these fields:

- `targetClientId`: to that player only;
- `targetTeamId`: to the online players of that team;
- neither: to everyone else in the room.

With `addToQueue: true` the server also keeps the message for the team, for teammates who are not
online. Those players receive it in the next `UPDATE_TEAM_STATE` they ask for.

## Types the server handles

| Type | From | Fields | Server |
| --- | --- | --- | --- |
| `HANDSHAKE` | client, first | `protocol` (1), `clientId` (to rejoin as the same player), `clientState`, `roomState` (used if the room is new or ownerless) | gives the player an id; the first player online owns the room; answers `UPDATE_ROOM_STATE` and sends everyone `ALL_CLIENT_STATE` |
| `UPDATE_CLIENT_STATE` | client | `state`: `name`, `color`, `teamId`, `version`, `settingsHash`, `seed`, `isSaveLoaded`, `isGameComplete`, `stage`, `room`, `point` | stores the player's record, sends everyone `ALL_CLIENT_STATE` |
| `UPDATE_ROOM_STATE` | room owner | `roomState`: `syncItemsAndFlags`, `teleportMode`, `showLocationsMode`, `pvpMode`, `listed`, `title`, and the player-interaction settings `pvp` (`off`, `no_damage`, `on`), `pvpScale` (damage multiplier, 0.25-4), `pvpFriendlyFire` (0/1: hits within a team), `playerCollision` (0/1) | stores it (the interaction settings clamped to their ranges) and sends it to everyone; refused for other players |
| `UPDATE_TEAM_STATE` | client | `state` (the team's save data, see below), `targetClientId` (when answering a request) | stores it as the team's state and empties the team's queue; forwards it when targeted |
| `REQUEST_TEAM_STATE` | client | none | forwards it to a teammate who has a save loaded; if none is online, answers with the stored `state` and `queue` |
| `PLAYER_UPDATE` | client | pose (below) | sends it only to players whose `stage` is the sender's |
| `CHART_UPDATE` | client | `sea` [x, z] (world units: the player's place at sea, or the centre of the Sea Chart square of the island they are on), `angle` (the game's s16 facing) | not a server type: relayed to the whole room like any other (about 5 a second), for the other players' Sea Charts |

## Types the server sends

| Type | Fields |
| --- | --- |
| `ALL_CLIENT_STATE` | `state`: every player's record with `clientId`, `online`, and `self` (true in your own) |
| `UPDATE_ROOM_STATE` | `roomState`, `ownerClientId` |
| `ERROR` | `message` (protocol mismatch, room full, not the owner, ...) |
| `SERVER_MESSAGE` | `message` (announcements) |

## Types between players (relayed)

These carry Wind Waker's data. The client defines them.

| Type | Routing | Fields |
| --- | --- | --- |
| `SYNC_SAVE` | team, queued | `changes`: [[save offset, value], ...]: bytes of the synced save parts that this player gained (bits newly set, a larger maximum, a filled or upgraded item slot) |
| `SET_SWITCH` | team (`targetTeamId`) | `stage`, `room`, `no` (the switch), `on`, `seed` (the sender's randomizer seed, empty for the normal game; other seeds ignore it): a switch the sender's game set or cleared, applied live by teammates in the same place |
| `CHAT` | room | `text` (at most a line; control characters removed) |
| `REQUEST_TELEPORT` | one player | none |
| `TELEPORT_TO` | one player | `stage`, `room`, `point`, `pos`, `angle` |
| `DAMAGE_PLAYER` | one player | `damage`, `effect` (reserved: the game does not send it yet) |
| `PVP_HIT` | one player (`targetClientId` required; dropped while the room's `pvp` is `off`) | `damage` (the attacker's attack power), `hitType` (the game's attack type bits), `pos` (the attacker's place), `direction` ([x, z], from the attacker to the target) |
| `GAME_COMPLETE` | room | none (reserved: the game does not send it yet) |

**Team state.** `state` is the save's synced parts as [[offset, value], ...] (`runtime/host/src/team_sync.c`:
max life, wallet, max magic, equipped sword/shield/bracelet, item slots except bottles, item-get bits,
ammo maxima, bag-get bits, collection, charts and the sea map, each stage's memory, the ocean flags and the
event flags without the event registers). The receiver merges it (bits OR-ed, maxima kept, item slots filled
or upgraded) and replays `queue`. Small keys use pseudo offsets: `0x0F00 + table` carries a signed change
(a key found or used) in `SYNC_SAVE`, `0x0F10 + table` a count in a team state.

**Pose.** `PLAYER_UPDATE` carries `stage`, `room`, `pos` [x, y, z], `angle` [x, y, z], `anim` (animation ids
and frames), `equip` (sword, shield, held item), `boat` (when sailing), `outfit`, `colors`. A binary format may
replace it later; binary frames are reserved for that.

## ITEM_FOUND (client to team, no server change)

A player's game names what its own save just gained, so teammates can show "<name> found <item>".
`{"type":"ITEM_FOUND","itemId":<tww dItemNo, 0-255>,"name":"<English name>","targetTeamId":"<team>"}`.
It is relayed by the server's default case to the team's online members (the server adds `clientId`);
it is not queued (`addToQueue` is absent), so a player who joins later gets no toasts for old items.
