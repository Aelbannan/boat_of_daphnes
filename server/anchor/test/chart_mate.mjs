// A teammate standing at a fixed place, for the Sea Chart scenario (scripts/windows/scenarios/anchor_sea_chart.txt).
// Usage: node test/chart_mate.mjs <ws://host:port> <room> <seconds> <stage> <stage room> <x> <z> <angle> [seaX seaZ]
// seaX/seaZ (the chart square's centre, world units) are sent for a mate ashore, as the game sends them.
const [base, room, seconds, stage, stageRoom, x, z, angle, seaX, seaZ] = process.argv.slice(2);
const ws = new WebSocket(`${base}/ws?room=${room}`);
ws.onopen = () => {
  ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1,
    clientState: { teamId: 'blue', color: process.env.COLOR || '#ff2020', name: process.env.NAME || 'Mate', stage, room: Number(stageRoom), isSaveLoaded: true } }));
  setInterval(() => {
    ws.send(JSON.stringify({ type: 'PLAYER_UPDATE', stage, room: Number(stageRoom), pos: [Number(x), 0, Number(z)], angle: [0, Number(angle), 0] }));
    ws.send(JSON.stringify({ type: 'CHART_UPDATE', sea: seaX !== undefined ? [Number(seaX), Number(seaZ)] : [Number(x), Number(z)], angle: Number(angle) }));
  }, 200);
};
setTimeout(() => process.exit(0), Number(seconds) * 1000);
