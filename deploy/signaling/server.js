/**
 * Colo Sync — serveur P2P (signaling + configuration + credentials TURN)
 *
 * Ne transporte et ne stocke AUCUNE donnée de liste : uniquement la découverte
 * des pairs, l'échange SDP/ICE et des credentials TURN éphémères.
 *
 * Variables d'environnement :
 *   PORT=7778
 *   HOST=127.0.0.1                 (nginx expose /signal)
 *   PUBLIC_URL=https://exemple.org (base des URLs renvoyées par /config)
 *   ALLOWED_APPS=colocourse,colotaches
 *   STUN_URLS=stun:exemple.org:3479
 *   TURN_URLS=turn:exemple.org:3479?transport=udp,...
 *   TURN_SECRET_FILE=/data/.../turn.secret
 *   TURN_TTL=43200
 *
 * Protocole v2 (compatible v1 sans app) :
 *   → register { protocol, app, device_id }
 *   → join { channel_tag }
 *   ← room_state { channel_tag, peers:[{device_id}] }
 *   ← peer_joined { device_id, channel_tag }
 *   → offer/answer/candidate { to, channel_tag, ... }
 *   ← offer/answer/candidate { from, channel_tag, ... }
 *   ← peer_left { device_id, channel_tag }
 *   → ping { t }  ← pong { t }
 */

const http = require("http");
const fs = require("fs");
const crypto = require("crypto");
const { WebSocketServer } = require("ws");

const PORT = parseInt(process.env.PORT || "7778", 10);
const HOST = process.env.HOST || "127.0.0.1";
const PUBLIC_URL = (process.env.PUBLIC_URL || "https://colo-apps.les-crevettes-cevenoles.fr")
  .replace(/\/+$/, "");
const ALLOWED_APPS = new Set(
  (process.env.ALLOWED_APPS || "colocourse,colotaches")
    .split(",").map((s) => s.trim()).filter(Boolean)
);
const STUN_URLS = (process.env.STUN_URLS || "")
  .split(",").map((s) => s.trim()).filter(Boolean);
const TURN_URLS = (process.env.TURN_URLS || "")
  .split(",").map((s) => s.trim()).filter(Boolean);
const TURN_TTL = parseInt(process.env.TURN_TTL || "43200", 10);
const MAX_ROOMS_PER_CLIENT = 32;
const MAX_PEERS_PER_ROOM = 8;
const RATE_LIMIT_PER_SEC = 40;
const TURN_RATE_LIMIT_PER_HOUR = 40;

let turnSecret = null;
if (process.env.TURN_SECRET_FILE) {
  try {
    turnSecret = fs.readFileSync(process.env.TURN_SECRET_FILE, "utf8").trim();
  } catch (err) {
    console.error(`[signaling] TURN secret illisible: ${err.message}`);
  }
}

const metrics = {
  startedAt: Date.now(),
  connections: 0,
  messages: 0,
  rejected: 0,
  turnCredentials: 0,
};

const clients = new Set();
const devices = new Map(); // `${app}:${deviceId}` -> client
const rooms = new Map();   // `${app}:${channel}` -> Map(deviceId, client)
const turnRate = new Map(); // ip -> { count, reset }

function log(...args) {
  console.log(new Date().toISOString(), ...args);
}

function hash(value) {
  return crypto.createHash("sha256").update(String(value)).digest("hex").slice(0, 12);
}

function appOf(client) {
  return client.app || "colocourse";
}

function roomKey(client, channel) {
  return `${appOf(client)}:${channel}`;
}

function send(ws, msg) {
  if (ws && ws.readyState === 1 /* OPEN */) {
    ws.send(JSON.stringify(msg));
  }
}

function sendError(ws, code, message) {
  metrics.rejected++;
  send(ws, { type: "error", code, message });
}

function validDeviceId(id) {
  return typeof id === "string" && /^[A-Za-z0-9_.:-]{1,96}$/.test(id);
}

function validChannel(channel) {
  // Les vrais canaux sont 32 hex ; on tolère les canaux de test historiques.
  return typeof channel === "string" && /^[A-Za-z0-9_.:-]{1,96}$/.test(channel);
}

function roomMembers(key) {
  return rooms.get(key) || new Map();
}

function leaveRooms(client) {
  for (const channel of client.channels) {
    const key = roomKey(client, channel);
    const members = rooms.get(key);
    if (!members) continue;
    members.delete(client.deviceId);
    for (const other of members.values()) {
      send(other.ws, {
        type: "peer_left",
        device_id: client.deviceId,
        channel_tag: channel,
        app: appOf(client),
      });
    }
    if (members.size === 0) rooms.delete(key);
  }
  client.channels.clear();
}

function cleanupClient(client) {
  leaveRooms(client);
  if (client.deviceId) {
    const key = `${appOf(client)}:${client.deviceId}`;
    if (devices.get(key) === client) devices.delete(key);
  }
  clients.delete(client);
}

