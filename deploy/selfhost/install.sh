#!/usr/bin/env bash
# Installe un serveur Colo Sync complet (relais + signaling + coturn) chez soi.
# Prérequis : Ubuntu/Debian, DNS pointé, certificat Let's Encrypt, Docker pour Strfry.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENV_FILE="$ROOT/deploy/selfhost/.env"
if [[ ! -f "$ENV_FILE" ]]; then
  echo "Copier deploy/selfhost/.env.example vers deploy/selfhost/.env et l'adapter." >&2
  exit 1
fi
set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

if [[ $EUID -ne 0 ]]; then echo "sudo $0" >&2; exit 1; fi
if [[ ! -r "/etc/letsencrypt/live/${DOMAIN}/fullchain.pem" ]]; then
  echo "Certificat absent. Obtenir d'abord un certificat pour ${DOMAIN} (certbot)." >&2
  exit 1
fi

echo "=== Relais Strfry (Docker) ==="
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker requis pour Strfry. Installer Docker puis relancer." >&2
  exit 1
fi
# Le relais est fourni par le dépôt principal : copier deploy/relay sur l'hôte
# et suivre deploy/relay/README.md (strfry.conf + write-policy.py + compose).
echo "  → configurer deploy/relay/ (strfry) avant de continuer."
echo "  → le vhost nginx doit pointer / vers Strfry (127.0.0.1:7777)."

echo "=== Signaling + TURN ==="
bash "$ROOT/deploy/server/deploy-all.sh"

echo
echo "Terminé. Dans l'app :"
echo "  Relais Nostr : wss://${DOMAIN}"
echo "  Serveur P2P  : ${PUBLIC_URL}"
echo "Ouvrir sur la box : 3479/udp+tcp, 5350/tcp, 50000-50100/udp -> ${PRIVATE_IP}"
