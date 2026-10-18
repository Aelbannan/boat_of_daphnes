# Online play (Anchor)

Anchor is the port's online co-op. Players who join the same *room* see each other as Link in the world, and
players on the same *team* share what they find. The idea and the name come from Ship of Harkinian's Anchor; the
code is this project's own and the protocol is its own (see [Protocol](#protocol)).

Everything runs from the game's settings menu (**Network > Anchor**). The server is a small program you can run
yourself ([Running your own server](#running-your-own-server)).

## For players

### Joining

Open the menu with F1 or Esc, then **Network > Anchor**.

| Setting | Meaning |
| --- | --- |
| Name | What other players see (up to 24 characters). |
| Room Name | Players who enter the same name play together. **The name is the room's password**: anyone who knows it can join, and nobody can list a room unless its owner lists it. 1 to 64 characters. The default `ww-global` is a public room by convention. |
| Team | Players on one team share items and progress. Other teams in the room are seen but share nothing. The default is `default`, so everyone in a room is on one team unless they change it. |
| Server Address | The server to use. The project's server is the default. `host:port` connects without encryption, for a server on your own PC. |
| Connect / Disconnect | The room and team can only be changed while disconnected. |

Your colour is your Hero tunic colour from the Cosmetics Editor (green if you have not changed it), and it is shared with the room. Anchor connects again at launch if it was
connected last time. A reconnecting game keeps its place in the room; the game sends a random token kept in your
settings so a crash does not leave a ghost player behind.

Everyone in a room must run a build with the same Anchor protocol version. A red `(!)` next to a player's name in
the **Anchor Room** window means their version differs and some features may not work together.

### Rooms and the room owner

The first player online in an empty room owns it. The owner changes the room's settings (PvP, below); everyone
else sees them. If the owner leaves, the next player becomes the owner. A room's state lives on the server until
it has been empty and unused for a while.

A room holds at most 32 players.

### The Anchor Room window

Lists the players in the room with their names, where each one is (an island's name and its Sea Chart square, or
the dungeon or interior), and whether they are offline. It is also where you teleport to someone.

### Item and flag sync

While you are connected to a room that syncs and in a loaded game, your save and your teammates' saves
are merged as you play. It only ever *adds*:

- hearts, magic, the wallet and ammo capacities keep the larger value;
- item slots are filled when empty or upgraded (the Picto Box to the Deluxe one, the Bow to the Fire and Ice, then
  Light Arrows). Bottles are not shared;
- item-get bits, charts and the sea map, songs, pearls, Triforce shards and the other collection bits are OR-ed
  together;
- each dungeon's chests, switches, visited rooms, maps, compasses, big keys and bosses, and the ocean flags, and
  the story's event flags;
- small keys are shared as *gains and uses* rather than as a count, since keys are spent.

When a teammate gets an item, you see a short message with the item's icon. Your first sync after joining or
reconnecting asks the team for its state, so a player who joins late catches up: the server keeps the last team
state and a queue of changes made while someone was away.

Merges only happen when no event or cutscene is running, so nothing changes under you during one.

**Randomizer games.** A save from a different randomizer seed, or a vanilla save compared with a randomizer one,
is ignored: its items and flags mean different things. Play the same seed to sync.

If the connection or sync seems stuck, **Sync from Team** (in the same page) asks the team for its state again.

### Switch sync

Doors, gates, pushed blocks and other puzzles are driven by *switches*. When your game sets a switch, teammates in
the same stage get it set too, as it happens. If you open a gate, it opens for them when they are in the same
place. A switch the game *clears* is kept private when it belongs to the stage (timed gates and temporary states
stay your own), but cleared dungeon and zone switches are shared.

### Seeing other players

Other players appear as Link models, doing what their Link is doing: walking, rolling, swimming, holding items out,
swinging a sword, riding a boat. The sender's pose is read from its Link and replayed on the receiver's puppet, so
any action shows without special handling. The puppet carries the sender's equipment, outfit, colours and the
Bunny Hood if worn. Rope, hookshot chain and boat sail are drawn too. A player in the same stage and room as you
also shows as a coloured arrow on the minimap.

| Setting | Meaning |
| --- | --- |
| Show Nametags | The name above each player on your stage. |
| Players on the Sea Chart | See below. |
| Puppet Voice and Sounds | Puppets make Link's voice and sounds when they roll, jump, cut or are hurt. They use *your* game's voice. |
| Share Custom Player Content | Shares your player model, voice and equipment (the Player Model entry of the Mod Menu) with the room and shows theirs. Content is downloaded in the background and kept in the cache folder (up to 500 MB). Off: yours is not sent and theirs is not used; everyone is drawn as the game's own Link. |

Shared content is checked by hash and is only ever model and sound archives; it is never run as code.

### The Sea Chart

With **Players on the Sea Chart** on, the pause menu's Sea Chart shows the other players of the room as copies of
Link's chart arrow in their colour, wherever they are. At sea, the arrow is at their real position. On an island or
inside a dungeon it is at the centre of that island's chart square. Your own arrow is unchanged. Positions are sent
about five times a second.

### Teleport

In the Anchor Room window, an arrow button next to a player teleports you to them. The room's teleport setting says
who may:

| Mode | Who can teleport to whom |
| --- | --- |
| 0 | Nobody. |
| 1 (the default) | Players on the same team. |
| 2 | Anyone in the room. |

Both players must be in a loaded game. The target's game answers with the last place its Link stood on the ground
(not a mid-air position, so you cannot arrive over a pit), and your game changes scene as an exit does. If the
target is sailing, you arrive on a ship placed beside theirs. A second request within a few seconds is ignored, a request during a scene change is dropped, and
a teleport waits until no event is running.

### Chat

**Network > Anchor** has a chat page. During play, press the chat key (T by default; it can be changed) to open a
line over the game. The game's pad and keyboard are blocked while it is open; Enter sends, an empty Enter or Esc
closes it. Messages from others also appear as notifications while the chat window is closed.

### Player interaction (PvP)

The room owner sets how players meet, in **Room Settings**.

| Setting | Meaning |
| --- | --- |
| PvP: Off | Players cannot hit each other. |
| PvP: No Damage | Hits land (knockback, the hurt animation and sound) but nobody loses a heart. |
| PvP: On | Hits hurt, as an enemy's would, with the game's own invincibility frames, hearts, fairies and game over. |
| PvP Damage | Half, Normal or Double (with PvP On). The server keeps a factor from 0.25 to 4. |
| Friendly Fire Within a Team | Players on one team can hit each other. |
| Player Collision | Players are solid: they push each other apart instead of walking through, and Z-targeting can lock on to one. |

Your sword, arrows, bombs and other items hit a puppet through the game's own collision system; your game sends
the hit to that player, whose game applies it with knockback and the usual hurt state. A player takes no hits for a
few seconds after a scene starts, during an event, a menu or a load, and a hit has a short cooldown.

## For server operators

The server in `server/anchor` is one Cloudflare Worker. You can run it on your own machine to try things, or deploy
it to your own Cloudflare account.

- **Rooms**: each room is a Durable Object (`src/room.js`) using WebSocket hibernation, so an idle room costs nothing.
- **Room list**: a Lobby Durable Object (`src/lobby.js`) lists rooms whose owners ask to be listed. `GET /rooms`
  returns them; entries a room has not refreshed for two minutes drop.
- **Custom player content**: models and voices that players share are stored by hash in a Workers KV namespace
  (`src/content.js`). The server accepts only RARC or Yaz0 archives, at most 20 MB each, and rate-limits uploads
  per client address. Blobs expire after 30 days and are uploaded again when needed.
- **Endpoints**: `/ws?room=<name>` (WebSocket), `/rooms`, `/content/<sha256>`, `/health`.

### Run it locally

```
cd server/anchor
npm install
npm run dev          # http://127.0.0.1:8787  (ws://127.0.0.1:8787/ws?room=test)
npm test             # with `npm run dev` running: players in a room, routing, team state, PvP settings
```

Then in the game set **Server Address** to `127.0.0.1:8787` (a `host:port` address uses an unencrypted connection).
The scripts in `server/anchor/test/` are scripted players: a puppet replaying a pose, a teammate sending items, a
PvP opponent. Each says at its top how to run it.

### Deploy your own

```
npx wrangler login
npx wrangler kv namespace create CONTENT     # put the id in wrangler.jsonc
npm run deploy
```

Add a `routes` entry in `wrangler.jsonc` to serve it on your own domain. The first deploy creates the Durable Object
classes (migration `v1`); later changes are only `npm run deploy`. An optional secret, `IP_SALT`, salts the hash
that the upload rate limit is kept under (`wrangler secret put IP_SALT`). The `PROTOCOLS` variable lists the
protocol versions the server accepts.

The repository's copy of `src/room_log.js` is a stub that keeps no log. If you want to record room events, put
your own code there; it is called as rooms open, fill and close.

### Limits

- 32 players a room.
- 2,000 stored packets a team.
- 64 KB a message.
- Room names are 1 to 64 characters, and the server never shows a room's name to anyone who does not already
  have it. Players are known by a numeric id and the name and colour they send.

Treat the room name as a secret; anyone who has it can join.

## Protocol

The protocol is version 1, carried as JSON text messages over a WebSocket. `server/anchor/PROTOCOL.md` has the
full description. In short:

- The first message is `HANDSHAKE` (protocol version, the client's state, and the id and token to rejoin as the
  same player). The server answers with the room state and the list of players (`ALL_CLIENT_STATE`).
- The server handles `HANDSHAKE`, `UPDATE_CLIENT_STATE`, `UPDATE_ROOM_STATE` (owner only), `UPDATE_TEAM_STATE` and
  `REQUEST_TEAM_STATE` itself. `PLAYER_UPDATE` goes only to players whose stage is the sender's. Anything else is
  relayed by its `targetClientId` (one player), `targetTeamId` (a team) or to everyone else in the room, and with
  `addToQueue: true` it is also kept for the team's members who are offline.
- Messages between players carry the game's data: `SYNC_SAVE` (byte changes to the synced parts of the save),
  `SET_SWITCH`, `ITEM_FOUND`, `REQUEST_TELEPORT` and `TELEPORT_TO`, `PVP_HIT`, `CHART_UPDATE`, `CHAT`,
  `GAME_COMPLETE`. `PROTOCOL.md` documents most of these; `SET_SWITCH` and `CHAT` are used by the client and relayed
  by the server like any other type.
- `SET_CHECK_STATUS` is reserved in the protocol for randomizer check status. The current client does not send it:
  randomizer games sync through the save like vanilla games do.
- A client sends `{"type":"PING"}` about every 30 seconds and the server answers `PONG`.

The room state has `syncItemsAndFlags`, `teleportMode`, `showLocationsMode`, `pvpMode`, `listed`, `title`, and the
player-interaction fields `pvp` (`off`, `no_damage`, `on`), `pvpScale` (0.25 to 4), `pvpFriendlyFire` and
`playerCollision`. The in-game Room Settings page edits the last four; the others keep the server's defaults
(syncing on, teleport to teammates, not listed) unless a client sets them.

The client sends its Anchor build identifier as `clientVersion`. Raise it when a message changes in a way older
builds cannot follow.
