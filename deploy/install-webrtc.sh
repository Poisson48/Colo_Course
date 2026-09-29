#!/usr/bin/env bash
# Colo Course — Install complète WebRTC (signaling + coturn STUN/TURN)
# ─────────────────────────────────────────────────────────────────────
# Usage : cd /opt/colo-apps/Colo_Course && sudo ./deploy/install-webrtc.sh
#
# Ce script :
#   1. Déploie le signaling server (WebSocket sur :7778, nginx /signal)
#   2. Installe coturn STUN/TURN (:3479/:5350, relay :50000-50100)
#   3. Ne touche PAS au STUN meownopoly (:3478), ni aux autres services
#
# ⚠️  AVANT de lancer : ouvrir les ports sur la box :
#     - 3479/udp + 3479/tcp  (STUN/TURN)
#     - 5350/tcp             (TURNS TLS)
#     - 50000-50100/udp      (relay UDP)
#     (le signaling passe par :443/nginx, pas besoin de port supplémentaire)
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

if [[ $EUID -ne 0 ]]; then
  echo "Relancer avec sudo : sudo $0"
  exit 1
fi

echo "=========================================="
echo "  Colo Course — déploiement WebRTC"
echo "=========================================="
echo ""

echo "╔══════════════════════════════════════╗"
echo "║  1/2 — Signaling server              ║"
echo "╚══════════════════════════════════════╝"
bash "$REPO_DIR/deploy/signaling/deploy-signaling.sh"

echo ""
echo "╔══════════════════════════════════════╗"
echo "║  2/2 — coturn STUN/TURN              ║"
echo "╚══════════════════════════════════════╝"
bash "$REPO_DIR/deploy/coturn/install.sh"

echo ""
echo "=========================================="
echo "  ✅ WebRTC complet !"
echo "=========================================="
echo ""
echo "  Signaling : wss://colo-apps.les-crevettes-cevenoles.fr/signal"
echo "  STUN      : stun:colo-apps.les-crevettes-cevenoles.fr:3479"
echo "  TURN      : turn:colo-apps.les-crevettes-cevenoles.fr:3479"
echo "  TURNS     : turns:colo-apps.les-crevettes-cevenoles.fr:5350"
echo ""
echo "  ⚠  Vérifier que ces ports sont ouverts sur la box :"
echo "     3479/udp, 3479/tcp, 5350/tcp, 50000-50100/udp"
echo ""
echo "  Le STUN meownopoly (:3478) n'a PAS été touché."