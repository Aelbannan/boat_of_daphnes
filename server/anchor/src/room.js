// A room: the players in it, their teams, and what each team has saved. One Durable Object
// per room name, using WebSocket hibernation, so an idle room costs nothing and survives
// eviction (its state is in storage).
//
// Protocol: PROTOCOL.md. In short: every message is a JSON object with a `type`. The server
// handles HANDSHAKE, UPDATE_CLIENT_STATE, UPDATE_ROOM_STATE, UPDATE_TEAM_STATE and
// REQUEST_TEAM_STATE itself; anything else is relayed, to `targetClientId` (one player),
// `targetTeamId` (a team) or the whole room, and stored for the team when `addToQueue` is set.
// PLAYER_UPDATE goes only to players on the same stage.

import { DurableObject } from 'cloudflare:workers';
import { roomLog, roomLogWho } from './room_log.js';

const MAX_PLAYERS = 32;
const OFFLINE_KEEP_MS = 30 * 60 * 1000; // an offline player's entry (their seat, for a reconnect) is kept this long
const OFFLINE_KEEP_MAX = 8;              // and at most this many offline entries, the most recent
const MAX_QUEUE = 2000;          // stored packets per team
const MAX_MESSAGE = 64 * 1024;   // bytes
const PVP_MODES = ['off', 'no_damage', 'on'];

// The player-interaction settings (pvp mode, damage scale, friendly fire, player collision, carrying) as the room keeps them: a value
// out of range or of the wrong kind becomes the default, so no client has to guard against a stranger's JSON.
function cleanInteraction(rs) {
  if ('pvp' in rs && !PVP_MODES.includes(rs.pvp)) rs.pvp = 'off';
  if ('pvpScale' in rs) rs.pvpScale = Math.min(4, Math.max(0.25, Number(rs.pvpScale) || 1));
  if ('pvpFriendlyFire' in rs) rs.pvpFriendlyFire = rs.pvpFriendlyFire ? 1 : 0;
  if ('playerCollision' in rs) rs.playerCollision = rs.playerCollision ? 1 : 0;
  if ('allowCarrying' in rs) rs.allowCarrying = rs.allowCarrying ? 1 : 0;
  return rs;
}

