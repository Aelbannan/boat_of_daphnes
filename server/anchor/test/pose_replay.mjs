// A scripted second player for the puppet scenarios: records the game's PLAYER_UPDATE stream (with its
// "pose" joint matrices) for <record> seconds, then plays it back looped as "Mate", offset 150 units in x,
// 10 times a second. With a pack folder (a wwrando model folder: Link.arc, metadata.txt, color_masks, sound)
// it also uploads that pack to the server's content store and advertises its manifest in its client state,
// so the game downloads it and draws Mate as that model.
// Usage: node test/pose_replay.mjs <ws://host:port> <room> <record seconds> <total seconds> [pack folder]
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';

const [base, room, record, total, pack] = [process.argv[2], process.argv[3], Number(process.argv[4] || 10), Number(process.argv[5] || 90), process.argv[6]];
const http = base.replace(/^ws/, 'http');
const sha = (b) => createHash('sha256').update(b).digest('hex');

async function upload(bytes, kind) {
  const h = sha(bytes);
  if ((await fetch(`${http}/content/${h}`, { method: 'HEAD' })).status === 200) return h;
  const r = await fetch(`${http}/content/${h}${kind ? `?kind=${kind}` : ''}`, { method: 'PUT', body: bytes });
  if (r.status !== 201 && r.status !== 204) throw new Error(`upload ${h.slice(0, 12)}: ${r.status} ${await r.text()}`);
  return h;
}

// The pack's manifest: the files a game would share, roles as the game names them.
async function publish(folder) {
  const files = [];
  const walk = (dir) => { for (const n of readdirSync(dir)) { const p = join(dir, n); statSync(p).isDirectory() ? walk(p) : files.push(p); } };
  walk(folder);
  const list = [];
  let totalSize = 0;
  for (const p of files.sort()) {
    const rel = relative(folder, p).replaceAll('\\', '/');
    const low = rel.toLowerCase();
    // The store takes only RARC and Yaz0 files: the model (and equipment archives); the rest stays on the player's PC.
    const role = low === 'link.arc' ? 'model' : low === 'metadata.txt' ? 'metadata' : low.startsWith('color_masks/') && low.endsWith('.png') ? 'colors'
      : low.startsWith('sound/') ? 'voice' : low.startsWith('equipment/') ? 'equipment' : null;
    if (role !== 'model' && !(role === 'equipment' && low.endsWith('.arc'))) continue;
    const bytes = readFileSync(p);
    if (totalSize + bytes.length > 20 * 1024 * 1024) { if (role === 'voice') continue; throw new Error('pack over 20 MB'); }
    totalSize += bytes.length;
    list.push({ path: rel, role, sha256: await upload(bytes), size: bytes.length });
  }
  const manifest = Buffer.from(JSON.stringify({ v: 1, name: folder.split(/[\\/]/).pop(), files: list }));
  const hash = await upload(manifest, 'manifest');
  console.log(`pack ${folder}: ${list.length} files, ${totalSize} bytes, manifest ${hash}`);
  return hash;
}

const content = pack ? await publish(pack) : '';
const ws = new WebSocket(`${base}/ws?room=${room}`);
const frames = [];
let t0 = 0, at = 0, latest = null;
ws.onopen = () => ws.send(JSON.stringify({ type: 'HANDSHAKE', protocol: 1,
  clientState: { name: 'Mate', teamId: 'red', stage: 'sea', room: 44, isSaveLoaded: true, content, voicePitch: Number(process.env.VOICE_PITCH || 0) } }));
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === 'PLAYER_UPDATE' && m.pose) {
    latest = m;
    if (!t0) t0 = Date.now();
    if (Date.now() - t0 < record * 1000) frames.push(m);
  }
  if (m.type === 'REQUEST_TEAM_STATE')
    ws.send(JSON.stringify({ type: 'UPDATE_TEAM_STATE', state: [], targetClientId: m.clientId }));
};
// The recording again and again, moved so it starts where the game's Link now is, 150 units to his side.
setInterval(() => {
  if (!frames.length || Date.now() - t0 < record * 1000 || !latest) return;
  const f = frames[at++ % frames.length];
  const first = frames[0];
  const pose = Buffer.from(f.pose, 'base64');
  const delta = [0, 1, 2].map((i) => f.pos[i] - first.pos[i] + latest.pos[i] + (i === 0 ? 150 : 0) - f.pos[i]);
  for (let i = 0; i < 3; i++) pose.writeFloatLE(pose.readFloatLE(6 + 4 * i) + delta[i], 6 + 4 * i); // base position, wire byte 6
  ws.send(JSON.stringify({ type: 'PLAYER_UPDATE', stage: f.stage, room: f.room, pos: [f.pos[0] + delta[0], f.pos[1] + delta[1], f.pos[2] + delta[2]],
    angle: f.angle, pose: pose.toString('base64') }));
}, 100);
setTimeout(() => { ws.close(); process.exit(0); }, total * 1000);
