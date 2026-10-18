// Custom player content (a player's Link model, voice and equipment files), stored by hash.
//
//   PUT  /content/<sha256>[?kind=manifest]   a blob; the server hashes the body and refuses it unless it
//                                            is <sha256>. kind=manifest is also parsed and checked.
//   GET  /content/<sha256>                   the blob (immutable: the name is the hash of the bytes)
//   HEAD /content/<sha256>                   200 if the server has it, 404 if not (so a client uploads
//                                            only what is missing)
//
// A manifest is JSON { v: 1, name, files: [{ path, sha256, size }] } and is itself stored by its hash:
// that hash is what a player advertises in their client state. The server never looks inside a file; clients
// check what they download (and never run it).
//
// Storage: the CONTENT Workers KV namespace (values up to 25 MB; the limit here is 20 MB per file and per
// manifest). Uploads are rate-limited per client address (IP_SALT salts the hash the counter is kept under).

export const MAX_FILE = 20 * 1024 * 1024;
export const MAX_MANIFEST_TOTAL = 20 * 1024 * 1024;
const MAX_MANIFEST_BYTES = 64 * 1024;
const MAX_FILES = 64;
const UPLOADS_PER_HOUR = 150; // a pack of colour masks is dozens of small files
const BYTES_PER_HOUR = 120 * 1024 * 1024;
const HEX64 = /^[0-9a-f]{64}$/;
// Blobs expire after 30 days; a client uploads again what the server no longer has (HEAD first).
const CONTENT_TTL = 30 * 24 * 3600;

// Only what a Link model pack is made of: RARC archives, plain or Yaz0-compressed. Anything else (an
// executable, a page) is refused, so the store is not general file hosting on this domain.
function isGameArchive(body) {
  if (body.byteLength < 16) return false;
  const magic = new TextDecoder().decode(new Uint8Array(body, 0, 4));
  return magic === 'RARC' || magic === 'Yaz0';
}

const cors = { 'access-control-allow-origin': '*', 'access-control-allow-methods': 'GET, HEAD, PUT, OPTIONS', 'access-control-allow-headers': '*' };

function reply(body, status = 200, extra = {}) {
  return new Response(body, { status, headers: { ...cors, ...extra } });
}
function fail(status, message) {
  return reply(JSON.stringify({ error: message }), status, { 'content-type': 'application/json' });
}

async function sha256Hex(buffer) {
  const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', buffer));
  return Array.from(digest, (b) => b.toString(16).padStart(2, '0')).join('');
}

// A manifest's shape: sizes and hashes only, paths that stay inside the pack.
export function checkManifest(text) {
  let m;
  try {
    m = JSON.parse(text);
  } catch {
    return 'the manifest is not JSON';
  }
  if (!m || m.v !== 1 || !Array.isArray(m.files)) return 'the manifest is not version 1';
  if (m.files.length < 1 || m.files.length > MAX_FILES) return `the manifest lists 1 to ${MAX_FILES} files`;
  let total = 0;
  const seen = new Set();
  for (const f of m.files) {
    if (!f || typeof f.path !== 'string' || !HEX64.test(f.sha256) || !Number.isInteger(f.size) || f.size < 0) return 'a file entry is malformed';
    if (f.path.length > 160 || f.path.startsWith('/') || f.path.includes('..') || f.path.includes('\\') || /[\0-\x1f]/.test(f.path)) return 'a file path is not allowed';
    if (f.size > MAX_FILE) return 'a file is over the size limit';
    if (seen.has(f.path.toLowerCase())) return 'a path is listed twice';
    seen.add(f.path.toLowerCase());
    total += f.size;
  }
  if (total > MAX_MANIFEST_TOTAL) return `the pack is over ${MAX_MANIFEST_TOTAL >> 20} MB`;
  return null;
}

// Uploads per client address per hour (the address only as a salted hash).
async function rateLimit(request, env, bytes) {
  const ip = request.headers.get('cf-connecting-ip') || 'local';
  const hour = Math.floor(Date.now() / 3600000);
  const key = `rl:${(await sha256Hex(new TextEncoder().encode(`${env.IP_SALT || ''}|${ip}`))).slice(0, 24)}:${hour}`;
  const used = JSON.parse((await env.CONTENT.get(key)) || '{"n":0,"b":0}');
  if (used.n + 1 > UPLOADS_PER_HOUR || used.b + bytes > BYTES_PER_HOUR) return false;
  await env.CONTENT.put(key, JSON.stringify({ n: used.n + 1, b: used.b + bytes }), { expirationTtl: 7200 });
  return true;
}

export async function handleContent(request, env, url) {
  if (request.method === 'OPTIONS') return reply(null, 204);
  if (!env.CONTENT) return fail(503, 'content storage is not set up on this server');
  const hash = url.pathname.slice('/content/'.length).toLowerCase();
  if (!HEX64.test(hash)) return fail(400, 'the name is a lowercase SHA-256');
  const key = `c:${hash}`;

  if (request.method === 'HEAD') {
    const { value } = await env.CONTENT.getWithMetadata(key, 'stream');
    if (value) await value.cancel();
    return reply(null, value ? 200 : 404);
  }
  if (request.method === 'GET') {
    const body = await env.CONTENT.get(key, 'arrayBuffer');
    if (!body) return fail(404, 'unknown content');
    return reply(body, 200, { 'content-type': 'application/octet-stream', 'cache-control': 'public, max-age=31536000, immutable' });
  }
  if (request.method === 'PUT') {
    const declared = Number(request.headers.get('content-length') || '-1');
    if (declared > MAX_FILE) return fail(413, `over ${MAX_FILE >> 20} MB`);
    if (await env.CONTENT.get(key, 'stream').then((v) => (v ? v.cancel().then(() => true) : false))) return reply(null, 204); // already here
    const body = await request.arrayBuffer();
    if (body.byteLength > MAX_FILE) return fail(413, `over ${MAX_FILE >> 20} MB`);
    if (body.byteLength === 0) return fail(400, 'empty');
    if ((await sha256Hex(body)) !== hash) return fail(400, 'the bytes do not match the hash');
    if (url.searchParams.get('kind') === 'manifest') {
      if (body.byteLength > MAX_MANIFEST_BYTES) return fail(413, 'the manifest is too large');
      const problem = checkManifest(new TextDecoder().decode(body));
      if (problem) return fail(400, problem);
    }
    if (url.searchParams.get('kind') !== 'manifest' && !isGameArchive(body)) {
      return fail(415, 'only Wind Waker archives (RARC, Yaz0) are stored');
    }
    if (!(await rateLimit(request, env, body.byteLength))) return fail(429, 'too many uploads: try again later');
    await env.CONTENT.put(key, body, { expirationTtl: CONTENT_TTL });
    return reply(null, 201);
  }
  return fail(405, 'GET, HEAD or PUT');
}
