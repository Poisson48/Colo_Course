#!/usr/bin/env bash
# Healthcheck lecture seule du serveur Colo Sync.
set -euo pipefail
DOMAIN="${DOMAIN:-colo-apps.les-crevettes-cevenoles.fr}"
BASE="https://${DOMAIN}"
fail=0
check() { if eval "$2" >/dev/null 2>&1; then echo "  ✓ $1"; else echo "  ✗ $1"; fail=1; fi; }

check "nginx actif" "systemctl is-active --quiet nginx"
check "colopush actif" "systemctl is-active --quiet colopush"
check "FCM /push" "curl -fsS '$BASE/push'"
check "relais Strfry (port 7777)" "ss -lntp | grep -q ':7777 '"
check "ntfy (port 8077)" "ss -lntp | grep -q ':8077 '"
check "STUN meownopoly 3478" "ss -lnup | grep -q ':3478 '"
check "P2P signaling arrêté" "! systemctl is-active --quiet colosync-signaling"
check "coturn arrêté" "! systemctl is-active --quiet coturn"
check "releases" "curl -fsS '$BASE/releases/manifest.json'"
check "ntfy" "curl -fsS '$BASE/ntfy/'"
echo
if [[ $fail -eq 0 ]]; then echo "OK"; else echo "ÉCHEC" >&2; exit 1; fi
