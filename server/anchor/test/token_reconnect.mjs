// A game that comes back after a crash (a new connection, no clientId) with its install token gets its seat back;
// another token does not take it. Usage: node test/token_reconnect.mjs <ws://host:port> <room>
const [base, room] = process.argv.slice(2);
function join(token) {
  return new Promise((resolve) => {
    const ws = new WebSocket(`${base}/ws?room=${room}`);
    ws.onopen = () => ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1, token,
      clientState: { name: 'Tok', stage: 'sea', room: 44, isSaveLoaded: true } }));
    ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      if (m.type === 'ALL_CLIENT_STATE') {
        const me = m.state.find((c) => c.self);
        if (me) resolve({ ws, id: me.clientId, count: m.state.length, leaked: m.state.some((c) => 'token' in c) });
      }
    };
  });
}
const a = await join('token-A');
a.ws.close();
await new Promise((r) => setTimeout(r, 3000));
const again = await join('token-A');
const other = await join('token-B');
console.log(`first ${a.id}, back with the same token ${again.id}, another token ${other.id}, entries ${other.count}, token in list ${other.leaked}`);
const ok = again.id === a.id && other.id !== a.id && !other.leaked;
console.log(ok ? 'PASS' : 'FAIL');
process.exit(ok ? 0 : 1);
