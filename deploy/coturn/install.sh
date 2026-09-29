#!/usr/bin/env bash
# Installe coturn (STUN/TURN) sur colo-apps.les-crevettes-cevenoles.fr
# ─────────────────────────────────────────────────────────────────────
# Ne touche PAS à nginx, Strfry, ntfy, releases, ni au STUN meownopoly (:3478).
# Ports coturn : 3479 (UDP+TCP), 5350 (TURNS TLS), 50000-50100 (relay UDP)
#
# Usage : sudo ./install.sh
set -euo pipefail

DOMAIN="colo-apps.les-crevettes-cevenoles.fr"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONF_SRC="$SCRIPT_DIR/turnserver.conf"
CONF_DST="/etc/turnserver.conf"

if [[ $EUID -ne 0 ]]; then
  echo "Relancer avec sudo : sudo $0"
  exit 1
fi

echo "=== Vérifications préalables ==="

# 1. Certificat Let's Encrypt existant
if [[ ! -f "/etc/letsencrypt/live/$DOMAIN/fullchain.pem" ]]; then
  echo "ERREUR : certificat absent pour $DOMAIN"
  echo "D'abord : sudo certbot certonly --manual --preferred-challenges dns-01 -d $DOMAIN"
  exit 1
fi
echo "  ✓ Certificat TLS présent"

# 2. Vérifier que les ports coturn ne sont pas déjà utilisés
for port in 3479 5350; do
  if ss -tlnp | grep -q ":${port} " || ss -ulnp | grep -q ":${port} "; then
    echo "ERREUR : le port $port est déjà utilisé !"
    ss -tulnp | grep ":${port} "
    echo "Corriger avant de relancer."
    exit 1
  fi
done
echo "  ✓ Ports 3479 et 5350 libres"

# 3. Vérifier que le STUN meownopoly (:3478) est bien intact
if ss -ulnp | grep -q ':3478' || ss -tlnp | grep -q ':3478'; then
  echo "  ✓ STUN meownopoly (:3478) actif — on n'y touche pas"
else
  echo "  ⚠ Port 3478 non détecté (meownopoly STUN) — pas grave, on n'y touche pas"
fi

# 4. Vérifier que les services existants tournent bien
echo "  Services existants :"
for svc in nginx; do
  if systemctl is-active --quiet "$svc" 2>/dev/null; then
    echo "    ✓ $svc actif"
  else
    echo "    ⚠ $svc inactif (pas forcément un problème)"
  fi
done
if docker ps --format '{{.Names}}' 2>/dev/null | grep -q strfry; then
  echo "    ✓ strfry (Docker) actif"
else
  echo "    ⚠ strfry Docker non trouvé"
fi

echo ""
echo "=== Installation de coturn ==="

apt-get update -qq
apt-get install -y -qq coturn

# Générer un secret si le config contient le placeholder
if grep -q 'CHANGEMI_GENERATE_WITH_openssl_rand_hex_32' "$CONF_SRC" 2>/dev/null; then
  SECRET=$(openssl rand -hex 32)
  echo "  ✓ Secret TURN généré (garder précieusement) :"
  echo "    $SECRET"
  echo ""
  # Copier le conf en remplaçant le placeholder
  sed "s/CHANGEMI_GENERATE_WITH_openssl_rand_hex_32/$SECRET/" "$CONF_SRC" > "$CONF_DST"
else
  cp "$CONF_SRC" "$CONF_DST"
fi
echo "  ✓ Config installée → $CONF_DST"

# Créer le répertoire de logs
mkdir -p /var/log/coturn
chown turnserver:turnserver /var/log/coturn 2>/dev/null || true

# Activer le service (coturn est désactivé par défaut sur Debian/Ubuntu)
if [[ -f /etc/default/coturn ]]; then
  sed -i 's/^#TURNSERVER_ENABLED=1/TURNSERVER_ENABLED=1/' /etc/default/coturn
  if ! grep -q '^TURNSERVER_ENABLED=1' /etc/default/coturn; then
    echo 'TURNSERVER_ENABLED=1' >> /etc/default/coturn
  fi
fi

systemctl daemon-reload
systemctl enable coturn
systemctl restart coturn

# Vérifier que coturn tourne
sleep 2
if systemctl is-active --quiet coturn; then
  echo "  ✓ coturn démarré"
else
  echo "  ✗ coturn a échoué ! Logs :"
  journalctl -u coturn --no-pager -n 20
  exit 1
fi

echo ""
echo "=== Vérification post-install ==="

# Test local des ports
if ss -ulnp | grep -q ':3479'; then
  echo "  ✓ STUN/TURN UDP 3479 OK"
else
  echo "  ⚠ UDP 3479 non détecté"
fi
if ss -tlnp | grep -q ':3479'; then
  echo "  ✓ STUN/TURN TCP 3479 OK"
else
  echo "  ⚠ TCP 3479 non détecté"
fi
if ss -tlnp | grep -q ':5350'; then
  echo "  ✓ TURNS TLS 5350 OK"
else
  echo "  ⚠ TLS 5350 non détecté"
fi

# Vérifier que le STUN meownopoly est toujours intact
if ss -ulnp | grep -q ':3478'; then
  echo "  ✓ STUN meownopoly (:3478) toujours intact"
fi

echo ""
echo "=== RÉCAPITULATIF ==="
echo ""
echo "  Services sur $DOMAIN :"
echo "    - Nostr relay  : wss://$DOMAIN           (Strfry → 127.0.0.1:7777)"
echo "    - ntfy push    : https://$DOMAIN/ntfy/   (→ 127.0.0.1:8077)"
echo "    - Releases     : https://$DOMAIN/releases/"
echo "    - STUN/TURN    : stun:$DOMAIN:3479       (coturn UDP+TCP)"
echo "    - TURNS (TLS)  : turns:$DOMAIN:5350      (coturn TLS)"
echo ""
echo "  ⚠️  PORTS À OUVRIR SUR LA BOX :"
echo "    - 3479/udp  (STUN/TURN)"
echo "    - 3479/tcp  (STUN/TURN TCP fallback)"
echo "    - 5350/tcp  (TURNS TLS)"
echo "    - 50000-50100/udp  (relay UDP)"
echo ""
echo "  Config : $CONF_DST"
echo "  Logs   : journalctl -u coturn -f"
echo "         : /var/log/coturn/turnserver.log"
echo ""
echo "  Secret TURN (pour l'app) : voir $CONF_DST → static-auth-secret"
echo ""
echo "  ⚠  Le STUN meownopoly (:3478) n'a PAS été touché."