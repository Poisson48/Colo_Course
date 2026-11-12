# Serveur de synchronisation

Dernière mise à jour : **2026-11-12**. Architecture actuelle : relais Nostr
store-and-forward + broker de notifications. **Pas de P2P, pas de TURN, pas de
WebRTC.**

## Vue d'ensemble

```text
Appareil A ── wss ──► Strfry (relais Nostr) ── wss ──► Appareil B
     │                                               │
     └── POST /push ──► broker colopush ── FCM ──────┘
                              └──────── UnifiedPush ─┘
```

| Composant | Rôle | Port / URL |
|---|---|---|
| Strfry | relais Nostr kind 4545, stockage des blobs chiffrés | `127.0.0.1:7777`, nginx `:443` `/` |
| colopush | broker FCM + UnifiedPush | `127.0.0.1:7780`, nginx `/push` |
| ntfy | serveur de notifications / distributeur | `127.0.0.1:8077`, nginx `/ntfy/` |
| nginx | TLS, reverse proxy | `:443` |
| releases | APK/AppImage/manifest | `/releases/` |

## Relais Nostr

- URL par défaut : `wss://colo-apps.les-crevettes-cevenoles.fr`.
- Kind `4545`, tag `t=channelTag`, contenu chiffré XChaCha20-Poly1305.
- Le relais ne peut pas lire les listes ; il voit les métadonnées (listId,
  channelTag, tailles, horodatages, IP).
- Écriture autorisée uniquement pour le kind 4545 (`write-policy.py`).
- `maxEventSize` 256 Ko pour les vignettes.
- Rétention : les deltas acquittés peuvent être purgés ; un snapshot est publié
  périodiquement par les clients.

## Broker colopush

- Node.js sans dépendance, systemd `colopush.service`, utilisateur `colosync`.
- `/push` : FCM data-only + fan-out UnifiedPush.
- `/config` : capacités.
- Secrets : compte de service FCM dans
  `/data/leo/colo-apps/secrets/fcm-service-account.json` (root:colosync, 640).
- Registre UnifiedPush :
  `/data/leo/colo-apps/push/endpoints.json` (root:colosync, 660).

## Déploiement

```bash
# relais
cd ~/colocourse-relay && docker compose up -d

# broker
install -d -m 750 -o root -g colosync /data/leo/colo-apps/push
install -m 750 -o root -g colosync deploy/push/server.mjs /data/leo/colo-apps/push/server.mjs
systemctl restart colopush

# nginx
sudo cp deploy/relay/nginx-colo-apps.conf /etc/nginx/sites-available/colo-apps
sudo nginx -t && sudo systemctl reload nginx
```

Vérifications :

```bash
curl -fsS https://colo-apps.les-crevettes-cevenoles.fr/push
curl -fsS https://colo-apps.les-crevettes-cevenoles.fr/config
systemctl is-active colopush nginx
```

## Sauvegarde et perte

- Le relais conserve les deltas jusqu'à acquittement ; au-delà, un snapshot peut
  être redemandé aux clients en ligne.
- Si **tous** les appareils disparaissent, une liste peut être perdue. Accepté
  pour des listes de courses/tâches.
- Les volumes Strfry/ntfy ne doivent pas être supprimés lors d'un déploiement.

## Sécurité

- Aucune clé de liste sur le serveur.
- Pas de comptes : l'appartenance = possession de la clé.
- Le broker ne transporte aucun contenu de liste.
- Rate-limit par topic (5 s) et taille de body limitée.
- Les endpoints UnifiedPush sont des URL opaques ; ne pas les publier.

## Ancienne architecture

Les documents P2P/WebRTC/TURN et les plans de hub alternatif ont été retirés :
cette architecture n'est plus maintenue. Voir l'historique Git si nécessaire.
