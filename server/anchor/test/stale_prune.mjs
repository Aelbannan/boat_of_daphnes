// Offline entries are pruned: five players named "Alpha" from older builds (no install token) join and leave, one
// "Bravo" joins and leaves; then "Alpha" joins again. The room must list that one Alpha (online) and Bravo (offline,
// recent), not the five stale Alphas.
// Usage: node test/stale_prune.mjs <ws://host:port> [room]
const [base, room = `prune-${Date.now()}`] = process.argv.slice(2);

function join(name) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`${base}/ws?room=${room}`);
    let list = null;
    ws.onopen = () => ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1,
      clientState: { name, teamId: 'default', isSaveLoaded: false } }));
    ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      if (m.type === 'ALL_CLIENT_STATE') {
        list = m.state;
        resolve({ ws, list: () => list });
      }
    };
    ws.onerror = reject;
  });
}
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

for (let i = 0; i < 5; ++i) {
  const p = await join('Alpha');
  p.ws.close();
  await wait(300);
}
const b = await join('Bravo');
b.ws.close();
await wait(500);
const back = await join('Alpha');
await wait(500);
const list = back.list();
const alphas = list.filter((c) => c.name === 'Alpha');
const bravo = list.filter((c) => c.name === 'Bravo');
console.log('list:', list.map((c) => `${c.name}${c.online ? '' : ' (offline)'}`).join(', '));
back.ws.close();
const ok = alphas.length === 1 && alphas[0].online && bravo.length === 1 && !bravo[0].online;
console.log(ok ? 'PASS' : 'FAIL');
process.exit(ok ? 0 : 1);
