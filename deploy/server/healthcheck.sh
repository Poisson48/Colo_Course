#!/usr/bin/env bash
# Healthcheck lecture seule du serveur Colo Sync.
set -euo pipefail
DOMAIN="${DOMAIN:-colo-apps.les-crevettes-cevenoles.fr}"
BASE="https://${DOMAIN}"
fail=0
check() { if eval "$2" >/dev/null 2>&1; then echo "  ✓ $1"; else echo "  ✗ $1"; fail=1; fi; }

check "nginx actif" "systemctl is-active --quiet nginx"
check "signaling actif" "systemctl is-active --quiet colosync-signaling"
check "coturn actif" "systemctl is-active --quiet coturn"
check "relais Strfry (port 7777)" "ss -lntp | grep -q ':7777 '"
check "ntfy (port 8077)" "ss -lntp | grep -q ':8077 '"
check "STUN meownopoly 3478" "ss -lnup | grep -q ':3478 '"
check "coturn 3479/udp" "ss -lnup | grep -q ':3479 '"
check "coturn 5350/tcp" "ss -lntp | grep -q ':5350 '"
check "config HTTPS" "curl -fsS '$BASE/config'"
check "signal HTTPS (426 attendu)" "curl -sS -o /dev/null -w '%{http_code}' '$BASE/signal' | grep -q 426"
check "releases" "curl -fsS '$BASE/releases/manifest.json'"
check "ntfy" "curl -fsS '$BASE/ntfy/'"
echo
if [[ $fail -eq 0 ]]; then echo "OK"; else echo "ÉCHEC" >&2; exit 1; fi
