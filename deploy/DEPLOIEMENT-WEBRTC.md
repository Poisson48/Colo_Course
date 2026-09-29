# Colo Course — Rapport de déploiement WebRTC v0.31.0
_Généré le 2026-09-29 — Tout est prêt, il manque juste l'ouverture des ports sur la box_

---

## ✅ Ce qui est fait (maintenant)

### Code
- **Tag `v0.31.0` poussé** → GitHub Actions build l'APK + AppImage
- **Lien release** : https://github.com/Poisson48/Colo_Course/actions (en cours)
- **Branche** : `main` (merge depuis `feat/p2p-sharing`)
- **15/17 tests passent** (2 échecs pré-existants, pas liés au WebRTC)

### Fonctionnalités dans l'app
- **Switch "P2P direct (WebRTC)"** dans le dialog relais (paramètres)
- **Champs signaling URL et ICE servers** (configurables, defaults pré-remplis)
- **STUN par défaut** : Google public (`stun.l.google.com:19302`) — fonctionne immédiatement
- **Mode Nostr** : toujours fonctionnel (connecté au relay colo crevettes)
- **WebRTCTransport** : code complet (libdatachannel, signaling, ICE, data channels)
- **Signaling server** : Node.js WebSocket sur port 7778

### Scripts de déploiement serveur
- `deploy/install-webrtc.sh` — script unique (signaling + coturn)
- `deploy/signaling/deploy-signaling.sh` — signaling server seul
- `deploy/coturn/install.sh` — coturn seul
- `deploy/DEPLOIEMENT-WEBRTC.md` — guide complet

### Monitoring
- **Cron actif** : vérifie les ports 3479 et 5350 toutes les 30 minutes
- **Log** : `/tmp/colo-port-check.log`
- **Auto-déploiement** : dès que les ports sont ouverts, le script SSH sur le serveur et lance `install-webrtc.sh`

---

## ⏳ Ce qui reste (ce soir)

### Étape 1 : Ouvrir les ports sur la box

| Port | Protocole | Usage |
|------|-----------|-------|
| 3479 | UDP + TCP | STUN/TURN coturn |
| 5350 | TCP | TURNS (TLS) |
| 50000-50100 | UDP | Relay data WebRTC |

⚠️ **NE PAS toucher au port 3478** — STUN meownopoly (jeu + chat).

### Étape 2 : Vérifier que le cron a détecté l'ouverture

```bash
# Voir le log du monitoring
tail -f /tmp/colo-port-check.log

# Ou forcer une vérification manuelle
/home/leo/leo/Colo_Course/scripts/check-ports-and-deploy.sh
```

Quand les ports seront ouverts, le log affichera :
```
🎉 Tous les ports sont ouverts ! Lancement du déploiement...
✅ Déploiement WebRTC terminé avec succès !
```

### Étape 3 (si le cron ne peut pas SSH) : déploiement manuel

```bash
ssh ton-user@78.122.112.36
cd /opt/colo-apps/Colo_Course
git pull origin main
sudo ./deploy/install-webrtc.sh
```

**Note le secret TURN** affiché à la fin du script.

### Étape 4 : Tester l'app

1. Installer l'APK v0.31.0 depuis GitHub Releases
2. Ouvrir les paramètres → dialog relais
3. Activer le switch "P2P direct (WebRTC)"
4. Les champs sont pré-remplis (signaling + ICE servers)
5. Valider → l'app se connecte au signaling et cherche des pairs

---

## Architecture réseau finale

```
┌───────────────────────────────────────────────────────────────────┐
│                    Internet (78.122.112.36)                        │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  :443 ──▶ nginx SNI router                                       │
│           ├─ colo-apps.*   → :11443                               │
│           │   ├─ /          → Strfry :7777    (Nostr relay)       │
│           │   ├─ /ntfy/     → ntfy :8077     (push)               │
│           │   ├─ /releases/ → fichiers statiques                  │
│           │   └─ /signal    → signaling :7778 (WebRTC ⭐)         │
│           ├─ cloud.*       → :12443 → Nextcloud :8083             │
│           ├─ carte.*       → :13443 → Node :18542                 │
│           └─ default       → :4443  → meownopoly                  │
│                                                                   │
│  :3478 ──▶ STUN meownopoly — NE PAS TOUCHER                      │
│  :3479 ──▶ coturn STUN/TURN — ⭐ NOUVEAU (à ouvrir)              │
│  :5350 ──▶ coturn TURNS TLS — ⭐ NOUVEAU (à ouvrir)              │
│  :50000-50100/udp ──▶ coturn relay — ⭐ NOUVEAU (à ouvrir)        │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

---

## Fichiers clés dans le repo

```
deploy/
├── DEPLOIEMENT-WEBRTC.md       ← ce rapport (dans le repo)
├── install-webrtc.sh           ← script unique (signaling + coturn)
├── coturn/
│   ├── turnserver.conf         ← config (3479/5350/50000-50100)
│   ├── install.sh              ← installe coturn
│   └── README.md
├── signaling/
│   ├── server.js               ← signaling WebSocket (:7778)
│   ├── package.json            ← deps (ws)
│   └── deploy-signaling.sh     ← déploie + systemd + nginx
└── relay/
    └── nginx-colo-apps.conf    ← modifié : /signal → :7778

scripts/
└── check-ports-and-deploy.sh   ← cron monitoring (toutes les 30 min)

src/
├── net/synctransport.h/.cpp    ← interface abstraite transport
├── net/webrtctransport.h/.cpp  ← transport WebRTC P2P
├── net/relaypool.h/.cpp        ← refactoré (hérite SyncTransport)
├── app/appcontroller.cpp       ← toggle syncMode, ICE servers
└── qml/ListsPage.qml           ← UI toggle + champs config
```

---

## Commandes utiles

```bash
# Vérifier le statut du cron
crontab -l | grep colo

# Voir les logs du monitoring
tail -f /tmp/colo-port-check.log

# Forcer une vérification
/home/leo/leo/Colo_Course/scripts/check-ports-and-deploy.sh

# Vérifier la release GitHub
gh run list --limit 3

# Vérifier les ports depuis l'extérieur
curl -sS "https://check-host.net/check-tcp?host=78.122.112.36:3479&max_nodes=3" -H "Accept: application/json"
```

---

## Rappels

- **Ne pas `pkill node`** — plusieurs projets Node tournent sur le serveur
- **Pas de NAT loopback** — tester avec check-host.net, pas `curl 78.122.112.36:port`
- **ufw inactif** — les ports sont contrôlés par la box uniquement
- **Secret TURN** — généré à l'installation coturn, à noter pour la config app
- **STUN Google** — fonctionne immédiatement (pas besoin du serveur coturn pour le STUN)
- **TURN** — nécessaire seulement si les deux pairs sont derrière un NAT symétrique