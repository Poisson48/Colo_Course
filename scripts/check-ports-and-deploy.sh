#!/usr/bin/env bash
# Vérifie si les ports STUN/TURN sont ouverts sur la box.
# Exécuté toutes les 30 minutes par cron. Dès que les ports sont ouverts,
# lance automatiquement le déploiement WebRTC sur le serveur.
#
# Usage : ./check-ports-and-deploy.sh
# Cron  : */30 * * * * /home/leo/leo/Colo_Course/scripts/check-ports-and-deploy.sh >> /tmp/colo-port-check.log 2>&1

set -euo pipefail

SERVER="78.122.112.36"
PORTS=(3479 5350)
LOGFILE="/tmp/colo-port-check.log"
DEPLOY_MARKER="/tmp/colo-webrtc-deployed"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*"; }

# Si déjà déployé, ne rien faire
if [[ -f "$DEPLOY_MARKER" ]]; then
    log "Déjà déployé — rien à faire."
    exit 0
fi

log "Vérification des ports ${PORTS[*]} sur $SERVER..."

all_open=true
for port in "${PORTS[@]}"; do
    # Test TCP via check-host.net (on ne peut pas tester en local — pas de NAT loopback)
    result=$(curl -sS --max-time 15 \
        "https://check-host.net/check-tcp?host=${SERVER}:${port}&max_nodes=3" \
        -H "Accept: application/json" 2>/dev/null || echo '{}')
    
    request_id=$(echo "$result" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('request_id',''))" 2>/dev/null || echo '')
    
    if [[ -z "$request_id" ]]; then
        log "  Port $port : erreur check-host.net"
        all_open=false
        continue
    fi
    
    sleep 10
    
    check_result=$(curl -sS --max-time 15 \
        "https://check-host.net/check-result/${request_id}" \
        -H "Accept: application/json" 2>/dev/null || echo '{}')
    
    # Vérifier si au moins un nœud a réussi (pas d'erreur)
    success=$(echo "$check_result" | python3 -c "
import sys, json
d = json.load(sys.stdin)
for node, results in d.items():
    if results and isinstance(results[0], dict) and 'address' in results[0]:
        print('yes')
        break
else:
    print('no')
" 2>/dev/null || echo 'no')
    
    if [[ "$success" == "yes" ]]; then
        log "  Port $port : ✅ OUVERT"
    else
        log "  Port $port : ❌ Fermé"
        all_open=false
    fi
done

if $all_open; then
    log "🎉 Tous les ports sont ouverts ! Lancement du déploiement..."
    
    # Pousser le code sur le serveur et lancer le déploiement
    # (nécessite SSH configuré vers le serveur)
    ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new \
        "${SERVER_USER:-leo}@${SERVER}" bash -c "'
        cd /opt/colo-apps/Colo_Course 2>/dev/null || cd ~/Colo_Course
        git pull origin main
        sudo ./deploy/install-webrtc.sh
    '" 2>&1 && {
        log "✅ Déploiement WebRTC terminé avec succès !"
        touch "$DEPLOY_MARKER"
    } || {
        log "⚠️  Déploiement automatique échoué. Lancer manuellement :"
        log "   ssh ${SERVER_USER:-leo}@${SERVER}"
        log "   cd /opt/colo-apps/Colo_Course && sudo ./deploy/install-webrtc.sh"
        # Ne pas marquer comme déployé — on réessaiera dans 30 min
    }
else
    log "Ports pas encore ouverts. Prochaine vérification dans 30 minutes."
fi