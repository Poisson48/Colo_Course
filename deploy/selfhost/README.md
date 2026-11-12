# Auto-hébergement

Dernière mise à jour : **2026-11-12**. Le P2P, le TURN et le signaling ne font
plus partie du produit. Le kit self-host se limite à :

1. **relais Nostr** Strfry (`wss://…`) — stockage et fan-out des blobs chiffrés ;
2. **broker de notifications** `colopush` (`/push`) — FCM + UnifiedPush ;
3. **ntfy** (`/ntfy/`) — notifications différées et distributeur UnifiedPush ;
4. éventuellement **releases** (`/releases/`) pour les mises à jour.

Aucune donnée de liste n'est lisible par ces services.

## Relais

```bash
cd deploy/relay
# voir nginx-colo-apps.conf et le README de deploy/relay
```

Le relais doit accepter le kind `4545`, avec `maxEventSize = 256 Ko`.

## Notifications

- FCM : fournir un compte de service Firebase et le placer dans
  `/data/leo/colo-apps/secrets/fcm-service-account.json`.
- UnifiedPush : les clients enregistrent leur endpoint dans
  `/data/leo/colo-apps/push/endpoints.json` via `POST /push`.
- ntfy : nécessaire pour le mode différé et pour un distributeur UnifiedPush
  auto-hébergé.

## Checklist

```bash
systemctl is-active nginx colopush
curl -fsS https://VOTRE-DOMAINE/push
curl -fsS https://VOTRE-DOMAINE/config
docker compose -f ~/colocourse-relay/docker-compose.yml ps
```

## Ce qui n'est plus nécessaire

- STUN/TURN, coturn, ports `3478`/`5349` ;
- serveur de signaling WebRTC ;
- photos HD synchronisées.

Voir [docs/SYNC-SERVER.md](../../docs/SYNC-SERVER.md) et
[PROTOCOL.md](../../PROTOCOL.md).