export class Room extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.state = null; // loaded on first use: { nextId, owner, roomState, clients, teams, lobbyKey }
    // Ping without waking the room.
    this.ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('{"type":"PING"}', '{"type":"PONG"}'));
  }

  async load() {
    if (this.state) return this.state;
    this.state = (await this.ctx.storage.get('state')) || {
      nextId: 1,
      owner: 0,
      roomState: { syncItemsAndFlags: 1, teleportMode: 1, showLocationsMode: 1, pvpMode: 0, listed: 0, title: '', pvp: 'off', pvpScale: 1, pvpFriendlyFire: 0, playerCollision: 1, allowCarrying: 0 },
      clients: {}, // id -> client state (online kept current)
      teams: {},   // team -> { snapshot, queue: [] }
      lobbyKey: crypto.randomUUID(),
    };
    return this.state;
  }

  async save() {
    await this.ctx.storage.put('state', this.state);
  }

  // The room log (src/room_log.js): what happens in rooms, for whoever runs the server. The room is named by a short id
  // of its object, never by its name (the name is the room's password). Logging never delays the game.
  log(event, title, fields) {
    roomLog(this, event, title, fields);
  }

  who(c, id, a) {
    return roomLogWho(c, id, a);
  }

  // Offline entries: a seat kept for a reconnect, not a list of everyone who ever came. Dropped: entries offline longer
  // than OFFLINE_KEEP_MS (or from before offline times were kept), entries with the name of a player who has just
  // joined (their own stale seats from older builds, which sent no install token), and all but the OFFLINE_KEEP_MAX
  // most recent. The owner is never an offline entry here (left() moves it on).
  static prune(s, joinedName) {
    const now = Date.now();
    const offline = Object.entries(s.clients).filter(([, c]) => !c.online);
    const drop = new Set();
    for (const [id, c] of offline) {
      if (!c.leftAt || now - c.leftAt > OFFLINE_KEEP_MS) drop.add(id);
      else if (joinedName && c.name === joinedName) drop.add(id);
    }
    const kept = offline.filter(([id]) => !drop.has(id)).sort(([, x], [, y]) => (y.leftAt || 0) - (x.leftAt || 0));
    for (const [id] of kept.slice(OFFLINE_KEEP_MAX)) drop.add(id);
    for (const id of drop) {
      delete s.clients[id];
      if (s.tokens) delete s.tokens[id];
    }
    return drop.size;
  }

  static minutes(ms) {
    const m = Math.round(ms / 60000);
    return m < 60 ? `${m} min` : `${Math.floor(m / 60)} h ${m % 60} min`;
  }

  async fetch(request) {
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({
      clientId: 0,
      roomName: request.headers.get('x-room-name') || '',
      colo: request.headers.get('x-colo') || '',
      country: request.headers.get('x-country') || '',
    });
    return new Response(null, { status: 101, webSocket: client });
  }

  sockets() {
    return this.ctx.getWebSockets().map((ws) => ({ ws, a: ws.deserializeAttachment() || {} }));
  }

  send(ws, message) {
    try {
      ws.send(typeof message === 'string' ? message : JSON.stringify(message));
    } catch {
      // closed under us; webSocketClose tidies up
    }
  }

  team(name) {
    const teams = this.state.teams;
    if (!teams[name]) teams[name] = { snapshot: null, queue: [] };
    return teams[name];
  }

  broadcastClients() {
    const clients = Object.entries(this.state.clients).map(([id, c]) => ({ ...c, clientId: Number(id) }));
    for (const { ws, a } of this.sockets()) {
      if (!a.clientId) continue;
      this.send(ws, {
        type: 'ALL_CLIENT_STATE',
        state: clients.map((c) => ({ ...c, self: c.clientId === a.clientId })),
      });
    }
  }

  async reportToLobby() {
    const s = this.state;
    const online = Object.values(s.clients).filter((c) => c.online);
    const body = {
      key: s.lobbyKey,
      listed: !!s.roomState.listed && online.length > 0,
      title: String(s.roomState.title || '').slice(0, 48),
      players: online.length,
      teams: new Set(online.map((c) => c.teamId || 'default')).size,
    };
    try {
      const lobby = this.env.LOBBY.get(this.env.LOBBY.idFromName('lobby'));
      await lobby.fetch('https://lobby/report', { method: 'POST', body: JSON.stringify(body) });
    } catch {
      // the list is a convenience; a room works without it
    }
  }

  async webSocketMessage(ws, data) {
    if (typeof data !== 'string') return; // binary frames are reserved for a later pose format
    if (data.length > MAX_MESSAGE) return this.send(ws, { type: 'ERROR', message: 'message too large' });
    let msg;
    try {
      msg = JSON.parse(data);
    } catch {
      return this.send(ws, { type: 'ERROR', message: 'not JSON' });
    }
    if (!msg || typeof msg.type !== 'string') return;
    const s = await this.load();
    const a = ws.deserializeAttachment() || {};

    if (msg.type === 'HANDSHAKE') return this.handshake(ws, a, msg);
    if (!a.clientId) return this.send(ws, { type: 'ERROR', message: 'handshake first' });
    const me = s.clients[a.clientId];
    msg.clientId = a.clientId;

    switch (msg.type) {
      case 'UPDATE_CLIENT_STATE':
        s.clients[a.clientId] = { ...me, ...(msg.state || {}), online: true };
        await this.save();
        this.broadcastClients();
        if (s.roomState.listed) await this.reportToLobby();
        return;
      case 'UPDATE_ROOM_STATE':
        if (a.clientId !== s.owner) return this.send(ws, { type: 'ERROR', message: 'only the room owner changes the room' });
        s.roomState = cleanInteraction({ ...s.roomState, ...(msg.roomState || {}) });
        await this.save();
        for (const { ws: other } of this.sockets()) this.send(other, { type: 'UPDATE_ROOM_STATE', roomState: s.roomState, ownerClientId: s.owner });
        await this.reportToLobby();
        return;
      case 'UPDATE_TEAM_STATE': {
        // "I have everything the team has": store it as the team's state; the queue is now in it.
        const t = this.team(me.teamId || 'default');
        if (msg.state !== undefined) t.snapshot = msg.state;
        t.queue = [];
        await this.save();
        // A teammate's answer to REQUEST_TEAM_STATE goes on to the one who asked.
        if (msg.targetClientId) return this.relay(ws, a, msg, me);
        return;
      }
      case 'REQUEST_TEAM_STATE': {
        const teamId = me.teamId || 'default';
        const mate = this.sockets().find(({ a: o }) => o.clientId && o.clientId !== a.clientId &&
          s.clients[o.clientId] && s.clients[o.clientId].online && (s.clients[o.clientId].teamId || 'default') === teamId &&
          s.clients[o.clientId].isSaveLoaded);
        if (mate) return this.send(mate.ws, msg); // a teammate in game answers with UPDATE_TEAM_STATE to us
        const t = this.team(teamId);
        return this.send(ws, { type: 'UPDATE_TEAM_STATE', state: t.snapshot, queue: t.queue, clientId: 0, targetClientId: a.clientId });
      }
      case 'PLAYER_UPDATE': {
        // To the players on the same stage only.
        const where = me.stage;
        for (const { ws: other, a: o } of this.sockets())
          if (o.clientId && o.clientId !== a.clientId && s.clients[o.clientId] && s.clients[o.clientId].stage === where)
            this.send(other, msg);
        return;
      }
      case 'PVP_HIT':
        // One player hits another (the attacker's game says so, the target's takes it): only to the target, and only
        // while the room has PvP on, so a client that ignores the setting still cannot hit anyone.
        if (!msg.targetClientId || !s.roomState.pvp || s.roomState.pvp === 'off') return;
        return this.relay(ws, a, msg, me);
      case 'CARRY_START':
      case 'CARRY_POS':
        // One player lifts another (the carrier's game says so, the carried player's takes it): only to the carried
        // player, and only while the room allows carrying. CARRY_END is always relayed, so a hold can always be let go.
        if (!msg.targetClientId || !s.roomState.allowCarrying) return;
        return this.relay(ws, a, msg, me);
      default:
        return this.relay(ws, a, msg, me);
    }
  }

  async handshake(ws, a, msg) {
    const s = await this.load();
    const protocols = String(this.env.PROTOCOLS || '1').split(',').map((v) => Number(v));
    if (!protocols.includes(Number(msg.protocol))) {
      this.send(ws, { type: 'ERROR', message: `protocol ${msg.protocol} is not this server's (${protocols.join(', ')})` });
      this.log('refused', 'Join refused: protocol', [{ name: 'Client protocol', value: msg.protocol }, ...this.who(msg.clientState, 0, a)]);
      return ws.close(4000, 'protocol');
    }
    const online = Object.values(s.clients).filter((c) => c.online).length;
    // A reconnect keeps its id (and seat) if it was this room's. A game that sends its install's token (kept private
    // here, never in the client list) gets its earlier seat back after a crash or a restart too, instead of a new id
    // beside a stale offline entry (rooms filled with one player's old entries, 2026-10-17); a token that is not that
    // seat's does not take it.
    const token = typeof msg.token === 'string' ? msg.token.slice(0, 64) : '';
    s.tokens = s.tokens || {};
    let id = Number(msg.clientId) || 0;
    if (id && token && s.tokens[id] && s.tokens[id] !== token) id = 0;
    if (!id && token) {
      const mine = Object.keys(s.tokens).find((k) => s.tokens[k] === token && s.clients[k]);
      if (mine) id = Number(mine);
    }
    if (!id || !s.clients[id]) {
      if (online >= MAX_PLAYERS) {
        this.send(ws, { type: 'ERROR', message: 'the room is full' });
        this.log('refused', 'Join refused: room full', this.who(msg.clientState, 0, a));
        return ws.close(4001, 'full');
      }
      id = s.nextId++;
    }
    for (const { ws: other, a: o } of this.sockets()) if (other !== ws && o.clientId === id) other.close(4002, 'replaced');
    const wasOnline = !!(s.clients[id] && s.clients[id].online);
    a.clientId = id;
    ws.serializeAttachment(a);
    s.clients[id] = { ...(s.clients[id] || {}), ...(msg.clientState || {}), online: true };
    delete s.clients[id].leftAt;
    if (token) s.tokens[id] = token;
    Room.prune(s, s.clients[id].name);
    if (!s.owner || !s.clients[s.owner] || !s.clients[s.owner].online) {
      s.owner = id;
      if (msg.roomState && typeof msg.roomState === 'object') s.roomState = cleanInteraction({ ...s.roomState, ...msg.roomState });
    }
    const nowOnline = Object.values(s.clients).filter((c) => c.online).length;
    if (online === 0 && !wasOnline) {
      s.openedAt = Date.now();
      s.peak = 1;
    }
    s.peak = Math.max(s.peak || 0, nowOnline);
    await this.save();
    this.send(ws, { type: 'UPDATE_ROOM_STATE', roomState: s.roomState, ownerClientId: s.owner });
    this.broadcastClients();
    await this.reportToLobby();
    if (!wasOnline) {
      if (online === 0) this.log('opened', 'Room opened', [...this.who(s.clients[id], id, a), { name: 'Listed', value: s.roomState.listed ? 'yes' : 'no', inline: true }]);
      else this.log('joined', 'Player joined', this.who(s.clients[id], id, a));
    }
  }

  async relay(ws, a, msg, me) {
    const s = this.state;
    const teamId = me.teamId || 'default';
    if (msg.addToQueue) {
      const t = this.team(msg.targetTeamId || teamId);
      t.queue.push(JSON.stringify(msg));
      if (t.queue.length > MAX_QUEUE) t.queue.splice(0, t.queue.length - MAX_QUEUE);
      await this.save();
    }
    const text = JSON.stringify(msg);
    for (const { ws: other, a: o } of this.sockets()) {
      if (!o.clientId || o.clientId === a.clientId) continue;
      const c = s.clients[o.clientId];
      if (!c || !c.online) continue;
      if (msg.targetClientId && o.clientId !== Number(msg.targetClientId)) continue;
      if (msg.targetTeamId && (c.teamId || 'default') !== msg.targetTeamId) continue;
      this.send(other, text);
    }
  }

  async webSocketClose(ws) {
    await this.left(ws);
  }

  async webSocketError(ws) {
    await this.left(ws);
  }

  async left(ws) {
    const s = await this.load();
    const a = ws.deserializeAttachment() || {};
    if (!a.clientId || !s.clients[a.clientId]) return;
    const stillHere = this.sockets().some(({ ws: other, a: o }) => other !== ws && o.clientId === a.clientId);
    if (stillHere) return;
    s.clients[a.clientId].online = false;
    s.clients[a.clientId].leftAt = Date.now();
    Room.prune(s);
    const remaining = Object.values(s.clients).filter((c) => c.online).length;
    if (remaining === 0)
      this.log('closed', 'Room closed', [
        ...this.who(s.clients[a.clientId], a.clientId, a),
        { name: 'Open for', value: s.openedAt ? Room.minutes(Date.now() - s.openedAt) : '?', inline: true },
        { name: 'Peak players', value: s.peak || 1, inline: true },
      ]);
    else this.log('left', 'Player left', this.who(s.clients[a.clientId], a.clientId, a));
    if (s.owner === a.clientId) {
      const next = Object.entries(s.clients).find(([, c]) => c.online);
      s.owner = next ? Number(next[0]) : 0;
    }
    await this.save();
    this.broadcastClients();
    for (const { ws: other } of this.sockets()) if (other !== ws) this.send(other, { type: 'UPDATE_ROOM_STATE', roomState: s.roomState, ownerClientId: s.owner });
    await this.reportToLobby();
  }
}
