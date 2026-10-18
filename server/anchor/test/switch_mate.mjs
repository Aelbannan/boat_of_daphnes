// A teammate for the live switch sync scenario (scripts/windows/scenarios/anchor_switch_sync.txt): logs the SET_SWITCH
// messages the game sends, and after <delay> seconds sends one of its own for the game's stage.
// Usage: node test/switch_mate.mjs <ws://host:port> <room> <seconds> <delay> <stage> <room no> <switch no>
const [base, room, seconds, delay, stage, stageRoom, no] = process.argv.slice(2);
const ws = new WebSocket(`${base}/ws?room=${room}`);
ws.onopen = () => {
  ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1,
    clientState: { name: 'Mate', teamId: 'default', stage, room: Number(stageRoom), isSaveLoaded: true } }));
  setInterval(() => {
    ws.send(JSON.stringify({ type: 'SET_SWITCH', stage, room: Number(stageRoom), no: Number(no), on: true, seed: '',
      targetTeamId: 'default' }));
    console.log('sent SET_SWITCH', stage, stageRoom, no);
  }, Number(delay) * 1000); // again and again: the game may join after the first
};
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === 'SET_SWITCH') console.log('got SET_SWITCH', m.stage, m.room, m.no, m.on);
  if (m.type === 'REQUEST_TEAM_STATE')
    ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId }));
};
setTimeout(() => process.exit(0), Number(seconds) * 1000);
