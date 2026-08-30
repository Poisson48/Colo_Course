# Relais Nostr — colo-apps.les-crevettes-cevenoles.fr

| Élément | Valeur |
|---|---|
| **URL relais Colo Course** | `wss://colo-apps.les-crevettes-cevenoles.fr` |
| **IPv4 publique** | `78.122.112.36` |
| **Backend** | Strfry Docker → `127.0.0.1:7777` |
| **TLS nginx** | SNI `:443` → `127.0.0.1:11443` |
| **Certificat** | Let's Encrypt DNS-01 |

## Relais Docker

```bash
cd ~/colocourse-relay && docker compose up -d
```

Fichiers : `strfry.conf`, `write-policy.py`, `docker-compose.yml`

## Politique d'écriture

L'image `dockurr/strfry` refuse par défaut tout sauf une whitelist de pubkeys.
Colo Course publie le **kind 4545** avec une clé Nostr **dérivée par liste** :
une whitelist ne peut pas fonctionner.

`write-policy.py` accepte uniquement le kind 4545.

Symptôme si mal configuré : modifications locales OK, jamais reçues ailleurs,
message relais `blocked: pubkey … not in whitelist`.

## Checklist mise en ligne

Voir aussi `docs/RELAY.md`.
