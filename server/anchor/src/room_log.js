// The room log: what happens in rooms (opened, joined, left, closed, refused), for whoever runs the server.
// room.js calls these for each event. This build keeps no log; a server operator can send the events wherever
// they like here (for example to their own monitoring), rate-limited per room.

export function roomLog(room, event, title, fields) {}

export function roomLogWho(c, id, a) {
  return [];
}
