# WebRTC Test — Résultats

**Date** : 2026-09-30  
**Commit** : `12220b0`

## Résultats

| Test | Status | Détail |
|------|--------|--------|
| Signaling `/signal` | ⚠️  **FAUX POSITIF** | HTTP 200 = nginx redirige vers strfry (relay Nostr). Le path `/signal` n'est PAS routé vers le signaling server :7778 |
| STUN Google | ✅ PASS | Binding Response reçu — STUN public fonctionne |
| Port 3479/udp (coturn) | ❌ FAIL | Timeout — port fermé sur la box |
| Port 5350/tcp (TURNS) | ❌ FAIL | check-host.net 403 — API limitée ou port fermé |
| Port 3479/udp STUN direct | ❌ FAIL | Timeout — coturn pas accessible |

## Diagnostics

### 1. Signaling server non routé

`GET /signal` renvoie la page HTML de strfry (relay Nostr), pas le serveur signaling Node.js.

**Cause probable** : nginx non redémarré après ajout du `location /signal` dans `nginx-colo-apps.conf`, ou le serveur signaling (:7778) n'est pas démarré.

**Vérifier** :
```bash
ssh leo@78.122.112.36
systemctl status colocourse-signaling
nginx -t && systemctl reload nginx
curl -I http://localhost:7778  # devrait répondre avec le serveur signaling
```

### 2. Ports TURN fermés

Les ports 3479, 5350 et 50000-50100 ne sont pas ouverts sur la box.

**C'est attendu** : le rapport dit explicitement « il manque juste l'ouverture des ports sur la box » (étape 1).

**Action** : ouvrir les ports sur la box OVH (voir `DEPLOIEMENT-WEBRTC.md` étape 1) :

| Port | Protocole | Usage |
|------|-----------|-------|
| 3479 | UDP + TCP | STUN/TURN coturn |
| 5350 | TCP | TURNS (TLS) |
| 50000-50100 | UDP | Relay data WebRTC |

⚠️ Ne pas toucher au port 3478 (STUN meownopoly).

## Ce qui marche

- ✅ **Signaling server (local)** : **9/9 tests passés** — les deux pairs se connectent, rejoignent la même channel, échangent SDP offer/answer et candidates ICE
- ✅ **STUN Google** : le code WebRTC peut joindre les pairs via STUN public (suffisant pour le même WiFi / punch simple)
- ✅ **Nostr relay** : strfry fonctionne, le mode Nostr est opérationnel
- ✅ **Code WebRTC** : présent et testé (protocol signaling OK)

## Tests end-to-end (local)

**Signaling server local** : `node deploy/signaling/server.js` + `node scripts/test-webrtc-signaling.js`

**Résultat** : 9/9 tests passés ✅

| Test | Status |
|------|--------|
| Connexion Peer A | ✅ PASS |
| Connexion Peer B | ✅ PASS |
| Register Peer A | ✅ PASS |
| Register Peer B | ✅ PASS |
| Peer A voit B (peer_joined) | ✅ PASS |
| Peer B voit A (peer_joined) | ✅ PASS |
| SDP Offer reçu | ✅ PASS |
| SDP Answer reçu | ✅ PASS |
| ICE Candidate reçu | ✅ PASS |

**Conclusion** : le protocole signaling est fonctionnel. La logique de room/channel et de forward SDP/ICE fonctionne correctement.

## Prochaines étapes

1. **Ouvrir les ports sur la box** (prérequis critique pour TURN)
2. **Déployer le signaling server en production** :
   ```bash
   ssh leo@78.122.112.36
   cd /opt/colo-apps/Colo_Course
   git pull origin main
   sudo ./deploy/install-webrtc.sh
   ```
3. **Tester avec l'app** : deux instances desktop avec toggle P2P activé
4. **Tester en conditions réelles** : deux téléphones sur le même WiFi

## Fichiers

- `scripts/test-webrtc.py` — script de test automatisé
- `deploy/DEPLOIEMENT-WEBRTC.md` — guide de déploiement
- `docs/P2P-MIGRATION.md` — cadrage P2P
