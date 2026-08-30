#!/usr/bin/env bash
# Active /releases/ sur colo-apps (MAJ app + catalogue recettes) en une commande.
#
# Sur le serveur, depuis un clone à jour du dépôt :
#   git pull
#   sudo ./deploy/releases/install-all.sh
#
# Prérequis :
#   - certificat Let's Encrypt pour colo-apps.les-crevettes-cevenoles.fr
#   - route SNI dans /etc/nginx/nginx.conf (voir deploy/relay/install-host.sh)
set -euo pipefail

DOMAIN="colo-apps.les-crevettes-cevenoles.fr"
PUBLIC_BASE="https://${DOMAIN}/releases"
RELEASES_DIR="${COLO_RELEASES_DIR:-/var/lib/colo-apps/releases}"
OPT_DIR="${COLO_OPT_DIR:-/opt/colo-apps}"
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

log() { printf '[%s] %s\n' "$(date -Iseconds)" "$*"; }
die() { log "ERREUR: $*"; exit 1; }

if [[ $EUID -ne 0 ]]; then
  die "Relancer avec sudo : sudo $0"
fi

if [[ ! -f "/etc/letsencrypt/live/$DOMAIN/fullchain.pem" ]]; then
  die "Certificat absent. D'abord : sudo certbot certonly --manual --preferred-challenges dns-01 -d $DOMAIN"
fi

if ! command -v nginx >/dev/null 2>&1; then
  die "nginx introuvable"
fi

if ! command -v curl >/dev/null 2>&1; then
  die "curl introuvable"
fi

if ! command -v python3 >/dev/null 2>&1; then
  die "python3 introuvable"
fi

NGINX_SRC="$REPO_DIR/deploy/relay/nginx-colo-apps.conf"
[[ -f "$NGINX_SRC" ]] || die "Fichier manquant : $NGINX_SRC (clone du dépôt requis)"

log "=== 1/6 — nginx (/releases/ au lieu du relais) ==="
if [[ -f /etc/nginx/sites-available/colo-apps ]]; then
  cp /etc/nginx/sites-available/colo-apps \
    "/etc/nginx/sites-available/colo-apps.bak.$(date +%Y%m%d%H%M%S)"
fi
cp "$NGINX_SRC" /etc/nginx/sites-available/colo-apps
if [[ "$RELEASES_DIR" != "/var/lib/colo-apps/releases" ]]; then
  sed -i "s|/var/lib/colo-apps/releases/|${RELEASES_DIR}/|g" /etc/nginx/sites-available/colo-apps
fi
ln -sf /etc/nginx/sites-available/colo-apps /etc/nginx/sites-enabled/colo-apps

if ! grep -q "$DOMAIN" /etc/nginx/nginx.conf; then
  echo ""
  echo "Ajoutez dans le map \$ssl_preread_server_name de /etc/nginx/nginx.conf :"
  echo "        $DOMAIN  127.0.0.1:11443;"
  echo ""
  die "Route SNI manquante — voir deploy/relay/install-host.sh"
fi

nginx -t
systemctl reload nginx
log "nginx rechargé"

