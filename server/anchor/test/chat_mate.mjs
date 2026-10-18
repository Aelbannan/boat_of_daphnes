// A scripted chat partner: joins the room as "finald" (its own colour) and says a line every few seconds, answering "found the
// sword" style messages the game sends. Usage: node test/chat_mate.mjs <ws://host:port> <room> <seconds> [interval seconds]
const [base, room, seconds, every] = [process.argv[2], process.argv[3], Number(process.argv[4] || 90), Number(process.argv[5] || 8)];
const ws = new WebSocket(`${base}/ws?room=${room}`);
const lines = ['epic', 'where did you find it?', 'nice', 'on my way', 'ha, same here', 'one sec'];
let n = 0;
ws.onopen = () => {
  ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1, clientState: { name: 'finald', color: '#E8A030', teamId: 'red', stage: 'sea', isSaveLoaded: true } }));
  setInterval(() => ws.send(JSON.stringify({ type: 'CHAT', text: lines[n++ % lines.length] })), every * 1000);
};
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === 'CHAT') console.log('chat from', m.clientId, m.text);
  if (m.type === 'REQUEST_TEAM_STATE') ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId }));
};
setTimeout(() => { ws.close(); process.exit(0); }, seconds * 1000);
