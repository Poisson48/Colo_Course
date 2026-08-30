#!/usr/bin/env bash
# Installe le vhost nginx + route SNI pour colo-apps.les-crevettes-cevenoles.fr
set -euo pipefail

DOMAIN="colo-apps.les-crevettes-cevenoles.fr"
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
NGINX_SRC="$REPO_DIR/deploy/relay/nginx-colo-apps.conf"
NGINX_DST="/etc/nginx/sites-available/colo-apps"

if [[ $EUID -ne 0 ]]; then
  echo "Relancer avec sudo : sudo $0"
  exit 1
fi

if [[ ! -f "/etc/letsencrypt/live/$DOMAIN/fullchain.pem" ]]; then
  echo "Certificat absent pour $DOMAIN."
  echo "D'abord : sudo certbot certonly --manual --preferred-challenges dns-01 -d $DOMAIN"
  exit 1
fi

cp "$NGINX_SRC" "$NGINX_DST"
ln -sf "$NGINX_DST" /etc/nginx/sites-enabled/colo-apps

if ! grep -q "$DOMAIN" /etc/nginx/nginx.conf; then
  echo ""
  echo "Ajoutez dans le map \$ssl_preread_server_name de /etc/nginx/nginx.conf :"
  echo "        $DOMAIN  127.0.0.1:11443;"
  echo ""
  echo "Puis relancez ce script."
  exit 1
fi

nginx -t
systemctl reload nginx

mkdir -p /var/lib/colo-apps/releases
if [[ -f "$REPO_DIR/deploy/releases/sync-from-github.sh" ]]; then
  install -m 0755 "$REPO_DIR/deploy/releases/sync-from-github.sh" /opt/colo-apps/sync-from-github.sh
  echo "Script releases : /opt/colo-apps/sync-from-github.sh"
  echo "Cron suggéré : */5 * * * * root COLO_RELEASES_DIR=/var/lib/colo-apps/releases /opt/colo-apps/sync-from-github.sh"
fi

echo "OK — wss://$DOMAIN"
