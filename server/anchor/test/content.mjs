// Tests the content store of a running server (npm run dev -- --port 8799, then
// node test/content.mjs http://127.0.0.1:8799). Needs Node 22+.
import { createHash } from 'node:crypto';
const base = process.argv[2] || 'http://127.0.0.1:8799';
const sha = (b) => createHash('sha256').update(b).digest('hex');
let failures = 0;
const check = (name, ok, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name} ${ok ? '' : extra}`); if (!ok) failures++; };
const put = (hash, body, kind) => fetch(`${base}/content/${hash}${kind ? `?kind=${kind}` : ''}`, { method: 'PUT', body });

const blob = Buffer.concat([Buffer.from('RARC'), Buffer.from('a Link.arc, or something like it: ' + Math.random())]); // the store takes only archives
const h = sha(blob);
check('HEAD before: 404', (await fetch(`${base}/content/${h}`, { method: 'HEAD' })).status === 404);
let r = await put(h, blob);
check('PUT blob: 201', r.status === 201, r.status);
r = await put(h, blob);
check('PUT again: 204 (already there)', r.status === 204, r.status);
check('HEAD after: 200', (await fetch(`${base}/content/${h}`, { method: 'HEAD' })).status === 200);
const got = Buffer.from(await (await fetch(`${base}/content/${h}`)).arrayBuffer());
check('GET returns the bytes', got.equals(blob));
r = await put(sha(Buffer.from('other')), blob);
check('PUT with the wrong hash: 400', r.status === 400, r.status);
check('GET unknown: 404', (await fetch(`${base}/content/${'0'.repeat(64)}`)).status === 404);
check('bad name: 400', (await fetch(`${base}/content/xyz`)).status === 400);
const big = Buffer.concat([Buffer.from('RARC'), Buffer.alloc(21 * 1024 * 1024, 7)]);
r = await put(sha(big), big);
check('PUT over 20 MB: 413', r.status === 413, r.status);

const manifest = (files) => Buffer.from(JSON.stringify({ v: 1, name: 'Test', files }));
const good = manifest([{ path: 'Link.arc', sha256: h, size: blob.length }]);
r = await put(sha(good), good, 'manifest');
check('PUT manifest: 201', r.status === 201, r.status);
const evil = manifest([{ path: '../x', sha256: h, size: 1 }]);
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
  const b = Buffer.from(`RARCrate ${i} ${Math.random()}`);
  if ((await put(sha(b), b)).status === 429) limited = i;
}
check('uploads are rate limited', limited > 0, 'no 429 in 170 uploads');
console.log(failures ? `${failures} failed` : 'all passed');
process.exit(failures ? 1 : 0);
