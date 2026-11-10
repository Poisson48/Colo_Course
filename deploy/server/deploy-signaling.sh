#!/usr/bin/env bash
# Installe / met à jour le signaling Colo Sync sur le vhost colo-apps.
# Idempotent, ne remplace JAMAIS le vhost : patch-nginx.py insère seulement les
# locations /signal, /turn-credentials, /config, /healthz.
#
# Usage : sudo DOMAIN=... PUBLIC_IP=... PRIVATE_IP=... ./deploy/server/deploy-signaling.sh
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
DOMAIN="${DOMAIN:-colo-apps.les-crevettes-cevenoles.fr}"
COLO_DATA_DIR="${COLO_DATA_DIR:-/data/leo/colo-apps}"
PUBLIC_URL="${PUBLIC_URL:-https://${DOMAIN}}"
ALLOWED_APPS="${ALLOWED_APPS:-colocourse,colotaches}"
STUN_URLS="${STUN_URLS:-stun:${DOMAIN}:3479}"
TURN_URLS="${TURN_URLS:-turn:${DOMAIN}:3479?transport=udp,turn:${DOMAIN}:3479?transport=tcp,turns:${DOMAIN}:5350?transport=tcp}"
PORT="${PORT:-7778}"
SERVICE_USER="${SERVICE_USER:-colosync}"
SIGNALING_DIR="${COLO_DATA_DIR}/signaling"
SECRET_FILE="${COLO_DATA_DIR}/turn.secret"
NGINX_CONF="${NGINX_CONF:-/etc/nginx/sites-available/colo-apps}"
SYSTEMD_UNIT="/etc/systemd/system/colosync-signaling.service"

if [[ $EUID -ne 0 ]]; then
  echo "Relancer avec sudo : sudo $0" >&2
  exit 1
fi

if [[ ! -f "$REPO_DIR/deploy/signaling/server.js" ]]; then
  echo "server.js introuvable dans $REPO_DIR/deploy/signaling" >&2
  exit 1
fi
if [[ ! -f "$NGINX_CONF" ]]; then
  echo "vhost introuvable : $NGINX_CONF" >&2
  exit 1
fi

echo "=== 1/6 — utilisateur et répertoires ==="
if ! id "$SERVICE_USER" >/dev/null 2>&1; then
  useradd --system --home /nonexistent --shell /usr/sbin/nologin "$SERVICE_USER"
fi
install -d -m 750 -o root -g "$SERVICE_USER" "$COLO_DATA_DIR" "$SIGNALING_DIR"
# nginx (www-data) doit traverser $COLO_DATA_DIR pour servir /releases/ ;
# le sous-dossier signaling reste protégé (750 root:colosync).
if command -v setfacl >/dev/null 2>&1; then
  setfacl -m u:www-data:x "$COLO_DATA_DIR"
fi

echo "=== 2/6 — code et dépendances ==="
install -m 640 -o root -g "$SERVICE_USER" "$REPO_DIR/deploy/signaling/server.js" "$SIGNALING_DIR/server.js"
install -m 640 -o root -g "$SERVICE_USER" "$REPO_DIR/deploy/signaling/package.json" "$SIGNALING_DIR/package.json"
if [[ -f "$REPO_DIR/deploy/signaling/package-lock.json" ]]; then
  install -m 640 -o root -g "$SERVICE_USER" "$REPO_DIR/deploy/signaling/package-lock.json" "$SIGNALING_DIR/package-lock.json"
  (cd "$SIGNALING_DIR" && npm ci --omit=dev --silent)
else
  (cd "$SIGNALING_DIR" && npm install --omit=dev --silent)
fi
chown -R root:"$SERVICE_USER" "$SIGNALING_DIR"

echo "=== 3/6 — secret TURN ==="
if [[ ! -s "$SECRET_FILE" ]]; then
  umask 077
  openssl rand -hex 32 > "$SECRET_FILE"
fi
chown root:"$SERVICE_USER" "$SECRET_FILE"
chmod 640 "$SECRET_FILE"

echo "=== 4/6 — environnement et service ==="
ENV_FILE="$SIGNALING_DIR/signaling.env"
if [[ ! -f "$ENV_FILE" ]]; then
  umask 077
  cat > "$ENV_FILE" <<EOF
PORT=${PORT}
HOST=127.0.0.1
PUBLIC_URL=${PUBLIC_URL}
ALLOWED_APPS=${ALLOWED_APPS}
MAX_ROOMS_PER_CLIENT=${MAX_ROOMS_PER_CLIENT}
MAX_PEERS_PER_ROOM=${MAX_PEERS_PER_ROOM}
STUN_URLS=${STUN_URLS}
TURN_URLS=${TURN_URLS}
TURN_SECRET_FILE=${SECRET_FILE}
TURN_TTL=43200
EOF
  chown root:"$SERVICE_USER" "$ENV_FILE"
  chmod 640 "$ENV_FILE"
fi

cat > "$SYSTEMD_UNIT" <<EOF
[Unit]
Description=Colo Sync signaling (Course + Taches)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${SERVICE_USER}
Group=${SERVICE_USER}
WorkingDirectory=${SIGNALING_DIR}
EnvironmentFile=${ENV_FILE}
ExecStart=/usr/bin/node server.js
Restart=always
RestartSec=3
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadOnlyPaths=${SIGNALING_DIR}
MemoryMax=256M

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable colosync-signaling
systemctl restart colosync-signaling

echo "=== 5/6 — nginx (patch ciblé) ==="
python3 "$REPO_DIR/deploy/server/patch-nginx.py" "$NGINX_CONF"
nginx -t
systemctl reload nginx

echo "=== 6/6 — vérifications ==="
for _ in $(seq 1 20); do
  if curl -fsS "http://127.0.0.1:${PORT}/healthz" >/dev/null 2>&1; then break; fi
  sleep 0.5
done
curl -fsS "http://127.0.0.1:${PORT}/healthz"
echo
curl -fsS "http://127.0.0.1:${PORT}/config"
echo
systemctl --no-pager --lines=10 status colosync-signaling || true
echo "OK — signaling sur 127.0.0.1:${PORT}, public ${PUBLIC_URL}/signal"
