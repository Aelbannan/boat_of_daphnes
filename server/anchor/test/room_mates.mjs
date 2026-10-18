const [base, room, seconds] = [process.argv[2], process.argv[3], Number(process.argv[4] || 90)];
const mates = [
  { name: 'finald', color: '#E8A030', teamId: 'red', clientVersion: 'bod-anchor-2026.10.17', stage: 'sea', room: 11, isSaveLoaded: true, linkColors: 'Casual.Shirt=ff0000' },
  { name: 'Tetra', color: '#40D080', teamId: 'blue', stage: 'sea', room: 44, isSaveLoaded: true },
];
for (const st of mates) {
  const ws = new WebSocket(`${base}/ws?room=${room}`);
  ws.onopen = () => ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1, clientState: st }));
  ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.type === 'REQUEST_TEAM_STATE') ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId })); };
}
setTimeout(() => process.exit(0), seconds * 1000);
