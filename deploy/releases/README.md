# Mises à jour hébergées sur colo-apps

Dernière mise à jour : **2026-11-12**.

Les clients interrogent `https://colo-apps.les-crevettes-cevenoles.fr/releases/manifest.json`
plutôt que l'API GitHub. Le serveur synchronise la dernière release GitHub.

## Installation

```bash
cd /opt/colo-apps/Colo_Course
git pull
sudo ./deploy/releases/install-all.sh
```

Ou manuellement :

```bash
sudo mkdir -p /var/lib/colo-apps/releases
sudo cp deploy/releases/sync-from-github.sh /opt/colo-apps/
sudo chmod +x /opt/colo-apps/sync-from-github.sh
sudo COLO_RELEASES_DIR=/var/lib/colo-apps/releases /opt/colo-apps/sync-from-github.sh

echo '*/5 * * * * root COLO_RELEASES_DIR=/var/lib/colo-apps/releases /opt/colo-apps/sync-from-github.sh >>/var/log/colo-apps-releases.log 2>&1' \
  | sudo tee /etc/cron.d/colo-apps-releases
```

Puis recharger nginx si `deploy/relay/nginx-colo-apps.conf` a changé :

```bash
sudo cp deploy/relay/nginx-colo-apps.conf /etc/nginx/sites-available/colo-apps
sudo nginx -t && sudo systemctl reload nginx
```

## manifest.json

```json
{
  "version": "0.34.13",
  "publishedAt": "2026-11-12T…Z",
  "notes": "Notes affichées avant installation",
  "apkUrl": "https://…/releases/colocourse-v0.34.13-arm64.apk",
  "appImageUrl": "https://…/ColoCourse-0.34.13-x86_64.AppImage",
  "releaseUrl": "https://github.com/…/releases/tag/v0.34.13",
  "changelog": [{ "version": "0.34.13", "notes": "…", "publishedAt": "…" }]
}
```

Le script ne conserve qu'un seul jeu d'artefacts.

## VersionCode

- `versionName` = tag sans `v`.
- `versionCode` = `10000 + nombre de commits`, pour rester au-dessus des APK de
  test (`Test APK`, versionCode 9999).
- Ne jamais publier une release avec un `versionCode` inférieur à celui déjà
  installé : Android refuse la mise à jour.

## APK de test

Workflow **Test APK** (`workflow_dispatch`) : construit un APK signé avec la clé
de release **sans publier de Release**. Il permet de tester une version par-dessus
l'app installée. Ne pas le distribuer.

## Catalogue de recettes

`recipe_library.json` (~60 Mo) est servi au même endroit :

- `https://colo-apps…/releases/recipe_library.json`
- `https://colo-apps…/releases/recipes-manifest.json`

Synchronisation : `deploy/releases/sync-recipes.sh`, cron horaire.
L'app utilise le cache local, sinon la copie embarquée, puis le manifest serveur.