function handleJoin(client, msg) {
  const channel = msg.channel_tag;
  if (!validChannel(channel)) {
    sendError(client.ws, 400, "channel_tag invalide");
    return;
  }
  if (client.channels.size >= MAX_ROOMS_PER_CLIENT) {
    sendError(client.ws, 429, "trop de canaux");
    return;
  }

  const key = roomKey(client, channel);
  let members = rooms.get(key);
  if (!members) {
    members = new Map();
    rooms.set(key, members);
  }
  if (!members.has(client.deviceId) && members.size >= MAX_PEERS_PER_ROOM) {
    sendError(client.ws, 429, "room pleine");
    return;
  }

  const existingClients = [...members.values()]
    .filter((c) => c.deviceId !== client.deviceId);
  const existing = existingClients.map((c) => ({ device_id: c.deviceId }));

  members.set(client.deviceId, client);
  client.channels.add(channel);

  send(client.ws, { type: "room_state", channel_tag: channel, peers: existing });

  // Notifier les deux côtés. Le client tranche l'initiateur par deviceId,
  // donc pas de glare même si les deux reçoivent peer_joined.
  for (const other of existingClients) {
    send(other.ws, {
      type: "peer_joined",
      device_id: client.deviceId,
      channel_tag: channel,
      app: appOf(client),
    });
  }
  for (const other of existingClients) {
    send(client.ws, {
      type: "peer_joined",
      device_id: other.deviceId,
      channel_tag: channel,
      app: appOf(client),
    });
  }
  log(`[signaling] ${hash(client.deviceId)} joined ${hash(channel)} (${members.size})`);
}

function handleForward(client, msg) {
  const channel = msg.channel_tag;
  if (!validChannel(channel) || !msg.to) {
    sendError(client.ws, 400, "message de signaling incomplet");
    return;
  }
  const key = roomKey(client, channel);
  const members = rooms.get(key);
  if (!members || !members.has(client.deviceId) || !members.has(msg.to)) {
    sendError(client.ws, 403, "pair ou canal inconnu");
    return;
  }
  const target = members.get(msg.to);
  send(target.ws, {
    ...msg,
    from: client.deviceId,
    app: appOf(client),
  });
  metrics.messages++;
}

function handleMessage(client, raw) {
  if (!client.rate) client.rate = { count: 0, reset: Date.now() + 1000 };
  if (Date.now() > client.rate.reset) {
    client.rate.count = 0;
    client.rate.reset = Date.now() + 1000;
  }
  if (++client.rate.count > RATE_LIMIT_PER_SEC) {
    sendError(client.ws, 429, "trop de messages");
    return;
  }

  let msg;
  try {
    msg = JSON.parse(raw.toString());
  } catch {
    return;
  }
  if (!msg || typeof msg !== "object" || typeof msg.type !== "string") return;
  metrics.messages++;

  switch (msg.type) {
    case "register": {
      const app = msg.app || "colocourse";
      const protocol = msg.protocol || 1;
      if (!ALLOWED_APPS.has(app)) {
        sendError(client.ws, 403, `application non autorisée: ${app}`);
        client.ws.close(4003, "app not allowed");
        return;
      }
      if (protocol > 2) {
        sendError(client.ws, 426, `protocole ${protocol} non supporté`);
        return;
      }
      if (!validDeviceId(msg.device_id)) {
        sendError(client.ws, 400, "device_id invalide");
        return;
      }
      client.app = app;
      client.protocol = protocol;
      client.deviceId = msg.device_id;
      client.lastSeen = Date.now();
      devices.set(`${app}:${client.deviceId}`, client);
      send(client.ws, {
        type: "registered",
        device_id: client.deviceId,
        app,
        protocol: 2,
      });
      log(`[signaling] registered ${app}/${hash(client.deviceId)}`);
      return;
    }
    case "join":
      if (!client.deviceId) return sendError(client.ws, 401, "register d'abord");
      handleJoin(client, msg);
      return;
    case "offer":
    case "answer":
    case "candidate":
      if (!client.deviceId) return sendError(client.ws, 401, "register d'abord");
      handleForward(client, msg);
      return;
    case "leave": {
      const channel = msg.channel_tag;
      if (!channel) return;
      const key = roomKey(client, channel);
      const members = rooms.get(key);
      if (members) {
        members.delete(client.deviceId);
        for (const other of members.values()) {
          send(other.ws, {
            type: "peer_left",
            device_id: client.deviceId,
            channel_tag: channel,
            app: appOf(client),
          });
        }
        if (members.size === 0) rooms.delete(key);
      }
      client.channels.delete(channel);
      return;
    }
    case "ping":
      send(client.ws, { type: "pong", t: msg.t });
      return;
    case "pong":
      client.lastSeen = Date.now();
      return;
    default:
      sendError(client.ws, 400, `type inconnu: ${msg.type}`);
  }
}

function httpJson(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store",
    "Content-Length": Buffer.byteLength(body),
  });
  res.end(body);
}

