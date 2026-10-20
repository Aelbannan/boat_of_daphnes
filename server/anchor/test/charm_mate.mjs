// A teammate for the Hero's Charm / Bunny Hood team sync scenario (scripts/windows/scenarios/team_charm.txt): joins the
// default team, prints every SYNC_SAVE change the game sends (the charm's byte is save offset 0xB8), and after <delay>
// seconds sends the charm as an older build would: owned AND worn (0x03). The game must take it as owned only.
// Usage: node test/charm_mate.mjs <ws://host:port> <room> <seconds> <delay>
const [base, room, seconds, delay] = [process.argv[2], process.argv[3], Number(process.argv[4] || 60), Number(process.argv[5] || 25)];
const ws = new WebSocket(`${base}/ws?room=${room}`);
ws.onopen = () => {
  ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1, clientState: { name: 'Mate', teamId: 'default', stage: 'sea', isSaveLoaded: true } }));
  setTimeout(() => {
    ws.send(JSON.stringify({ type: 'SYNC_SAVE', changes: [[0xB8, 0x03]], targetTeamId: 'default', addToQueue: true }));
    console.log('sent the charm owned+worn (0x03)');
  }, delay * 1000);
};
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === 'SYNC_SAVE') console.log(new Date().toISOString(), 'SYNC_SAVE', JSON.stringify(m.changes));
  if (m.type === 'REQUEST_TEAM_STATE') {
    ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId }));
    console.log('answered a team state request');
  }
};
setTimeout(() => { ws.close(); process.exit(0); }, seconds * 1000);
