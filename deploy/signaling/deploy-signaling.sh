#!/usr/bin/env bash
# Déploie le signaling server WebRTC sur colo-apps.
# ──────────────────────────────────────────────────
# Copie les fichiers, installe les deps, crée le service systemd,
# et met à jour nginx pour /signal → 127.0.0.1:7778.
#
# Usage : sudo ./deploy-signaling.sh
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
SIGNALING_DIR="$REPO_DIR/deploy/signaling"
INSTALL_DIR="/opt/colo-apps/signaling"
NGINX_CONF="$REPO_DIR/deploy/relay/nginx-colo-apps.conf"
NGINX_DST="/etc/nginx/sites-available/colo-apps"
SERVICE_NAME="colo-signaling"

if [[ $EUID -ne 0 ]]; then
  echo "Relancer avec sudo : sudo $0"
  exit 1
fi

echo "=== 1/5 — Fichiers signaling ==="
mkdir -p "$INSTALL_DIR"
cp "$SIGNALING_DIR/server.js" "$INSTALL_DIR/server.js"
cp "$SIGNALING_DIR/package.json" "$INSTALL_DIR/package.json"

echo "=== 2/5 — npm install ==="
cd "$INSTALL_DIR"
if [[ ! -d node_modules ]]; then
  npm install --production
else
  echo "  node_modules existe déjà"
fi

echo "=== 3/5 — Service systemd ==="
cat > /etc/systemd/system/${SERVICE_NAME}.service <<EOF
[Unit]
Description=Colo Course WebRTC Signaling Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=$INSTALL_DIR
ExecStart=/usr/bin/node $INSTALL_DIR/server.js
Restart=on-failure
RestartSec=5
Environment=PORT=7778

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable ${SERVICE_NAME}
systemctl restart ${SERVICE_NAME}

sleep 1
if systemctl is-active --quiet ${SERVICE_NAME}; then
  echo "  ✓ signaling server démarré sur :7778"
else
  echo "  ✗ signaling server a échoué !"
  journalctl -u ${SERVICE_NAME} --no-pager -n 10
  exit 1
fi

echo "=== 4/5 — nginx (ajout /signal) ==="
if [[ -f "$NGINX_DST" ]]; then
  cp "$NGINX_DST" "${NGINX_DST}.bak.$(date +%Y%m%d%H%M%S)"
fi
cp "$NGINX_CONF" "$NGINX_DST"
nginx -t
systemctl reload nginx
echo "  ✓ nginx rechargé avec /signal → :7778"

echo "=== 5/5 — Vérification ==="
if curl -sS -o /dev/null -w "%{http_code}" http://127.0.0.1:7778/ 2>/dev/null | grep -qE '^(200|426|101)'; then
  echo "  ✓ signaling server répond"
else
  echo "  ⚠ signaling server ne répond pas (normal si pas de WebSocket handshake)"
fi

echo ""
echo "OK — signaling server actif"
echo "  URL : wss://colo-apps.les-crevettes-cevenoles.fr/signal"
echo "  Logs : journalctl -u ${SERVICE_NAME} -f"