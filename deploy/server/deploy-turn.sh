#!/usr/bin/env bash
# Installe / met à jour coturn sur le serveur Colo Sync.
# TURN est obligatoire pour la 4G ; les ports box restent à ouvrir côté routeur.
#
# Usage : sudo DOMAIN=... PUBLIC_IP=... PRIVATE_IP=... ./deploy/server/deploy-turn.sh
set -euo pipefail

DOMAIN="${DOMAIN:-colo-apps.les-crevettes-cevenoles.fr}"
PUBLIC_IP="${PUBLIC_IP:-78.122.112.36}"
PRIVATE_IP="${PRIVATE_IP:-192.168.1.40}"
COLO_DATA_DIR="${COLO_DATA_DIR:-/data/leo/colo-apps}"
COTURN_DIR="${COLO_DATA_DIR}-coturn"
SECRET_FILE="${COLO_DATA_DIR}/turn.secret"
TLS_DIR="${COTURN_DIR}/tls"
LOG_DIR="${COTURN_DIR}/logs"
CONF="/etc/turnserver.conf"
CERT_SRC="/etc/letsencrypt/live/${DOMAIN}/fullchain.pem"
KEY_SRC="/etc/letsencrypt/live/${DOMAIN}/privkey.pem"

if [[ $EUID -ne 0 ]]; then
  echo "Relancer avec sudo : sudo $0" >&2
  exit 1
fi

echo "=== 1/6 — préflight ==="
# Mise à jour idempotente : libérer nos propres ports avant de vérifier.
if systemctl is-active --quiet coturn 2>/dev/null; then
  echo "  arrêt de coturn pour mise à jour"
  systemctl stop coturn
fi
if ss -lnup 2>/dev/null | grep -qE ':3479\b' || ss -lntp 2>/dev/null | grep -qE ':3479\b'; then
  echo "ERREUR : port 3479 déjà utilisé" >&2
  ss -lntup | grep ':3479' >&2 || true
  exit 1
fi
ss -lnup 2>/dev/null | grep -q ':3478' && echo "  ✓ STUN meownopoly :3478 présent (intouché)"

echo "=== 2/6 — installation (mask avant démarrage) ==="
systemctl mask coturn >/dev/null 2>&1 || true
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends coturn

echo "=== 3/6 — secret et TLS ==="
install -d -m 750 -o root -g colosync "$COLO_DATA_DIR"
if [[ ! -s "$SECRET_FILE" ]]; then
  umask 077
  openssl rand -hex 32 > "$SECRET_FILE"
fi
chown root:colosync "$SECRET_FILE"
chmod 640 "$SECRET_FILE"
SECRET="$(cat "$SECRET_FILE")"

if [[ ! -r "$CERT_SRC" || ! -r "$KEY_SRC" ]]; then
  echo "ERREUR : certificat Let's Encrypt absent pour $DOMAIN" >&2
  echo "  attendu : $CERT_SRC et $KEY_SRC" >&2
  exit 1
fi
install -d -m 750 -o root -g turnserver "$COTURN_DIR" "$TLS_DIR" "$LOG_DIR"
install -m 640 -o root -g turnserver "$CERT_SRC" "$TLS_DIR/fullchain.pem"
install -m 640 -o root -g turnserver "$KEY_SRC" "$TLS_DIR/privkey.pem"
chown -R turnserver:turnserver "$LOG_DIR"

echo "=== 4/6 — configuration coturn ==="
umask 077
cat > "$CONF" <<EOF
listening-ip=${PRIVATE_IP}
relay-ip=${PRIVATE_IP}
external-ip=${PUBLIC_IP}/${PRIVATE_IP}
listening-port=3479
tls-listening-port=5350
min-port=50000
max-port=50019
realm=${DOMAIN}
server-name=${DOMAIN}
use-auth-secret
static-auth-secret=${SECRET}
fingerprint
no-cli
no-multicast-peers
stale-nonce=600
no-tlsv1
no-tlsv1_1
user-quota=12
total-quota=100
max-bps=5000000
cert=${TLS_DIR}/fullchain.pem
pkey=${TLS_DIR}/privkey.pem
log-file=${LOG_DIR}/turnserver.log
simple-log
EOF
chown root:turnserver "$CONF"
chmod 640 "$CONF"

if [[ -f /etc/default/coturn ]]; then
  if grep -q '^#*TURNSERVER_ENABLED=' /etc/default/coturn; then
    sed -i 's/^#*TURNSERVER_ENABLED=.*/TURNSERVER_ENABLED=1/' /etc/default/coturn
  else
    echo 'TURNSERVER_ENABLED=1' >> /etc/default/coturn
  fi
fi

echo "=== 5/6 — hook certbot ==="
HOOK_DIR="/etc/letsencrypt/renewal-hooks/deploy"
install -d -m 755 "$HOOK_DIR"
cat > "$HOOK_DIR/colosync-turn.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
install -m 640 -o root -g turnserver "$CERT_SRC" "$TLS_DIR/fullchain.pem"
install -m 640 -o root -g turnserver "$KEY_SRC" "$TLS_DIR/privkey.pem"
systemctl restart coturn
EOF
chmod 700 "$HOOK_DIR/colosync-turn.sh"

echo "=== 6/6 — démarrage et vérifications ==="
systemctl unmask coturn >/dev/null 2>&1 || true
systemctl enable coturn
systemctl restart coturn
sleep 2
systemctl --no-pager --lines=10 status coturn || true
ss -lntup | grep -E ':(3478|3479|5350)\b' || true
if ! ss -lnup | grep -qE ':3479\b'; then
  echo "ERREUR : coturn n'écoute pas 3479/udp" >&2
  journalctl -u coturn --no-pager -n 30 >&2 || true
  exit 1
fi
if ss -lnup | grep -qE ':3478\b'; then
  echo "  ✓ STUN meownopoly :3478 toujours présent"
else
  echo "  ⚠ STUN meownopoly :3478 non détecté" >&2
fi
echo "OK — coturn 3479/udp+tcp, 5350/tcp ; secret dans ${SECRET_FILE}"
echo "Ports box à ouvrir : 3479/udp, 3479/tcp, 5350/tcp, 50000-50019/udp -> ${PRIVATE_IP}"
