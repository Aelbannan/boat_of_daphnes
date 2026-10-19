// Tests the content store of a running server (npm run dev -- --port 8799, then
// node test/content.mjs http://127.0.0.1:8799). Needs Node 22+.
// Covers what the store takes and refuses (docs/fork/anchor-content-security.md): archives, JaiInit.aaf, wave banks,
// manifests; malformed, truncated, oversized, traversal and decompression-bomb inputs.
import { createHash } from 'node:crypto';
import { checkAaf, checkArchive, checkManifest, checkWave } from '../src/content.js';
const base = process.argv[2] || 'http://127.0.0.1:8799';
const sha = (b) => createHash('sha256').update(b).digest('hex');
let failures = 0;
const check = (name, ok, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name} ${ok ? '' : extra}`); if (!ok) failures++; };
const put = (hash, body, kind) => fetch(`${base}/content/${hash}${kind ? `?kind=${kind}` : ''}`, { method: 'PUT', body });

// ---- builders (the same as tests/content_validate_test.cpp's)
const be32 = (b, at, v) => b.writeUInt32BE(v >>> 0, at);
function j3d(magic, chunks = 2) {
  const b = Buffer.alloc(0x20 + chunks * 0x20);
  b.write(magic, 0, 'latin1');
  be32(b, 8, b.length);
  be32(b, 12, chunks);
  for (let i = 0; i < chunks; i++) { b.write('VTX1', 0x20 + i * 0x20, 'latin1'); be32(b, 0x20 + i * 0x20 + 4, 0x20); }
  return b;
}
function rarc(members) {
  let strings = 'root\0';
  const nameAt = [];
  for (const m of members) { nameAt.push(strings.length); strings += `${m.name}\0`; }
  const nodeOff = 0x20, entryOff = nodeOff + 0x10;
  const stringsOff = (entryOff + members.length * 0x14 + 31) & ~31;
  const dataOff = (stringsOff + strings.length + 31) & ~31;
  const offsets = [];
  let data = Buffer.alloc(0);
  for (const m of members) { offsets.push(data.length); data = Buffer.concat([data, m.data, Buffer.alloc((32 - (m.data.length % 32)) % 32)]); }
  const b = Buffer.alloc(0x20 + dataOff + data.length);
  b.write('RARC', 0, 'latin1');
  be32(b, 4, b.length); be32(b, 8, 0x20); be32(b, 0x0c, dataOff); be32(b, 0x10, data.length); be32(b, 0x14, data.length);
  be32(b, 0x20, 1); be32(b, 0x24, nodeOff); be32(b, 0x28, members.length); be32(b, 0x2c, entryOff); be32(b, 0x30, strings.length); be32(b, 0x34, stringsOff);
  b.write('ROOT', 0x20 + nodeOff, 'latin1');
  b.writeUInt16BE(members.length, 0x20 + nodeOff + 0x0a);
  members.forEach((m, i) => {
    const e = 0x20 + entryOff + i * 0x14;
    b.writeUInt16BE(i, e); b[e + 4] = m.type ?? 0x11; b.writeUInt16BE(nameAt[i], e + 6); be32(b, e + 8, offsets[i]); be32(b, e + 12, m.data.length);
  });
  b.write(strings, 0x20 + stringsOff, 'latin1');
  data.copy(b, 0x20 + dataOff);
  return b;
}
const goodArc = () => rarc([{ name: 'cl.bdl', data: j3d('J3D2bdl4') }, { name: 'swa.bdl', data: j3d('J3D2bdl3', 3) }, { name: 'bomb.bck', data: j3d('J3D1bck1', 1) }]);
const yaz0 = (declared, payload) => { const b = Buffer.alloc(16 + payload, 7); b.write('Yaz0', 0, 'latin1'); be32(b, 4, declared); return b; };
const aafWords = (words, pad = 0x40) => { const b = Buffer.alloc(Math.max(pad, words.length * 4)); words.forEach((w, i) => be32(b, i * 4, w)); return b; };
const ab = (b) => b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);

// ---- the validators, offline
check('a good archive passes', checkArchive(ab(goodArc())) === null, checkArchive(ab(goodArc())));
let truncated = 0;
const good = goodArc();
for (let n = 0; n < good.length; n += 3) if (checkArchive(ab(good.subarray(0, n))) === null) truncated++;
check('no truncated archive passes', truncated === 0, `${truncated} passed`);
for (const name of ['../evil.bdl', 'a/b.bdl', 'evil.exe', 'run.bat', 'x.py', 'a b.bdl'])
  check(`archive member "${name}" is refused`, checkArchive(ab(rarc([{ name: 'cl.bdl', data: j3d('J3D2bdl4') }, { name, data: j3d('J3D2bdl4') }]))) !== null);
for (const type of [0x15, 0x31, 0x51, 0x91])
  check(`archive member flag ${type.toString(16)} is refused`, checkArchive(ab(rarc([{ name: 'cl.bdl', data: j3d('J3D2bdl4'), type }]))) !== null);
check('a member that is not J3D is refused', checkArchive(ab(rarc([{ name: 'cl.bdl', data: Buffer.alloc(0x80, 0x41) }]))) !== null);
check('a Yaz0 declaring 4 GB is refused', checkArchive(ab(yaz0(0xffffffff, 64))) !== null);
check('a Yaz0 declaring 80000 times its size is refused', checkArchive(ab(yaz0(8 * 1024 * 1024, 100))) !== null);
check('a small Yaz0 passes', checkArchive(ab(yaz0(4096, 64))) === null);
check('an executable is refused', checkArchive(ab(Buffer.from('MZ' + 'x'.repeat(100)))) !== null);
const aaf = aafWords([1, 0x40, 0x20, 0, 2, 0x40, 0x10, 0, 0, 0], 0x80);
check('a good JaiInit.aaf passes', checkAaf(ab(aaf)) === null, checkAaf(ab(aaf)));
check('an aaf record outside the file is refused', checkAaf(ab(aafWords([1, 0x7ffffff0, 0x20, 0, 0, 0]))) !== null);
check('an unterminated aaf is refused', checkAaf(ab(Buffer.alloc(0x100, 0xff))) !== null);
check('a wave bank: 32-byte multiples only', checkWave(ab(Buffer.alloc(64))) === null && checkWave(ab(Buffer.alloc(63))) !== null && checkWave(ab(Buffer.alloc(9 * 1024 * 1024))) !== null);
const h0 = 'a'.repeat(64);
const man = (files, extra = {}) => JSON.stringify({ v: 1, name: 'T', files, ...extra });
const f = (path, role, size = 10) => ({ path, role, sha256: h0, size });
check('a manifest with parts passes', checkManifest(man([f('Link.arc', 'model'), f('equipment/Link.arc', 'equipment'), f('sound/JaiInit.aaf', 'voice'), f('sound/voice_0.aw', 'voice')], { parts: { link: true, equipment: 'equipment', voice: true } })) === null);
for (const bad of ['../Link.arc', '/Link.arc', 'a/../b', 'a\\b', 'C:/x', 'a//b', 'a/./b', 'sound/x.exe'])
  check(`manifest path "${bad}" is refused`, checkManifest(man([f(bad, bad.startsWith('sound') ? 'voice' : 'model')])) !== null);
check('a role that does not fit its path is refused', checkManifest(man([f('Link.arc', 'voice')])) !== null);
check('an unknown role is refused', checkManifest(man([f('Link.arc', 'script')])) !== null);
check('an unknown equipment part is refused', checkManifest(man([f('Link.arc', 'model')], { parts: { equipment: '../x' } })) !== null);

// ---- the running server
if (!(await fetch(`${base}/content/${'0'.repeat(64)}`).then((r) => r.status).catch(() => 0))) { console.log('no server at ' + base + ': only the offline checks ran'); process.exit(failures ? 1 : 0); }
const blob = good;
const h = sha(blob);
check('HEAD before: 404', (await fetch(`${base}/content/${h}`, { method: 'HEAD' })).status === 404);
let r = await put(h, blob);
check('PUT blob: 201', r.status === 201, r.status);
r = await put(h, blob);
check('PUT again: 204 (already there)', r.status === 204, r.status);
check('HEAD after: 200', (await fetch(`${base}/content/${h}`, { method: 'HEAD' })).status === 200);
const resp = await fetch(`${base}/content/${h}`);
const got = Buffer.from(await resp.arrayBuffer());
check('GET returns the bytes', got.equals(blob));
check('GET is served as a download that is not sniffed', resp.headers.get('x-content-type-options') === 'nosniff' && resp.headers.get('content-disposition') === 'attachment' && resp.headers.get('content-type') === 'application/octet-stream');
r = await put(sha(Buffer.from('other')), blob);
check('PUT with the wrong hash: 400', r.status === 400, r.status);
check('GET unknown: 404', (await fetch(`${base}/content/${'0'.repeat(64)}`)).status === 404);
check('bad name: 400', (await fetch(`${base}/content/xyz`)).status === 400);
check('a name with a path in it: 400', (await fetch(`${base}/content/..%2f..%2fetc%2fpasswd`)).status === 400);
const big = Buffer.concat([Buffer.from('RARC'), Buffer.alloc(21 * 1024 * 1024, 7)]);
r = await put(sha(big), big);
check('PUT over 20 MB: 413', r.status === 413, r.status);
const evilArc = rarc([{ name: 'cl.bdl', data: j3d('J3D2bdl4') }, { name: 'evil.exe', data: j3d('J3D2bdl4') }]);
r = await put(sha(evilArc), evilArc);
check('PUT of an archive with an exe member: 415', r.status === 415, r.status);
const bomb = yaz0(0xffffffff, 64);
r = await put(sha(bomb), bomb);
check('PUT of a Yaz0 bomb: 415', r.status === 415, r.status);
r = await put(sha(aaf), aaf, 'aaf');
check('PUT a JaiInit.aaf (kind=aaf): 201', r.status === 201, r.status);
const wave = Buffer.alloc(4096, 5);
r = await put(sha(wave), wave, 'wave');
check('PUT a wave bank (kind=wave): 201', r.status === 201, r.status);
r = await put(sha(wave), wave);
check('(already there: 204)', r.status === 204, r.status);
const wave2 = Buffer.alloc(4097, 6);
r = await put(sha(wave2), wave2, 'wave');
check('PUT a wave bank of 4097 bytes: 415', r.status === 415, r.status);
r = await put(sha(wave2), wave2);
check('a wave bank without its kind is refused as an archive: 415', r.status === 415, r.status);
r = await put(sha(Buffer.from('x')), Buffer.from('x'), 'script');
check('an unknown kind: 400', r.status === 400, r.status);

const manifest = (files, extra) => Buffer.from(man(files, extra));
const gm = manifest([{ path: 'Link.arc', role: 'model', sha256: h, size: blob.length }], { parts: { link: true, equipment: 'model', voice: false } });
r = await put(sha(gm), gm, 'manifest');
check('PUT manifest: 201', r.status === 201, r.status);
const evil = manifest([{ path: '../x', role: 'model', sha256: h, size: 1 }]);
r = await put(sha(evil), evil, 'manifest');
check('manifest with a path outside the pack: 400', r.status === 400, r.status);
const huge = manifest([{ path: 'a', sha256: h, size: 15 * 1024 * 1024 }, { path: 'b', sha256: h, size: 15 * 1024 * 1024 }]);
r = await put(sha(huge), huge, 'manifest');
check('manifest over 20 MB total: 400', r.status === 400, r.status);
const junk = Buffer.from('not json');
r = await put(sha(junk), junk, 'manifest');
check('manifest that is not JSON: 400', r.status === 400, r.status);

const notArchive = Buffer.from('MZ this is not a Wind Waker archive at all');
r = await put(sha(notArchive), notArchive);
check('PUT of something that is not RARC or Yaz0: 415', r.status === 415, r.status);

let limited = 0;
for (let i = 0; i < 170 && !limited; i++) {
  const b = Buffer.concat([Buffer.from('Yaz0'), Buffer.from([0, 0, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0]), Buffer.from(`rate ${i} ${Math.random()}`)]);
  if ((await put(sha(b), b)).status === 429) limited = i;
}
check('uploads are rate limited', limited > 0, 'no 429 in 170 uploads');
console.log(failures ? `${failures} failed` : 'all passed');
process.exit(failures ? 1 : 0);
