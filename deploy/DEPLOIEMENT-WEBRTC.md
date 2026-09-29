# Colo Course — Déploiement WebRTC (ce soir)
_Rapport généré le 2026-09-29_

---

## Étape 1 : Ouvrir les ports sur la box

Connecte-toi à l'interface de la box et ajoute ces forwards vers `78.122.112.36` :

| Port | Protocole | Usage |
|------|-----------|-------|
| 3479 | UDP | STUN/TURN |
| 3479 | TCP | STUN/TURN fallback (réseaux restrictifs) |
| 5350 | TCP | TURNS (TURN over TLS) |
| 50000-50100 | UDP | Relay data WebRTC (100 ports) |

⚠️ **NE PAS toucher au port 3478** — c'est le STUN meownopoly (jeu + chat d'un autre utilisateur).

Le signaling WebSocket passe par le port 443 (déjà ouvert via nginx), pas besoin de port supplémentaire.

---

## Étape 2 : Déployer sur le serveur

### Vérifier que les ports sont ouverts (depuis ton PC, après ouverture box)

```bash
# Tester l'accessibilité du 3479 depuis l'extérieur
curl -sS "https://check-host.net/check-tcp?host=78.122.112.36:3479&max_nodes=3" -H "Accept: application/json"
# Attendre 10s puis récupérer le résultat avec le request_id retourné
```

### SSH sur le serveur et lancer le script

```bash
ssh ton-user@78.122.112.36

cd /opt/colo-apps/Colo_Course
git pull origin feat/p2p-sharing   # ou main selon où on en sera

sudo ./deploy/install-webrtc.sh
```

### Ce que fait le script (environ 2 min) :

1. **Signaling server** (1/2)
   - Copie `deploy/signaling/server.js` + `package.json` → `/opt/colo-apps/signaling/`
   - `npm install --production` (installe le module `ws`)
   - Crée le service systemd `colo-signaling.service` (port 7778, restart=always)
   - Met à jour nginx : ajout `location /signal` → `127.0.0.1:7778`
   - `nginx -t && systemctl reload nginx`

2. **coturn STUN/TURN** (2/2)
   - Vérifie que les ports 3479/5350 sont libres
   - Vérifie que le STUN meownopoly (:3478) est intact
   - `apt install coturn`
   - Génère un **secret TURN aléatoire** (affiché dans le terminal → **le noter !**)
   - Active et démarre le service coturn

### À la fin du script, tu verras quelque chose comme :

```
✅ WebRTC complet !

  Signaling : wss://colo-apps.les-crevettes-cevenoles.fr/signal
  STUN      : stun:colo-apps.les-crevettes-cevenoles.fr:3479
  TURN      : turn:colo-apps.les-crevettes-cevenoles.fr:3479
  TURNS     : turns:colo-apps.les-crevettes-cevenoles.fr:5350

  ⚠  Vérifier que ces ports sont ouverts sur la box :
     3479/udp, 3479/tcp, 5350/tcp, 50000-50100/udp

  Le STUN meownopoly (:3478) n'a PAS été touché.
```

**🔑 Note le secret TURN affiché** — on en a besoin pour la suite (config côté app).

---

## Étape 3 : Vérifier que tout marche

### Vérifier les services sur le serveur

```bash
# Signaling server
systemctl status colo-signaling
curl -sS -o /dev/null -w "%{http_code}" http://127.0.0.1:7778/

# coturn
systemctl status coturn
ss -ulnp | grep 3479   # STUN/TURN UDP
ss -tlnp | grep 5350   # TURNS TLS

# STUN meownopoly toujours intact
ss -ulnp | grep 3478

# nginx
nginx -t
systemctl status nginx
```

### Vérifier depuis l'extérieur (ton PC)

```bash
# STUN/TURN joignable ?
curl -sS "https://check-host.net/check-tcp?host=78.122.112.36:3479&max_nodes=3" -H "Accept: application/json"

# TURNS joignable ?
curl -sS "https://check-host.net/check-tcp?host=78.122.112.36:5350&max_nodes=3" -H "Accept: application/json"

# Signaling (passe par nginx :443)
curl -sS https://colo-apps.les-crevettes-cevenoles.fr/signal
# → doit répondre (même une erreur HTTP 426 Upgrade Required = OK, c'est WebSocket)
```

---

## Inventaire complet du serveur (78.122.112.36)

### Sites via nginx SNI router (:443)

