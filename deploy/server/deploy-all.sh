#!/usr/bin/env bash
# Déploie signaling + coturn sur un hôte Colo Sync. À lancer sous root, après
# avoir ouvert (ou prévu d'ouvrir) les ports box.
set -euo pipefail
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ $EUID -ne 0 ]]; then echo "sudo $0" >&2; exit 1; fi

echo "=== 1/2 signaling ==="
bash "$REPO_DIR/deploy/server/deploy-signaling.sh"
echo "=== 2/2 coturn ==="
bash "$REPO_DIR/deploy/server/deploy-turn.sh"
echo
echo "Déploiement terminé. Vérifier depuis l'extérieur :"
echo "  wss://${DOMAIN:-colo-apps.les-crevettes-cevenoles.fr}/signal"
echo "  https://${DOMAIN:-colo-apps.les-crevettes-cevenoles.fr}/turn-credentials?app=colocourse&device_id=probe"
echo "  TURN 3479/udp+tcp, 5350/tcp"
