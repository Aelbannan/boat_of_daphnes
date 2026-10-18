// A scripted second player for the player-interaction scenarios (scripts/windows/scenarios/anchor_pvp_*.txt): joins first,
// so it owns the room and sets its PvP mode; copies the game's Link pose once and then stands still as a puppet at a fixed
// place beside where Link was (so Link can walk into it, target it and hit it); logs the PVP_HIT the game sends when Link
// hits it, and sends PVP_HIT to the game every <hit every> seconds (0: never).
// Usage: node test/pvp_mate.mjs <ws://host:port> <room> <seconds> <pvp: off|no_damage|on> <hit every> <offset x> <offset z> [scale]
const [base, room, seconds, pvp, hitEvery, ox, oz, scale] = process.argv.slice(2);
const ws = new WebSocket(`${base}/ws?room=${room}`);
let me = 0, gameId = 0, frame = null, spot = null;
ws.onopen = () => ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1,
  roomState: { pvp, pvpScale: Number(scale || 1), pvpFriendlyFire: 0, playerCollision: 1 },
  clientState: { name: 'Mate', teamId: 'blue', stage: 'sea', room: 44, isSaveLoaded: true } }));
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === 'ALL_CLIENT_STATE') {
    for (const c of m.state) {
      if (c.self) me = c.clientId;
      else if (c.online && !gameId) { gameId = c.clientId; console.log('the game is player', gameId); }
    }
  }
  if (m.type === 'UPDATE_ROOM_STATE') console.log('room state', JSON.stringify(m.roomState), 'owner', m.ownerClientId);
  if (m.type === 'PLAYER_UPDATE' && m.pose && !frame) {
    frame = m;
    spot = [m.pos[0] + Number(ox), m.pos[1], m.pos[2] + Number(oz)];
    console.log('standing at', spot.map((v) => v.toFixed(0)).join(' '), 'stage', m.stage, 'room', m.room);
  }
  if (m.type === 'PVP_HIT') console.log('GOT PVP_HIT from', m.clientId, JSON.stringify(m));
  if (m.type === 'REQUEST_TEAM_STATE')
    ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId }));
};
setInterval(() => {
  if (!frame) return;
  const pose = Buffer.from(frame.pose, 'base64');
  for (let i = 0; i < 3; i++) pose.writeFloatLE(spot[i], 6 + 4 * i); // base position, wire byte 6
  ws.send(JSON.stringify({ type: 'PLAYER_UPDATE', stage: frame.stage, room: frame.room, pos: spot, angle: frame.angle, pose: pose.toString('base64') }));
}, 100);
if (Number(hitEvery) > 0)
  setInterval(() => {
    if (!frame || !gameId) return;
    ws.send(JSON.stringify({ type: 'PVP_HIT', targetClientId: gameId, damage: 2, hitType: 2, pos: spot, direction: [0, 1] }));
    console.log('sent PVP_HIT to', gameId);
  }, Number(hitEvery) * 1000);
setTimeout(() => { ws.close(); process.exit(0); }, Number(seconds) * 1000);
