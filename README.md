# Colo_Course

**Liste de courses partagée pour couples et colocataires — sans compte, sans abonnement, sans serveur à soi.**

Une app minimaliste de gestion de liste de courses : créez une liste, partagez-la par QR code, collaborez en temps réel. Chaque participant garde une copie locale complète, chiffrée de bout en bout. Fonctionne hors ligne, gère les conflits d'édition simultanée. Plusieurs listes avec des participants différents. Open source, GPL v3.

[![CI](https://github.com/Poisson48/Colo_Course/actions/workflows/ci.yml/badge.svg)](https://github.com/Poisson48/Colo_Course/actions/workflows/ci.yml)

<p align="center">
  <img src="docs/screenshots/lists.png" width="250" alt="Écran des listes">
  <img src="docs/screenshots/items.png" width="250" alt="Articles d'une liste">
  <img src="docs/screenshots/share.png" width="250" alt="Partage par QR code">
</p>

## Comment ça marche

### Architecture
- **Copie locale complète** : chaque appareil stocke tous les items et l'historique en SQLite.
- **Sync chiffrée E2E** : les données circulent via des relais Nostr publics (store-and-forward gratuit). Les relais ne voient que des blobs chiffrés ; la clé de déchiffrement reste sur l'appareil.
- **Sans conflits** : CRDT maison avec Last-Writer-Wins par champ (nom, quantité, coché) et horloges de Lamport. Les modifications simultanées convergent identiquement sur tous les appareils, sans intervention.
- **Appairage simple** : créez une liste, puis soit l'autre appareil **scanne le QR code**, soit vous lui **envoyez le lien** `colocourse://` (WhatsApp, SMS…) — un appui dessus ouvre l'app et rejoint la liste. Le lien porte la clé : ne le publiez pas.
- **Plusieurs listes** : gérez autant de listes qu'il y a de contextes (courses à la maison, coloc, boulot…), chacune avec ses participants.
- **Hors ligne d'abord** : les modifications s'ajoutent immédiatement localement dans une file d'attente. Au retour du réseau, la sync reprend automatiquement.

### Cycle de vie
1. Créer ou rejoindre une liste via QR code / URI.
2. Ajouter / cocher / supprimer des articles localement (instantané).
3. Chiffrer et envoyer vers les relais Nostr.
4. Réception et merge automatiques chez les autres : convergence garantie.
5. Notifications locales à la réception de changements (quand l'app est ouverte).

## Build & installation

### Prérequis
- **Ubuntu/Debian** : `bash scripts/setup-dev.sh` installe les dépendances (Qt 6, libsodium, libsecp256k1, CMake, Ninja).
- **Autres OS** : adapter manuellement selon les paquets disponibles. Voir `scripts/setup-dev.sh` pour la liste complète.

### Build desktop Linux
```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Binaire exécutable : `build/src/colocourse`

### Android (arm64)

**Sans rien installer** : chaque push construit un APK signé. Récupérez-le dans les artefacts
du dernier run de la CI (onglet *Actions*), ou dans les [Releases](../../releases) pour une
version taguée. Puis :

```bash
adb install -r colocourse-arm64.apk
```
(ou copiez l'APK sur le téléphone et ouvrez-le, en autorisant l'installation depuis cette source).

L'APK est signé avec une **clé de debug** : il s'installe sans souci mais ne passe pas par le
Play Store. Une mise à jour exige la même provenance — sinon, désinstallez d'abord.

**Build local** (~7 Go de téléchargements : SDK, NDK, Qt pour Android) :
```bash
bash scripts/setup-android.sh   # une fois
bash scripts/build-android.sh   # → colocourse-arm64.apk, signé
```

### Publier une version

```bash
git tag -a v0.2.0 -m "…" && git push origin v0.2.0
```
Le workflow *Release* construit l'APK et le publie dans les Releases GitHub.

## État du projet

- **Desktop Linux (bêta)** : squelette + app fonctionnelle, tests CRDT complets, sync relais opérationnelle, chiffrement E2E, appairage par QR. Prêt pour expérimentation.
- **Android (bêta)** : APK arm64 construit par la CI, notifications système à la réception d'un changement.

Plan détaillé et workflow : [docs/PLAN.md](docs/PLAN.md)  
Spécification technique (CRDT, protocole relais, format données) : [docs/SPEC.md](docs/SPEC.md)

## Limitations connues

- **Notifications hors app** : l'app doit être ouverte (ou en premier plan sur mobile) pour recevoir les changements en temps réel. Pas de push notification Firebase ou FCM par choix architectural (pas de serveur tiers, de compte requis). Piste v2 : foreground service Android optionnel.
- **Révocation d'un membre** : non supportée en v1. Pour exclure quelqu'un d'une liste, recréez-la et repartagez la clé avec les nouveaux membres uniquement.
- **Nettoyage des tombstones** : les éléments supprimés sont purgés localement après 30 jours sans modification. Un pair très en retard peut théoriquement ressusciter un item purgé (très rare, accepté).

## Licence

GPLv3. Voir [LICENSE](LICENSE).  
Basé sur Qt 6 (LGPL), libsodium (ISC), libsecp256k1 (MIT).
