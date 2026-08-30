#!/usr/bin/env bash
# Télécharge la dernière release GitHub et publie manifest.json + APK/AppImage
# sur colo-apps. Les clients ne contactent plus GitHub directement.
#
# Usage (sur le serveur, en cron toutes les 5 min) :
#   */5 * * * * COLO_RELEASES_DIR=/var/lib/colo-apps/releases \
#     /opt/colo-apps/sync-from-github.sh >>/var/log/colo-apps-releases.log 2>&1
#
# Variables :
#   COLO_RELEASES_DIR  — répertoire servi par nginx (/releases/)
#   COLO_GITHUB_REPO   — défaut Poisson48/Colo_Course
set -euo pipefail

REPO="${COLO_GITHUB_REPO:-Poisson48/Colo_Course}"
DEST="${COLO_RELEASES_DIR:-/var/lib/colo-apps/releases}"
API="https://api.github.com/repos/${REPO}/releases?per_page=30"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$DEST"

log() { printf '[%s] %s\n' "$(date -Iseconds)" "$*"; }

# Dernière release stable (non draft, non prerelease).
RELEASE_JSON="$(
  curl -fsSL -H 'Accept: application/vnd.github+json' -H 'User-Agent: ColoCourse-Releases' \
    "$API" | python3 - "$TMP/release_meta.json" <<'PY'
import json, sys
releases = json.load(sys.stdin)
for r in releases:
    if r.get("draft") or r.get("prerelease"):
        continue
    tag = (r.get("tag_name") or "").lstrip("vV")
    if not tag:
        continue
    with open(sys.argv[1], "w", encoding="utf-8") as f:
        json.dump(r, f, ensure_ascii=False)
    print(tag)
    break
else:
    sys.exit(1)
PY
)" || { log "Aucune release stable trouvée sur GitHub"; exit 0; }

VERSION="$RELEASE_JSON"
log "Dernière release GitHub : $VERSION"

MANIFEST="$DEST/manifest.json"
CURRENT=""
if [[ -f "$MANIFEST" ]]; then
  CURRENT="$(python3 -c "import json; print(json.load(open('$MANIFEST')).get('version',''))" 2>/dev/null || true)"
fi

if [[ "$CURRENT" == "$VERSION" ]]; then
  log "Déjà à jour ($VERSION)"
  exit 0
fi

log "Mise à jour $CURRENT → $VERSION"

python3 - "$TMP/release_meta.json" "$DEST" "$VERSION" <<'PY'
import json, os, re, sys, urllib.request

meta_path, dest, version = sys.argv[1:4]
with open(meta_path, encoding="utf-8") as f:
    rel = json.load(f)

def notes_from_body(body: str) -> str:
    kept = []
    for line in (body or "").split("\n"):
        if line.strip() == "---":
            break
        s = line.strip()
        while s.startswith("#"):
            s = s[1:].strip()
        kept.append(s)
    while kept and not kept[-1]:
        kept.pop()
    return "\n".join(kept).strip()

tag = rel.get("tag_name", "")
published = rel.get("published_at", "")
notes = notes_from_body(rel.get("body", ""))
html_url = rel.get("html_url", "")

apk_name = appimage_name = ""
apk_url = appimage_url = ""
for asset in rel.get("assets", []):
    name = asset.get("name", "")
    url = asset.get("browser_download_url", "")
    if name.lower().endswith(".apk"):
        apk_name, apk_url = name, url
    elif name.lower().endswith(".appimage"):
        appimage_name, appimage_url = name, url

def download(url: str, path: str) -> None:
    req = urllib.request.Request(url, headers={"User-Agent": "ColoCourse-Releases"})
    with urllib.request.urlopen(req, timeout=600) as resp, open(path, "wb") as out:
        out.write(resp.read())

base = os.environ.get("COLO_RELEASES_PUBLIC_URL",
                       "https://colo-apps.les-crevettes-cevenoles.fr/releases")

if apk_url and apk_name:
    local_apk = os.path.join(dest, apk_name)
    print(f"Téléchargement APK {apk_name}…", flush=True)
    download(apk_url, local_apk)
    apk_public = f"{base.rstrip('/')}/{apk_name}"
else:
    apk_public = ""

if appimage_url and appimage_name:
    local_img = os.path.join(dest, appimage_name)
    print(f"Téléchargement AppImage {appimage_name}…", flush=True)
    download(appimage_url, local_img)
    os.chmod(local_img, 0o755)
    appimage_public = f"{base.rstrip('/')}/{appimage_name}"
else:
    appimage_public = ""

# Historique (30 dernières releases stables).
api = f"https://api.github.com/repos/{os.environ.get('COLO_GITHUB_REPO', 'Poisson48/Colo_Course')}/releases?per_page=30"
req = urllib.request.Request(api, headers={
    "Accept": "application/vnd.github+json",
    "User-Agent": "ColoCourse-Releases",
})
with urllib.request.urlopen(req, timeout=60) as resp:
    all_releases = json.load(resp)

changelog = []
for r in all_releases:
    if r.get("draft") or r.get("prerelease"):
        continue
    ver = (r.get("tag_name") or "").lstrip("vV")
    if not ver:
        continue
    changelog.append({
        "version": ver,
        "notes": notes_from_body(r.get("body", "")),
        "publishedAt": r.get("published_at", ""),
    })

manifest = {
    "version": version,
    "publishedAt": published,
    "notes": notes,
    "apkUrl": apk_public,
    "appImageUrl": appimage_public,
    "releaseUrl": html_url,
    "changelog": changelog,
}

tmp_manifest = os.path.join(dest, "manifest.json.tmp")
with open(tmp_manifest, "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=2)
    f.write("\n")
os.replace(tmp_manifest, os.path.join(dest, "manifest.json"))

# Conserver uniquement les artefacts de la version courante.
keep = {apk_name, appimage_name, "manifest.json"}
for name in os.listdir(dest):
    if name not in keep:
        path = os.path.join(dest, name)
        if os.path.isfile(path):
            os.remove(path)
            print(f"Supprimé ancien artefact : {name}", flush=True)

print(f"Manifest publié : {version}", flush=True)
PY

log "OK — manifest $VERSION publié dans $DEST"
