// A teammate for the toast scenario (scripts/windows/scenarios/anchor_toasts.txt). It joins team
// red and waits until the game is in the room, then leaves and comes back (the game shows "Mate
// Disconnected", then "Mate Connected") and, a few seconds apart, sends ITEM_FOUND (the game's
// toast with the item's icon), a SYNC_SAVE ("Save updated from team") and, last, leaves again.
// It logs the ITEM_FOUND messages the game sends when it picks an item up itself.
// Usage: node test/toast_mate.mjs <ws://host:port> <room> [seconds-to-wait-for-the-game]
const [base, room, patience] = [process.argv[2], process.argv[3], Number(process.argv[4] || 600)];
let id = 0;
const open = (onGame, onMessage) => {
  const ws = new WebSocket(`${base}/ws?room=${room}`);
  ws.send_ = (m) => ws.send(JSON.stringify(m));
  ws.onopen = () => ws.send_({ type: 'HANDSHAKE', protocol: 1, ...(id ? { clientId: id } : {}),
    clientState: { name: 'Mate', teamId: 'red', stage: 'sea', room: 44, isSaveLoaded: true } });
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.type === 'ALL_CLIENT_STATE') {
      id = m.state.find((c) => c.self)?.clientId ?? id;
      if (m.state.some((c) => !c.self && c.online)) onGame(ws);
    }
    if (m.type === 'ITEM_FOUND') console.log('ITEM_FOUND from the game:', JSON.stringify(m));
    if (m.type === 'REQUEST_TEAM_STATE') ws.send_({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId });
    onMessage?.(m);
  };
  return ws;
};
let started = false;
const first = open((ws) => {
  if (started) return;
  started = true;
  console.log('the game is here');
  setTimeout(() => ws.close(), 4000); // leave: "Mate Disconnected"
  setTimeout(() => {
    const second = open(() => {});
    const found = (itemId, name) => second.send_({ type: 'ITEM_FOUND', itemId, name, targetTeamId: 'red' });
    setTimeout(() => found(0x2f, 'Hookshot'), 6000);
    setTimeout(() => found(0x6d, "Wind's Requiem"), 9000);
    setTimeout(() => found(0x69, "Nayru's Pearl"), 12000);
    setTimeout(() => second.send_({ type: 'SYNC_SAVE', changes: [], targetTeamId: 'red', addToQueue: false }), 15000);
    setTimeout(() => { second.close(); console.log('left'); }, 80000); // after the game's own pick-up
    setTimeout(() => process.exit(0), 83000);
  }, 8000); // back: "Mate Connected"
});
setTimeout(() => { console.log('the game never came'); process.exit(1); }, patience * 1000);
