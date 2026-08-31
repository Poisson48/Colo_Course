---
name: colocourse-release
description: >-
  Publie une release Colo Course (tag Git → GitHub Actions → colo-apps).
  Use when the user asks for a release, deploy, publish APK/AppImage, sync
  serveur colo-apps, or mentions release.yml / sync-from-github.sh.
---

# Colo Course — Release (GitHub Actions + serveur)

## Vue d'ensemble

```
commit main → tag vX.Y.Z → push tag
       ↓
.github/workflows/release.yml  (APK arm64 + AppImage x86_64 → GitHub Release)
       ↓ (cron */5 min sur colo-apps)
deploy/releases/sync-from-github.sh  → manifest.json + binaires sur le serveur
```

Les clients interrogent `https://colo-apps.les-crevettes-cevenoles.fr/releases/manifest.json` — pas GitHub directement.

## Checklist release

```
- [ ] Changements commités sur main
- [ ] Tests OK localement (ctest ou tst_qml ciblé)
- [ ] Tag annoté créé (message = changelog affiché dans l'app)
- [ ] Tag poussé sur origin
- [ ] Workflow Release vert sur GitHub Actions
- [ ] (Optionnel) synchro serveur immédiate si urgent
```

## 1. Préparer et taguer

Version : incrémenter selon le changement (patch = fix, minor = feature).

```bash
# Depuis la racine du dépôt
git status
git pull origin main

# Tag annoté — le message EST le changelog affiché avant installation
git tag -a v0.30.1 -m "$(cat <<'EOF'
Correction affichage onglet Catalogue

- Suppression ScrollBar invalides sur ListView des catégories (panneau vide)
EOF
)"

git push origin main
git push origin v0.30.1
```

**Important** : le corps de la release GitHub est construit à partir du message du tag. L'app n'affiche que la partie **avant** le séparateur `---` (cf. `Updater::notesFromBody`).

## 2. GitHub Actions (`release.yml`)

Déclenché par `push` sur tag `v*`.

| Job | Rôle |
|-----|------|
| `version` | `versionName` = tag sans `v`, `versionCode` = nombre de commits |
| `apk` | Réutilise `android.yml` (APK arm64 signé, clé release requise) |
| `appimage` | Réutilise `desktop.yml` (AppImage x86_64) |
| `publish` | Crée/met à jour la GitHub Release avec APK + AppImage |

Secrets requis (repo GitHub) : clé de signature Android release (`ANDROID_RELEASE_*` — voir `android.yml`).

Vérifier :

```bash
gh run list --workflow=release.yml --limit 3
gh release view v0.30.1
```

## 3. Serveur colo-apps

### Première installation

Sur le serveur, depuis un clone à jour :

```bash
cd /opt/colo-apps/Colo_Course
git pull
sudo ./deploy/releases/install-all.sh
```

Installe nginx `/releases/`, scripts, cron, et lance la première synchro.

### Synchro manuelle (après une release)

Le cron tourne toutes les **5 minutes**. Pour forcer immédiatement :

```bash
sudo COLO_RELEASES_DIR=/var/lib/colo-apps/releases \
  /opt/colo-apps/sync-from-github.sh
```

### Catalogue recettes (séparé)

```bash
sudo COLO_RELEASES_DIR=/var/lib/colo-apps/releases \
  COLO_REPO_DIR=/opt/colo-apps/Colo_Course \
  /opt/colo-apps/sync-recipes.sh
```

Cron recettes : toutes les heures (`/etc/cron.d/colo-apps-recipes`).

### Vérification serveur

```bash
curl -fsSL https://colo-apps.les-crevettes-cevenoles.fr/releases/manifest.json | python3 -m json.tool
tail -20 /var/log/colo-apps-releases.log
```

Attendu dans `manifest.json` : `version`, `apkUrl`, `appImageUrl`, `notes`, `changelog`.

## 4. Fichiers clés

| Fichier | Rôle |
|---------|------|
| `.github/workflows/release.yml` | Pipeline tag → artefacts → GitHub Release |
| `.github/workflows/android.yml` | Build APK (réutilisé par release) |
| `.github/workflows/desktop.yml` | Build AppImage (réutilisé par release) |
| `deploy/releases/sync-from-github.sh` | Télécharge dernière release → serveur |
| `deploy/releases/sync-recipes.sh` | Publie `recipe_catalog.db` + manifest |
| `deploy/releases/install-all.sh` | Setup complet serveur |
| `deploy/releases/README.md` | Doc détaillée manifest.json |

## 5. Dépannage

| Symptôme | Cause probable | Action |
|----------|----------------|--------|
| Workflow Release rouge | Secret Android manquant / build échoué | `gh run view <id> --log-failed` |
| manifest.json pas à jour | Cron pas encore passé | Lancer `sync-from-github.sh` manuellement |
| App ne voit pas la MAJ | Version serveur = version locale | Vérifier `manifest.json` version |
| Catalogue recettes absent | `sync-recipes.sh` pas lancé | Vérifier `/var/log/colo-apps-recipes.log` |

## 6. Convention de version

- Tags : `vX.Y.Z` (ex. `v0.30.1`)
- Dernière release stable visible : `git tag -l 'v*' --sort=-v:refname | head -5`
- Ne pas réutiliser un tag existant ; créer un nouveau numéro.
