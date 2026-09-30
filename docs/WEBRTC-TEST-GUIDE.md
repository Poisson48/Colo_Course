# Test WebRTC Colo_Course — Signaling End-to-End

Ce script teste la connectivité WebSocket au signaling server et simule deux pairs.

## Prérequis

```bash
npm install ws
```

## Lancer le signaling server en local

```bash
cd deploy/signaling
node server.js
```

Le serveur écoute sur le port **7778**.

## Lancer le test

```bash
node scripts/test-webrtc-signaling.js
```

## Ce qui est testé

1. **Connexion WebSocket** au signaling server
2. **Join room** — deux clients rejoignent la même room
3. **Exchange SDP** — offre/réponse circulent entre les pairs
4. **ICE candidates** — les candidats sont relayés
5. **Peer connecté** — les deux pairs se voient mutuellement

## Structure du test

```
[Peer A] ←→ [Signaling Server] ←→ [Peer B]
   │              :7778                 │
   └──────── SDP/ICE exchange ─────────┘
```

## Codes de message (implémentés dans server.js)

| Type | Direction | Payload |
|------|-----------|---------|
| `join` | Client → Server | `{room, peerId}` |
| `offer` | Server → Room | `{from, sdp}` |
| `answer` | Server → Room | `{from, sdp}` |
| `ice` | Server → Room | `{from, candidate}` |
| `leave` | Client → Server | `{room, peerId}` |

## Dépannage

- **"Connection refused"** → le signaling server n'est pas lancé sur :7778
- **"Timeout waiting for peer"** → vérifier que les deux clients utilisent le même `roomId`
- **"SDP exchange failed"** → vérifier les logs du serveur : `tail -f signaling.log`

## Tester avec l'app

1. Lancer le signaling server en local
2. Ouvrir deux instances de l'app (desktop)
3. Paramètres → activer « P2P direct (WebRTC) »
4. Mettre `ws://localhost:7778` comme signaling URL
5. Créer une liste sur A, rejoindre avec B
6. Ajouter un article sur A → doit apparaître sur B