log "=== 2/6 — répertoires et scripts ==="
mkdir -p "$RELEASES_DIR" "$OPT_DIR"
# nginx (www-data) doit traverser les parents si RELEASES_DIR est sous /home ou /data.
if [[ "$RELEASES_DIR" == /data/* || "$RELEASES_DIR" == /home/* ]]; then
  p="$(dirname "$RELEASES_DIR")"
  while [[ "$p" != "/" ]]; do
    chmod o+x "$p" 2>/dev/null || true
    p="$(dirname "$p")"
  done
fi
chmod -R a+rX "$RELEASES_DIR"
install -m 0755 "$REPO_DIR/deploy/releases/sync-from-github.sh" "$OPT_DIR/sync-from-github.sh"
install -m 0755 "$REPO_DIR/deploy/releases/sync-recipes.sh" "$OPT_DIR/sync-recipes.sh"

log "=== 3/6 — tâches cron ==="
cat > /etc/cron.d/colo-apps-releases <<EOF
# Colo Course — synchronise manifest.json + APK/AppImage depuis GitHub
*/5 * * * * root COLO_RELEASES_DIR=$RELEASES_DIR COLO_RELEASES_PUBLIC_URL=$PUBLIC_BASE $OPT_DIR/sync-from-github.sh >>/var/log/colo-apps-releases.log 2>&1
EOF
chmod 644 /etc/cron.d/colo-apps-releases

cat > /etc/cron.d/colo-apps-recipes <<EOF
# Colo Course — synchronise recipe_library.json + recipes-manifest.json
0 * * * * root COLO_RELEASES_DIR=$RELEASES_DIR COLO_RELEASES_PUBLIC_URL=$PUBLIC_BASE $OPT_DIR/sync-recipes.sh >>/var/log/colo-apps-recipes.log 2>&1
EOF
chmod 644 /etc/cron.d/colo-apps-recipes

log "=== 4/6 — synchro releases (manifest + binaires) ==="
COLO_RELEASES_DIR="$RELEASES_DIR" COLO_RELEASES_PUBLIC_URL="$PUBLIC_BASE" \
  "$OPT_DIR/sync-from-github.sh"

log "=== 5/6 — synchro catalogue recettes (~60 Mo, peut prendre quelques minutes) ==="
COLO_RELEASES_DIR="$RELEASES_DIR" COLO_RELEASES_PUBLIC_URL="$PUBLIC_BASE" \
  COLO_GITHUB_REPO="${COLO_GITHUB_REPO:-Poisson48/Colo_Course}" \
  "$OPT_DIR/sync-recipes.sh"

log "=== 6/6 — vérification HTTPS ==="
check_json() {
  local url="$1" label="$2"
  local body ct
  body="$(curl -fsSL --max-time 30 "$url")" || die "Échec GET $url"
  ct="$(curl -fsSI --max-time 30 "$url" | tr -d '\r' | awk -F': ' 'tolower($1)=="content-type"{print tolower($2); exit}')"
  if [[ "$ct" != *"json"* && "$ct" != *"octet-stream"* ]]; then
    die "$label : Content-Type inattendu ($ct) — /releases/ pointe peut-être encore vers le relais"
  fi
  python3 -c "import json,sys; json.loads(sys.stdin.read())" <<<"$body" \
    || die "$label : réponse non JSON valide"
  log "OK $label"
}

check_json "$PUBLIC_BASE/manifest.json" "manifest.json"
check_json "$PUBLIC_BASE/recipes-manifest.json" "recipes-manifest.json"

if [[ ! -f "$RELEASES_DIR/recipe_library.json" ]]; then
  die "recipe_library.json absent dans $RELEASES_DIR"
fi
SIZE_MB="$(du -m "$RELEASES_DIR/recipe_library.json" | awk '{print $1}')"
log "OK recipe_library.json (${SIZE_MB} Mo)"

log "=== Vérif relais + autres sites (non destructif) ==="
if ! grep -q 'proxy_pass http://127.0.0.1:7777' /etc/nginx/sites-available/colo-apps; then
  die "Config colo-apps : proxy relais 7777 absent"
fi
if ! grep -q 'location /ntfy/' /etc/nginx/sites-available/colo-apps; then
  die "Config colo-apps : location /ntfy/ absente"
fi
log "OK relais + ntfy préservés dans la config colo-apps"
nginx -t
log "OK nginx -t (tous les sites)"

echo ""
log "Terminé — $PUBLIC_BASE/ est actif"
echo "  manifest.json          → MAJ app (cron */5 min)"
echo "  recipes-manifest.json  → catalogue recettes (cron 1 h)"
echo "  Logs : /var/log/colo-apps-releases.log , /var/log/colo-apps-recipes.log"
