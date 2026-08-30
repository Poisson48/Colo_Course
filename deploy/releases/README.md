# Mises à jour hébergées sur colo-apps

Les clients Android/Linux interrogent `https://colo-apps.les-crevettes-cevenoles.fr/releases/manifest.json`
au lieu de l'API GitHub. Le serveur synchronise la dernière release depuis GitHub.

## Installation sur le serveur

```bash
sudo mkdir -p /var/lib/colo-apps/releases
sudo cp deploy/releases/sync-from-github.sh /opt/colo-apps/
sudo chmod +x /opt/colo-apps/sync-from-github.sh

# Première synchro manuelle
sudo COLO_RELEASES_DIR=/var/lib/colo-apps/releases /opt/colo-apps/sync-from-github.sh

# Cron : vérifie GitHub toutes les 5 minutes
echo '*/5 * * * * root COLO_RELEASES_DIR=/var/lib/colo-apps/releases /opt/colo-apps/sync-from-github.sh >>/var/log/colo-apps-releases.log 2>&1' \
  | sudo tee /etc/cron.d/colo-apps-releases
```

Mettre à jour nginx (`deploy/relay/nginx-colo-apps.conf`) puis :

```bash
sudo cp deploy/relay/nginx-colo-apps.conf /etc/nginx/sites-available/colo-apps
sudo nginx -t && sudo systemctl reload nginx
```

## manifest.json

```json
{
  "version": "0.27.14",
  "publishedAt": "2026-08-30T11:42:15Z",
  "notes": "Texte affiché avant installation",
  "apkUrl": "https://colo-apps…/releases/colocourse-v0.27.14-arm64.apk",
  "appImageUrl": "https://colo-apps…/ColoCourse-0.27.14-x86_64.AppImage",
  "releaseUrl": "https://github.com/…/releases/tag/v0.27.14",
  "changelog": [{ "version": "…", "notes": "…", "publishedAt": "…" }]
}
```

Le script supprime les anciens APK/AppImage à chaque mise à jour (un seul jeu d'artefacts).
