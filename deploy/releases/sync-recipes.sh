#!/usr/bin/env bash
# Publie recipe_catalog.db + recipes-manifest.json sur colo-apps.
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

SRC_JSON="$TMP/recipe_library.json"
SRC_DB="$TMP/recipe_catalog.db"
BUILD_SCRIPT="$TMP/json_to_catalog_db.py"

if [[ -n "$REPO_DIR" && -f "$REPO_DIR/data/recipe_library.json" ]]; then
  cp "$REPO_DIR/data/recipe_library.json" "$SRC_JSON"
  cp "$REPO_DIR/scripts/json_to_catalog_db.py" "$BUILD_SCRIPT"
  log "Source locale : $REPO_DIR/data/recipe_library.json"
else
  log "Téléchargement depuis GitHub ($REPO)…"
  curl -fsSL "https://raw.githubusercontent.com/${REPO}/main/data/recipe_library.json" -o "$SRC_JSON"
  curl -fsSL "https://raw.githubusercontent.com/${REPO}/main/scripts/json_to_catalog_db.py" -o "$BUILD_SCRIPT"
fi

python3 "$BUILD_SCRIPT" "$SRC_JSON" "$SRC_DB"

python3 - "$SRC_JSON" "$SRC_DB" "$DEST" "$PUBLIC_BASE" <<'PY'
import hashlib
import json
import os
import sys
from datetime import datetime, timezone

json_path, db_path, dest, base = sys.argv[1:5]
with open(json_path, encoding="utf-8") as f:
    data = json.load(f)

count = data.get("count") or len(data.get("recipes", []))
version = int(data.get("version", 1))
manifest_path = os.path.join(dest, "recipes-manifest.json")
db_dest = os.path.join(dest, "recipe_catalog.db")

with open(db_path, "rb") as f:
    db_bytes = f.read()
sha256 = hashlib.sha256(db_bytes).hexdigest()
byte_size = len(db_bytes)

current = 0
current_sha = ""
if os.path.isfile(manifest_path):
    try:
        with open(manifest_path, encoding="utf-8") as f:
            old = json.load(f)
            current = int(old.get("count", 0))
            current_sha = str(old.get("sha256", ""))
    except (json.JSONDecodeError, TypeError, ValueError):
        current = 0

if count <= current and current_sha == sha256 and os.path.isfile(db_dest):
    print(f"Déjà à jour ({count} recettes)", flush=True)
    sys.exit(0)

tmp_db = db_dest + ".tmp"
with open(tmp_db, "wb") as f:
    f.write(db_bytes)
os.replace(tmp_db, db_dest)

manifest = {
    "version": version,
    "count": count,
    "schemaVersion": 2,
    "format": "sqlite",
    "byteSize": byte_size,
    "sha256": sha256,
    "updatedAt": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "url": f"{base.rstrip('/')}/recipe_catalog.db",
}
tmp_manifest = manifest_path + ".tmp"
with open(tmp_manifest, "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=2)
    f.write("\n")
os.replace(tmp_manifest, manifest_path)

print(f"Publié {count} recettes → recipe_catalog.db (était {current})", flush=True)
PY

log "OK — recipes-manifest.json dans $DEST"
