#!/usr/bin/env bash
# Rollback minimal : coupe les nouveaux services et restaure le vhost.
# Usage : sudo ./rollback.sh /chemin/backup (contenant colo-apps et/ou nginx.conf)
set -euo pipefail
if [[ $EUID -ne 0 ]]; then echo "sudo $0" >&2; exit 1; fi
BK="${1:-}"
if [[ -z "$BK" || ! -d "$BK" ]]; then echo "usage: $0 <backup-dir>" >&2; exit 1; fi

systemctl disable --now colosync-signaling 2>/dev/null || true
systemctl disable --now coturn 2>/dev/null || true
if [[ -f "$BK/colo-apps" ]]; then
  cp "$BK/colo-apps" /etc/nginx/sites-available/colo-apps
elif [[ -f "$BK/nginx-colo-apps.conf" ]]; then
  cp "$BK/nginx-colo-apps.conf" /etc/nginx/sites-available/colo-apps
fi
nginx -t
systemctl reload nginx
echo "Rollback effectué. Les données Strfry/ntfy et volumes Docker n'ont pas été touchés."
