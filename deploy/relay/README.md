# Relais Nostr — colo-apps.les-crevettes-cevenoles.fr

| Élément | Valeur |
|---|---|
| **URL relais Colo Course** | `wss://colo-apps.les-crevettes-cevenoles.fr` |
| **IPv4 publique** | `78.122.112.36` |
| **Backend** | Strfry Docker → `127.0.0.1:7777` |
| **TLS nginx** | SNI `:443` → `127.0.0.1:11443` |
| **Certificat** | Let's Encrypt DNS-01 |

## Relais Docker (déjà sur ce serveur)

```bash
# Copie opérationnelle (snap Docker) :
cd ~/colocourse-relay && docker compose up -d
```

Fichiers versionnés : `deploy/relay/docker-compose.yml`

## Checklist mise en ligne

Voir aussi `docs/RELAY.md` (guide générique).
