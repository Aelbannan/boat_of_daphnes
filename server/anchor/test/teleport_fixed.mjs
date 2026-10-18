// A teammate standing at a fixed place, for teleport scenarios into dungeons (scripts/windows/scenarios/anchor_teleport_dungeon.txt):
// reports that place in its PLAYER_UPDATEs and answers the game's REQUEST_TELEPORT with it.
// Usage: node test/teleport_fixed.mjs <ws://host:port> <room> <seconds> <stage> <stage room> <x> <y> <z>
const [base, room, seconds, stage, stageRoom, x, y, z] = process.argv.slice(2);
const pos = [Number(x), Number(y), Number(z)];
const ws = new WebSocket(`${base}/ws?room=${room}`);
ws.onopen = () => {
  ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1,
    clientState: { name: 'Mate', teamId: 'red', stage, room: Number(stageRoom), isSaveLoaded: true } }));
  setInterval(() => ws.send(JSON.stringify({ type: 'PLAYER_UPDATE', stage, room: Number(stageRoom), pos, angle: [0, 0, 0] })), 100);
};
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === 'REQUEST_TELEPORT') {
    ws.send(JSON.stringify({ type: 'TELEPORT_TO', targetClientId: m.clientId, stage, room: Number(stageRoom), point: 0, pos, angle: 0 }));
    console.log('answered REQUEST_TELEPORT', stage, stageRoom, JSON.stringify(pos));
  }
  if (m.type === 'REQUEST_TEAM_STATE')
    ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId }));
};
setTimeout(() => process.exit(0), Number(seconds) * 1000);
