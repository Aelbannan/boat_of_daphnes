// A teammate for the Check Tracker scenario (scripts/windows/scenarios/check_tracker_anchor.txt): joins team red and, every
// <delay> seconds (the game may join late), sends a SYNC_SAVE that opens one chest of Dragon Roost Cavern's save table
// (table 3 at 0x380 + 3 * 0x24, mTbox bit 10: Boss Stairs Left Chest), as a game's team sync does for a chest it opened.
// Usage: node test/check_mate.mjs <ws://host:port> <room> <seconds> <delay>
const [base, room, seconds, delay] = [process.argv[2], process.argv[3], Number(process.argv[4] || 60), Number(process.argv[5] || 20)];
const ws = new WebSocket(`${base}/ws?room=${room}`);
ws.onopen = () => {
  ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1, clientState: { name: 'Mate', teamId: 'red', stage: 'sea', isSaveLoaded: true } }));
  setInterval(() => {
    ws.send(JSON.stringify({ type: 'SYNC_SAVE', changes: [[0x3EC + 2, 0x04]], targetTeamId: 'red', addToQueue: true }));
    console.log('sent the chest');
  }, delay * 1000);
};
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === 'REQUEST_TEAM_STATE') ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId }));
};
setTimeout(() => process.exit(0), seconds * 1000);