| Domaine | Port interne | Service |
|---|---|---|
| `colo-apps.les-crevettes-cevenoles.fr` | `:11443` | Strfry (:7777) + ntfy (:8077) + releases + **signaling (:7778)** |
| `cloud.racines-et-chemins-cevenols.fr` | `:12443` | Nextcloud (:8083 Docker) |
| `carte.racines-et-chemins-cevenols.fr` | `:13443` | Mémoire des Cévennes (:18542 Node.js) |
| `memoires-cevenoles.les-crevettes-cevenoles.fr` | `:8443` | 301 redirect → carte |
| `default` (pattounecorp.ovh) | `:4443` | meownopoly asset_server |

### Ports directs (hors nginx)

| Port | Service | User |
|---|---|---|
| 3478 | STUN meownopoly | meow-server |
| 3479 | **coturn STUN/TURN (NOUVEAU)** | root |
| 5350 | **coturn TURNS TLS (NOUVEAU)** | root |
| 50000-50100 | **coturn relay UDP (NOUVEAU)** | root |
| 7778 | **signaling server (NOUVEAU)** | root |

### Box — ports forwardés

| Port | Usage |
|---|---|
| 443 | nginx SNI (tous les sites HTTPS) |
| 18542 | Mémoire des Cévennes (accès direct) |
| 3478 | STUN meownopoly |
| **3479** | **STUN/TURN coturn (À OUVRIR)** |
| **5350** | **TURNS TLS (À OUVRIR)** |
| **50000-50100** | **relay UDP (À OUVRIR)** |

---

## Architecture réseau finale

```
┌─────────────────────────────────────────────────────────────────┐
│                        Internet (78.122.112.36)                  │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  :443 ──▶ nginx SNI router                                      │
│           ├─ colo-apps.*   → :11443                             │
│           │   ├─ /          → Strfry :7777    (Nostr relay)     │
│           │   ├─ /ntfy/     → ntfy :8077     (push)             │
│           │   ├─ /releases/ → fichiers statiques                │
│           │   └─ /signal    → signaling :7778 (WebRTC ⭐ NOUVEAU)│
│           ├─ cloud.*       → :12443 → Nextcloud :8083           │
│           ├─ carte.*       → :13443 → Node :18542               │
│           └─ default       → :4443  → meownopoly                │
│                                                                 │
│  :3478 ──▶ STUN meownopoly (jeu + chat) — NE PAS TOUCHER       │
│  :3479 ──▶ coturn STUN/TURN (Colo Course) — ⭐ NOUVEAU          │
│  :5350 ──▶ coturn TURNS TLS — ⭐ NOUVEAU                       │
│  :50000-50100/udp ──▶ coturn relay — ⭐ NOUVEAU                 │
│                                                                 │
│  :18542 ──▶ Mémoire des Cévennes                               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Fichiers dans le repo

```
deploy/
├── install-webrtc.sh           ← script unique (lance les 2 ci-dessous)
├── coturn/
│   ├── turnserver.conf         ← config coturn (3479/5350/50000-50100)
│   ├── install.sh              ← installe coturn
│   └── README.md               ← doc coturn
├── signaling/
│   ├── server.js               ← signaling WebSocket (port 7778)
│   ├── package.json            ← deps (ws)
│   ├── deploy-signaling.sh     ← déploie signaling + systemd + nginx
│   └── (pas encore de README)
└── relay/
    ├── nginx-colo-apps.conf    ← modifié : ajout /signal → :7778
    └── README.md               ← mis à jour avec STUN/TURN

src/
├── app/appcontroller.cpp       ← ICE servers → port 3479
├── net/webrtctransport.cpp     ← STUN par défaut → 3479
├── net/webrtctransport.h       ← commentaire corrigé
└── net/synctransport.h         ← interface abstraite (inchangé)
```

---

## Rappels importants

- **Ne pas toucher au port 3478** (STUN meownopoly)
- **Le secret TURN** est généré à l'installation — le noter pour la config app
- **Le signaling passe par nginx** (:443) — aucun port supplémentaire nécessaire
- **ufw est inactif** sur le serveur — les ports sont contrôlés par la box uniquement
- **`pkill node` est interdit** — le serveur a plusieurs projets Node (meownopoly, loto, AgentDVR…)
- **Pas de NAT loopback** — pour tester l'accès externe, utiliser check-host.net, pas `curl 78.122.112.36:port`

---

## Après le déploiement

Une fois le secret TURN récupéré, il faudra :
1. Configurer le secret dans l'app (credential pour l'auth TURN)
2. Déployer le signaling server aussi dans nginx (déjà fait dans le nginx-colo-apps.conf)
3. Tester le mode P2P entre deux appareils

Le code WebRTC (WebRTCTransport, SyncEngine refactor, QML toggle) est déjà fait dans la session précédente sur la branche `feat/p2p-sharing`.