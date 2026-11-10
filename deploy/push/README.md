# Colo Push — broker FCM

Service Node minimal sur `127.0.0.1:7780`, exposé par nginx sur `/push`.

## Contrat

```http
POST /push
{ "app": "colocourse|colotaches", "topic": "colo-<32 hex>" }
→ 202 | 400 | 429 | 502
```

- Message **data-only** : `{ app, kind: "sync" }` ; jamais de titre ni de contenu.
- Rate-limit : 1 envoi / 5 s / topic.
- `/healthz` : `{ ok, fcm, project }`.
- `/config` : capacités FCM/UnifiedPush.

## Secrets

- `/data/leo/colo-apps/secrets/fcm-service-account.json` (root:colosync, 640)
- `/data/leo/colo-apps/secrets/vapid-private.txt` (root:root, 600)
- `/data/leo/colo-apps/secrets/vapid-public.txt` (public)

## Déploiement

```bash
install -d -m 750 -o root -g colosync /data/leo/colo-apps/push
install -m 750 -o root -g colosync deploy/push/server.mjs /data/leo/colo-apps/push/server.mjs
install -m 644 deploy/push/colopush.service /etc/systemd/system/colopush.service
systemctl daemon-reload && systemctl enable --now colopush.service
# nginx : proxy /push vers 127.0.0.1:7780
```

## Test

```bash
curl -fsS https://colo-apps.les-crevettes-cevenoles.fr/push \
  -H 'Content-Type: application/json' \
  -d '{"app":"colocourse","topic":"colo-0123456789abcdef0123456789abcdef"}'
```
