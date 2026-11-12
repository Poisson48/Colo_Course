# Relais Nostr — fichiers de déploiement

Dernière mise à jour : **2026-11-12**. Aucun TURN, aucun STUN, aucun signaling.

| Élément | Valeur |
|---|---|
| URL | `wss://colo-apps.les-crevettes-cevenoles.fr` |
| Backend | Strfry Docker → `127.0.0.1:7777` |
| TLS | nginx `:443` → `127.0.0.1:11443` |
| Kind accepté | `4545` |
| Taille max | 256 Ko |

Fichiers de ce dossier :

- `nginx-colo-apps.conf` : relais + `/push` + `/ntfy/` + `/releases/` ;
- `../relay/write-policy.py` (ou équivalent Strfry) : n'accepte que le kind 4545.

## Vérification

```bash
docker compose -f ~/colocourse-relay/docker-compose.yml ps
curl -fsS https://colo-apps.les-crevettes-cevenoles.fr/
```

Voir [docs/RELAY.md](../../docs/RELAY.md) et
[docs/SYNC-SERVER.md](../../docs/SYNC-SERVER.md).