function handleHttp(req, res) {
  const url = new URL(req.url || "/", `http://${req.headers.host || "localhost"}`);

  if (url.pathname === "/healthz") {
    return httpJson(res, 200, {
      ok: true,
      name: "colosync",
      uptime: Math.floor((Date.now() - metrics.startedAt) / 1000),
      rooms: rooms.size,
      peers: clients.size,
      turn: Boolean(turnSecret && TURN_URLS.length > 0),
    });
  }

  if (url.pathname === "/metrics") {
    const body = [
      `colosync_connections_total ${metrics.connections}`,
      `colosync_messages_total ${metrics.messages}`,
      `colosync_rejected_total ${metrics.rejected}`,
      `colosync_turn_credentials_total ${metrics.turnCredentials}`,
      `colosync_rooms ${rooms.size}`,
      `colosync_peers ${clients.size}`,
      "",
    ].join("\n");
    res.writeHead(200, { "Content-Type": "text/plain; version=0.0.4" });
    return res.end(body);
  }

  if (url.pathname === "/config") {
    return httpJson(res, 200, {
      name: "Colo Sync",
      protocol: 2,
      apps: [...ALLOWED_APPS],
      signaling: `${PUBLIC_URL}/signal`,
      turnCredentials: `${PUBLIC_URL}/turn-credentials`,
      iceServers: [...STUN_URLS, ...TURN_URLS],
      turn: Boolean(turnSecret && TURN_URLS.length > 0),
    });
  }

  if (url.pathname === "/turn-credentials") {
    if (!turnSecret || TURN_URLS.length === 0) {
      return httpJson(res, 503, { error: "TURN non configuré sur ce serveur" });
    }
    const app = url.searchParams.get("app") || "colocourse";
    const device = url.searchParams.get("device_id") || "";
    if (!ALLOWED_APPS.has(app) || !validDeviceId(device)) {
      return httpJson(res, 400, { error: "app ou device_id invalide" });
    }

    const ip = req.headers["x-real-ip"]
      || (req.headers["x-forwarded-for"] || "").split(",")[0].trim()
      || req.socket.remoteAddress
      || "unknown";
    const now = Date.now();
    const bucket = turnRate.get(ip) || { count: 0, reset: now + 3600_000 };
    if (now > bucket.reset) {
      bucket.count = 0;
      bucket.reset = now + 3600_000;
    }
    if (++bucket.count > TURN_RATE_LIMIT_PER_HOUR) {
      return httpJson(res, 429, { error: "trop de demandes de credentials" });
    }
    turnRate.set(ip, bucket);

    const expiry = Math.floor(now / 1000) + TURN_TTL;
    const username = `${expiry}:${app}-${device}`;
    const credential = crypto
      .createHmac("sha1", turnSecret)
      .update(username)
      .digest("base64");
    metrics.turnCredentials++;
    return httpJson(res, 200, {
      username,
      credential,
      ttl: TURN_TTL,
      urls: TURN_URLS,
    });
  }

  if (url.pathname === "/signal") {
    // Une requête HTTP simple sur une endpoint WebSocket doit inviter à l'upgrade.
    return httpJson(res, 426, { error: "WebSocket upgrade required" });
  }

  httpJson(res, 200, { ok: true, service: "colosync", config: "/config" });
}

const server = http.createServer(handleHttp);
const wss = new WebSocketServer({ server, maxPayload: 64 * 1024 });

wss.on("connection", (ws, req) => {
  const path = new URL(req.url || "/", "http://localhost").pathname;
  if (path !== "/signal" && path !== "/") {
    ws.close(4004, "path not found");
    return;
  }

  metrics.connections++;
  const client = {
    ws,
    app: null,
    deviceId: null,
    protocol: 1,
    channels: new Set(),
    lastSeen: Date.now(),
    rate: null,
    ip: req.socket.remoteAddress || "unknown",
    alive: true,
  };
  clients.add(client);
  ws.on("pong", () => { client.alive = true; client.lastSeen = Date.now(); });

  ws.on("message", (raw) => handleMessage(client, raw));
  ws.on("close", () => cleanupClient(client));
  ws.on("error", (err) => log(`[signaling] ws error: ${err.message}`));
});

const heartbeat = setInterval(() => {
  for (const client of clients) {
    if (!client.alive) {
      client.ws.terminate();
      continue;
    }
    client.alive = false;
    try { client.ws.ping(); } catch {}
  }
}, 20_000);

wss.on("close", () => clearInterval(heartbeat));

function shutdown(signal) {
  log(`[signaling] ${signal}, arrêt`);
  clearInterval(heartbeat);
  for (const client of clients) {
    try { client.ws.close(1001, "server shutdown"); } catch {}
  }
  wss.close(() => server.close(() => process.exit(0)));
  setTimeout(() => process.exit(0), 3000).unref();
}

process.on("SIGTERM", () => shutdown("SIGTERM"));
process.on("SIGINT", () => shutdown("SIGINT"));

server.listen(PORT, HOST, () => {
  log(`[signaling] listening on ${HOST}:${PORT}`);
  log(`[signaling] public=${PUBLIC_URL} apps=${[...ALLOWED_APPS].join(",")} turn=${Boolean(turnSecret && TURN_URLS.length > 0)}`);
});
