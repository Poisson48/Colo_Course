# Colo Push — broker de notifications

Dernière mise à jour : **2026-11-12**. Node.js sans dépendance, port
`127.0.0.1:7780`, exposé par nginx sur `/push`.

## Contrat

```http
POST /push
{ "app": "colocourse|colotaches", "topic": "colo-<32 hex>" }
→ 202 | 400 | 429 | 502
```

- FCM : data-only `{app, kind:"sync"}` ; aucun titre, aucun contenu de liste.
- UnifiedPush : envoi à chaque endpoint enregistré pour l'app.
- Rate-limit : 1 envoi / 5 s / topic.

### Enregistrement UnifiedPush

```http
POST /push
{ "action":"register", "app":"colocourse", "endpoint":"https://ntfy.sh/up…" }
```

Suppression :

```http
POST /push
{ "action":"unregister", "app":"colocourse", "endpoint":"https://ntfy.sh/up…" }
```

Les endpoints sont stockés dans `/data/leo/colo-apps/push/endpoints.json`
(root:colosync, 660). `GET /config` renvoie :

```json
{"notifications":{"fcm":true,"unifiedpush":true}}
```

## Secrets

- `/data/leo/colo-apps/secrets/fcm-service-account.json` (root:colosync, 640) ;
- clés VAPID : `/data/leo/colo-apps/secrets/vapid-*.txt` (non utilisées par
  UnifiedPush via ntfy, conservées pour un futur distributeur).

## Déploiement

```bash
install -d -m 750 -o root -g colosync /data/leo/colo-apps/push
install -m 750 -o root -g colosync deploy/push/server.mjs \
  /data/leo/colo-apps/push/server.mjs
install -m 644 deploy/push/colopush.service /etc/systemd/system/
systemctl daemon-reload && systemctl restart colopush

install -o root -g colosync -m 660 /dev/null \
  /data/leo/colo-apps/push/endpoints.json
```

nginx : `location = /push { proxy_pass http://127.0.0.1:7780; }`.

## Test

```bash
curl -fsS https://colo-apps.les-crevettes-cevenoles.fr/push
curl -fsS https://colo-apps.les-crevettes-cevenoles.fr/config

curl -fsS -X POST https://colo-apps.les-crevettes-cevenoles.fr/push \
  -H 'Content-Type: application/json' \
  -d '{"app":"colocourse","topic":"colo-0123456789abcdef0123456789abcdef"}'
```

Réponse attendue : `{"ok":true,"fcm":true,"unifiedpush":0}` ou `1`.
