#!/usr/bin/env bash
# Publie data/recipe_library.json + recipes-manifest.json sur colo-apps.
#
# Usage :
#   COLO_REPO_DIR=/opt/colo-apps/Colo_Course \
#   COLO_RELEASES_DIR=/var/lib/colo-apps/releases \
#   /opt/colo-apps/sync-recipes.sh
#
# Ou sans clone local (télécharge depuis GitHub) :
#   COLO_GITHUB_REPO=Poisson48/Colo_Course /opt/colo-apps/sync-recipes.sh
set -euo pipefail

REPO="${COLO_GITHUB_REPO:-Poisson48/Colo_Course}"
REPO_DIR="${COLO_REPO_DIR:-}"
DEST="${COLO_RELEASES_DIR:-/var/lib/colo-apps/releases}"
PUBLIC_BASE="${COLO_RELEASES_PUBLIC_URL:-https://colo-apps.les-crevettes-cevenoles.fr/releases}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$DEST"
log() { printf '[%s] %s\n' "$(date -Iseconds)" "$*"; }

SRC="$TMP/recipe_library.json"
if [[ -n "$REPO_DIR" && -f "$REPO_DIR/data/recipe_library.json" ]]; then
  cp "$REPO_DIR/data/recipe_library.json" "$SRC"
  log "Source locale : $REPO_DIR/data/recipe_library.json"
else
  log "Téléchargement depuis GitHub ($REPO)…"
  curl -fsSL "https://raw.githubusercontent.com/${REPO}/main/data/recipe_library.json" -o "$SRC"
fi

python3 - "$SRC" "$DEST" "$PUBLIC_BASE" <<'PY'
import json, os, sys
from datetime import datetime, timezone

src, dest, base = sys.argv[1:4]
with open(src, encoding="utf-8") as f:
    data = json.load(f)

count = data.get("count") or len(data.get("recipes", []))
version = int(data.get("version", 1))
manifest_path = os.path.join(dest, "recipes-manifest.json")

current = 0
if os.path.isfile(manifest_path):
    try:
        with open(manifest_path, encoding="utf-8") as f:
            current = int(json.load(f).get("count", 0))
    except (json.JSONDecodeError, TypeError, ValueError):
        current = 0

if count <= current and os.path.isfile(os.path.join(dest, "recipe_library.json")):
    print(f"Déjà à jour ({count} recettes)", flush=True)
    sys.exit(0)

out_json = os.path.join(dest, "recipe_library.json")
tmp_json = out_json + ".tmp"
with open(tmp_json, "wb") as f:
  import shutil
  shutil.copyfile(src, tmp_json)
os.replace(tmp_json, out_json)

manifest = {
    "version": version,
    "count": count,
    "updatedAt": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "url": f"{base.rstrip('/')}/recipe_library.json",
}
tmp_manifest = manifest_path + ".tmp"
with open(tmp_manifest, "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=2)
    f.write("\n")
os.replace(tmp_manifest, manifest_path)

print(f"Publié {count} recettes (était {current})", flush=True)
PY

log "OK — recipes-manifest.json dans $DEST"
