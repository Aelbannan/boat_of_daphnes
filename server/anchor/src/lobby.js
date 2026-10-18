// The room list: rooms whose owner asked for them to be listed report their title and
// player count here; /rooms reads it. Entries a room has not refreshed for two minutes drop.

import { DurableObject } from 'cloudflare:workers';

const STALE_MS = 2 * 60 * 1000;

export class Lobby extends DurableObject {
  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/list') {
      const rooms = (await this.ctx.storage.get('rooms')) || {};
      const now = Date.now();
      const list = Object.values(rooms)
        .filter((r) => now - r.updated < STALE_MS)
        .map(({ title, players, teams, updated }) => ({ title, players, teams, updated }));
      return new Response(JSON.stringify({ rooms: list }), {
        headers: { 'content-type': 'application/json', 'access-control-allow-origin': '*' },
      });
    }
    if (url.pathname === '/report' && request.method === 'POST') {
      const report = await request.json();
      const rooms = (await this.ctx.storage.get('rooms')) || {};
      if (report.listed) rooms[report.key] = { ...report, updated: Date.now() };
      else delete rooms[report.key];
      await this.ctx.storage.put('rooms', rooms);
      return new Response('ok');
    }
    return new Response('not found', { status: 404 });
  }
}
