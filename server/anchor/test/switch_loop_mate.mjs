// The pirate ship's switch storm: a mate whose actors toggle zone switches 226-229 of room 13
// again and again. Sends them <rate> times a second for <seconds>, on and off, plus one persistent switch (a stage
// memory switch, <persist no>) every <delay> s (the game may join after the first), which the game should still set. Logs what the game sends back.
// Usage: node test/switch_loop_mate.mjs <ws://host:port> <room> <seconds> <stage> <stage room> <persist no> <delay> [rate]
const [base, room, seconds, stage, stageRoom, persist, delay, rate] = process.argv.slice(2);
const ws = new WebSocket(`${base}/ws?room=${room}`);
let sent = 0, got = 0, flip = false;
ws.onopen = () => {
  ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1,
    clientState: { name: 'Mate', teamId: 'default', stage, room: Number(stageRoom), isSaveLoaded: true } }));
  setInterval(() => {
    flip = !flip;
    for (const no of [226, 227, 228, 229]) {
      ws.send(JSON.stringify({ type: 'SET_SWITCH', stage, room: Number(stageRoom), no, on: flip, seed: '', targetTeamId: 'default' }));
      ++sent;
    }
  }, 1000 / Number(rate || 4));
  setInterval(() => {
    ws.send(JSON.stringify({ type: 'SET_SWITCH', stage, room: Number(stageRoom), no: Number(persist), on: true, seed: '', targetTeamId: 'default' }));
    console.log(new Date().toISOString(), 'sent persistent switch', persist);
  }, Number(delay) * 1000);
};
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === 'SET_SWITCH') { ++got; console.log(new Date().toISOString(), 'got SET_SWITCH', m.stage, m.room, m.no, m.on); }
  if (m.type === 'REQUEST_TEAM_STATE')
    ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId }));
};
setTimeout(() => { console.log(`done: sent ${sent} zone toggles, game sent ${got} switches`); process.exit(0); }, Number(seconds) * 1000);
