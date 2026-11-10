# Héberger son propre serveur Colo Sync

Le serveur P2P est **indépendant du relais Nostr**. On peut self-host :
- le relais Strfry (`wss://…`), qui stocke les deltas/snaps/vignettes chiffrés ;
- le signaling/TURN (`https://…`), qui ne stocke aucune donnée de liste.

## Prérequis

- Ubuntu 24.04 (ou Debian 12), accès root.
- Un domaine qui pointe sur la machine (`A`/`AAAA`).
- Un certificat Let's Encrypt pour ce domaine (nginx termine TLS sur 443).
- Ports ouverts sur la box/routeur :
  - 80/tcp et 443/tcp (TLS + ACME)
  - 3479/udp + 3479/tcp (STUN/TURN)
  - 5350/tcp (TURNS)
  - 50000–50100/udp (relais TURN)
- Docker pour Strfry (le reste est natif : Node, coturn, nginx).

## Installation

```bash
git clone https://github.com/Poisson48/Colo_Course.git
cd Colo_Course
cp deploy/selfhost/.env.example deploy/selfhost/.env
$EDITOR deploy/selfhost/.env
sudo bash deploy/selfhost/install.sh
```

Le script :
1. vérifie le certificat et les prérequis ;
2. installe le signaling sur `127.0.0.1:7778` ;
3. patche **uniquement** le vhost nginx (locations `/signal`, `/config`,
   `/turn-credentials`, `/healthz`) ;
4. installe coturn sur 3479/5350 sans toucher au 3478 ;
5. génère le secret TURN et les credentials éphémères.

## Configurer l'app

Dans **Synchronisation** :
- **Relais Nostr** : `wss://VOTRE-DOMAINE`
- **Serveur P2P** : `https://VOTRE-DOMAINE`
- Mode : **Hybride (recommandé)**.

Le serveur P2P expose :
- `GET /config` : capacités (protocole, apps, URLs ICE) ;
- `GET /turn-credentials` : credentials TURN éphémères (HMAC, TTL 12 h) ;
- `WS /signal` : découverte des pairs + SDP/ICE ;
- `GET /healthz` : état local.

## Données et sauvegardes

- Signaling : aucun stockage métier.
- Strfry : volume Docker (deltas/snaps/vignettes chiffrés).
- TURN : aucun stockage.
- Sauvegarder le volume Strfry et `/etc/letsencrypt`, pas besoin de sauvegarder le signaling.

## Mise à jour

```bash
cd Colo_Course && git pull --ff-only
sudo bash deploy/server/deploy-signaling.sh
sudo bash deploy/server/deploy-turn.sh
```

## Rollback

```bash
sudo bash deploy/server/rollback.sh /chemin/backup
```

Les données Strfry/ntfy ne sont pas touchées.

## Sécurité

- Le signaling n'écoute que sur `127.0.0.1` ; nginx expose HTTPS/WSS.
- Aucune clé de liste ni contenu de liste n'est envoyé au signaling.
- Le secret TURN reste sur le serveur ; l'app ne reçoit que des credentials
  éphémères.
- TURN peut relayer du trafic chiffré (DTLS) mais ne le stocke pas.
