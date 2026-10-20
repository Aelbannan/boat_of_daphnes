// The server's side of carrying players: the room's allowCarrying setting (default off, cleaned to 0/1, owner only) and the
// routing of CARRY_START, CARRY_POS (only to their target, only while carrying is allowed) and CARRY_END (always).
// Usage: node test/carry.mjs [ws://127.0.0.1:8787]
const base = process.argv[2] || 'ws://127.0.0.1:8787';
const room = `carry-${Date.now()}`;
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const got = (p, type, pred = () => true) => p.inbox.some((m) => m.type === type && pred(m));

function player(name) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`${base}/ws?room=${room}`);
    const p = { ws, name, inbox: [], id: 0 };
    ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      p.inbox.push(m);
      if (m.type === 'ALL_CLIENT_STATE') { const s = m.state.find((c) => c.self); if (s) p.id = s.clientId; }
    };
    ws.onerror = reject;
    ws.onopen = () => { ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1, clientState: { name, teamId: 'red', stage: 'sea', isSaveLoaded: true } })); resolve(p); };
  });
}
const send = (p, m) => p.ws.send(JSON.stringify(m));

const a = await player('Ann');
await wait(300);
const b = await player('Bo');
const c = await player('Cy');
await wait(500);

check(got(b, 'UPDATE_ROOM_STATE', (m) => m.roomState.allowCarrying === 0), 'a new room has carrying off');
send(a, { type: 'CARRY_START', targetClientId: b.id, pos: [0, 100, 0], angle: 0 });
await wait(300);
check(!got(b, 'CARRY_START'), 'CARRY_START is dropped while carrying is off');
send(b, { type: 'UPDATE_ROOM_STATE', roomState: { allowCarrying: 1 } });
await wait(300);
check(got(b, 'ERROR', (m) => /owner/.test(m.message)), 'only the owner changes the carrying setting');
send(a, { type: 'UPDATE_ROOM_STATE', roomState: { allowCarrying: 'yes' } });
await wait(300);
check(got(b, 'UPDATE_ROOM_STATE', (m) => m.roomState.allowCarrying === 1), 'the owner turns it on (stored as 1) and everyone is told');

send(a, { type: 'CARRY_START', targetClientId: b.id, pos: [1, 2, 3], angle: 100, stage: 'sea', room: 44 });
send(a, { type: 'CARRY_POS', targetClientId: b.id, pos: [4, 5, 6], angle: 100, phase: 'held' });
send(a, { type: 'CARRY_START', pos: [0, 0, 0] });
await wait(300);
check(got(b, 'CARRY_START', (m) => m.clientId === a.id && m.pos[0] === 1), 'CARRY_START reaches its target, stamped with the sender');
check(got(b, 'CARRY_POS', (m) => m.clientId === a.id && m.phase === 'held'), 'CARRY_POS reaches its target');
check(!got(c, 'CARRY_START') && !got(c, 'CARRY_POS'), 'no one else gets them');
check(b.inbox.filter((m) => m.type === 'CARRY_START').length === 1, 'a CARRY_START without a target goes nowhere');

send(b, { type: 'CARRY_END', targetClientId: a.id, reason: 'breakfree' });
await wait(300);
check(got(a, 'CARRY_END', (m) => m.clientId === b.id && m.reason === 'breakfree'), 'CARRY_END reaches the carrier');

send(a, { type: 'UPDATE_ROOM_STATE', roomState: { allowCarrying: 0 } });
await wait(300);
a.inbox.length = 0; b.inbox.length = 0;
send(a, { type: 'CARRY_POS', targetClientId: b.id, pos: [0, 0, 0] });
send(a, { type: 'CARRY_END', targetClientId: b.id, reason: 'put' });
await wait(300);
check(!got(b, 'CARRY_POS') && got(b, 'CARRY_END', (m) => m.reason === 'put'), 'with carrying off again: CARRY_POS is dropped, CARRY_END still goes through');

a.ws.close(); b.ws.close(); c.ws.close();
console.log(failures ? `${failures} failed` : 'all passed');
process.exit(failures ? 1 : 0);
