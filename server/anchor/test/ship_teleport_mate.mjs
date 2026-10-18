// A sailing teammate for the ship teleport scenario (scripts/windows/scenarios/anchor_teleport.txt):
// joins team red on the sea stage, keeps the game's first pose, stands a little beside it
// (its PLAYER_UPDATEs draw the game's marker for it), and answers the game's REQUEST_TELEPORT
// as a player sailing in sea sector 41 (sailing, shipPos, shipAngle).
// Usage: node test/teleport_mate.mjs <ws://host:port> <room> <seconds>
const [base, room, seconds] = [process.argv[2], process.argv[3], Number(process.argv[4] || 90)];
const ws = new WebSocket(`${base}/ws?room=${room}`);
let first = null;
ws.onopen = () => {
  ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1,
    clientState: { name: 'Mate', teamId: 'red', stage: 'sea', room: 41, isSaveLoaded: true } }));
  setInterval(() => {
    if (!first) return;
    const pos = [first.pos[0] + 150, first.pos[1], first.pos[2]];
    ws.send(JSON.stringify({ type: 'PLAYER_UPDATE', stage: first.stage, room: first.room, pos, angle: [0, 0, 0] }));
  }, 100);
};
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === 'PLAYER_UPDATE' && !first) {
    first = m;
    console.log('first pose', JSON.stringify(m.pos), m.stage, m.room);
  }
  if (m.type === 'REQUEST_TELEPORT') {
    // Sailing: sea sector 41, on the ship at this place; the answer says so, and where the ship is.
    ws.send(JSON.stringify({ type: 'TELEPORT_TO', targetClientId: m.clientId, stage: 'sea', room: 41, point: 100,
      pos: [111554, 968, 111243], angle: 0, sailing: true, shipPos: [111804, 968, 111243], shipAngle: 0 }));
    console.log('answered REQUEST_TELEPORT as a sailing player');
  }
  if (m.type === 'REQUEST_TEAM_STATE')
    ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId }));
};
setTimeout(() => { ws.close(); process.exit(0); }, seconds * 1000);
