#!/usr/bin/env node
// Colo Push — envoi FCM data-only, sans contenu de liste.
// Un seul endpoint public : POST /push { app, topic }.
import http from 'node:http';
import crypto from 'node:crypto';
import fs from 'node:fs';

const PORT = Number(process.env.PUSH_PORT || 7780);
const SA_PATH = process.env.FCM_SERVICE_ACCOUNT
  || '/data/leo/colo-apps/secrets/fcm-service-account.json';
const ALLOWED_APPS = new Set((process.env.ALLOWED_APPS || 'colocourse,colotaches')
  .split(',').map(s => s.trim()).filter(Boolean));
const RATE_MS = Number(process.env.PUSH_RATE_MS || 5000);

let sa = null;
try { sa = JSON.parse(fs.readFileSync(SA_PATH, 'utf8')); }
catch (e) { console.error('[push] service account illisible:', e.message); }

let token = null;
let tokenExp = 0;
const lastSend = new Map();

function b64url(buf) {
  return Buffer.from(buf).toString('base64url');
}

async function accessToken() {
  if (!sa) throw new Error('service account absent');
  if (token && Date.now() < tokenExp - 60_000) return token;
  const now = Math.floor(Date.now() / 1000);
  const header = b64url(JSON.stringify({ alg: 'RS256', typ: 'JWT' }));
  const claim = b64url(JSON.stringify({
    iss: sa.client_email,
    scope: 'https://www.googleapis.com/auth/firebase.messaging',
    aud: 'https://oauth2.googleapis.com/token',
    iat: now,
    exp: now + 3600,
  }));
  const sig = crypto.sign('RSA-SHA256', Buffer.from(`${header}.${claim}`),
                           sa.private_key).toString('base64url');
  const res = await fetch('https://oauth2.googleapis.com/token', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({
      grant_type: 'urn:ietf:params:oauth:grant-type:jwt-bearer',
      assertion: `${header}.${claim}.${sig}`,
    }),
  });
  const data = await res.json();
  if (!res.ok) throw new Error(`oauth ${res.status}: ${data.error || 'inconnu'}`);
  token = data.access_token;
  tokenExp = Date.now() + (data.expires_in || 3600) * 1000;
  return token;
}

async function sendFcm(topic, app) {
  const at = await accessToken();
  const res = await fetch(
    `https://fcm.googleapis.com/v1/projects/${sa.project_id}/messages:send`, {
      method: 'POST',
      headers: {
        Authorization: `Bearer ${at}`,
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        message: {
          topic,
          data: { app, kind: 'sync' }, // data-only, aucun contenu
          android: { priority: 'high' },
        },
      }),
    });
  const body = await res.text();
  if (!res.ok) throw new Error(`fcm ${res.status}: ${body.slice(0, 200)}`);
  return body;
}

function json(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, { 'Content-Type': 'application/json',
                        'Content-Length': Buffer.byteLength(body) });
  res.end(body);
}

const server = http.createServer((req, res) => {
  if (req.method === 'GET' && req.url === '/healthz') {
    return json(res, 200, { ok: true, fcm: !!sa, project: sa?.project_id || null });
  }
  if (req.method === 'GET' && req.url === '/config') {
    return json(res, 200, { notifications: { fcm: !!sa, unifiedpush: false } });
  }
  if (req.method !== 'POST' || req.url !== '/push') {
    return json(res, 404, { error: 'not found' });
  }
  let body = '';
  req.on('data', chunk => {
    body += chunk;
    if (body.length > 4096) req.destroy();
  });
  req.on('end', async () => {
    try {
      const data = JSON.parse(body || '{}');
      const app = String(data.app || '');
      const topic = String(data.topic || '');
      if (!ALLOWED_APPS.has(app)) return json(res, 400, { error: 'app' });
      if (!/^colo-[a-f0-9]{32}$/.test(topic)) return json(res, 400, { error: 'topic' });
      const now = Date.now();
      if (now - (lastSend.get(topic) || 0) < RATE_MS)
        return json(res, 429, { error: 'rate' });
      lastSend.set(topic, now);
      await sendFcm(topic, app);
      return json(res, 202, { ok: true });
    } catch (e) {
      console.error('[push]', e.message);
      return json(res, 502, { error: 'fcm' });
    }
  });
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`[push] listening 127.0.0.1:${PORT} fcm=${!!sa} project=${sa?.project_id || '-'}`);
});
