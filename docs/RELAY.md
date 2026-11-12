# Relais Nostr — exploitation

Dernière mise à jour : **2026-11-12**.

| Élément | Valeur |
|---|---|
| URL | `wss://colo-apps.les-crevettes-cevenoles.fr` |
| Backend | Strfry Docker → `127.0.0.1:7777` |
| TLS | nginx `:443` → `127.0.0.1:11443` |
| Certificat | Let's Encrypt DNS-01 |
| Kind accepté | `4545` uniquement |
| Taille max | 256 Ko |

**Aucun STUN/TURN, aucun signaling, aucun port WebRTC.**

## Démarrage

```bash
cd ~/colocourse-relay
docker compose up -d
```

Fichiers : `strfry.conf`, `write-policy.py`, `docker-compose.yml`.

## Politique d'écriture

Chaque liste utilise une clé Nostr dérivée de la clé de liste. Une whitelist de
pubkeys ne peut donc pas fonctionner : `write-policy.py` accepte uniquement le
kind 4545.

`maxEventSize` doit rester à **256 Ko** : les vignettes chiffrées peuvent dépasser
le défaut Strfry de 64 Ko.

Symptômes :

- modifications locales mais jamais reçues ailleurs → politique d'écriture ou
  taille max ;
- `event too large` → augmenter `maxEventSize` ;
- `blocked: pubkey … not in whitelist` → write-policy mal configurée.

## Vérifications

```bash
docker compose ps
curl -fsS https://colo-apps.les-crevettes-cevenoles.fr/
systemctl is-active nginx
```

Les sauvegardes des volumes Strfry/ntfy sont à conserver. Ne pas supprimer les
volumes lors d'une mise à jour du relais.

## Voir aussi

- [SYNC-SERVER.md](SYNC-SERVER.md) — architecture complète ;
- [PROTOCOL.md](../PROTOCOL.md) — format des événements et chiffrement ;
- `deploy/relay/README.md` — fichiers de déploiement.
