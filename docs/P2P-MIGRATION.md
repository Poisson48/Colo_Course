# Colo Course — Rapport : passage Nostr → P2P (découverte IP seule)

> Document de cadrage. **Pas d’implémentation ici** — référence pour reprendre le travail plus tard.
> Date de rédaction : 2026-09-12.
> Contexte code : sync actuelle = CRDT + chiffrement `listKey` + transport Nostr kind 4545 via `wss://colo-apps.les-crevettes-cevenoles.fr` (Strfry).

---

## 1. Objectif produit

Passer d’un **relais Nostr store-and-forward** à un **partage P2P direct** entre appareils.

Le « serveur » (même nom de domaine que le relais actuel, hébergé en local sur le PC colo-apps) ne doit **surtout pas** être un vrai serveur de données :

| Autorisé | Interdit |
|----------|----------|
| Donner / croiser des **adresses IP** (et ports) pour se trouver | Stocker des listes, deltas, snaps, photos |
| Présence éphémère en **mémoire** (TTL court) | Mailbox / file opaque / rétention de blobs |
| HTTPS announce + lookup | Relayer le contenu des listes (TURN « données ») |

En une phrase : **rendez-vous d’IP uniquement, zéro stockage métier.**

---

## 2. Décisions déjà tranchées

1. **Pas de mailbox serveur** — rien n’est persisté côté colo-apps concernant le contenu des listes.
2. **Même domaine** que le relais actuel (`colo-apps.les-crevettes-cevenoles.fr`) pour le service de découverte (sous-chemin ou location nginx à définir à l’implémentation).
3. **Conservé** (couche stable) :
   - CRDT + SQLite local
   - Clé de liste + URI QR `colocourse://join/…`
   - Payloads chiffrés (`delta` / `snap` / `img`, XChaCha20-Poly1305)
   - Outbox **locale** (modifs en attente de livraison P2P)
4. **Retiré à terme** :
   - Strfry / événements Nostr kind 4545
   - Signature secp partagée « pour le relais »
   - Reseeding d’images motivé par la rétention relais
   - Notion « online = WebSocket relais up »

---

## 3. Conséquence produit majeure (à assumer)

Sans stockage serveur, la sync n’existe **que s’il existe une fenêtre où** :

1. les deux (ou N) appareils ont l’app **capable de réseau** en même temps, **et**
2. un **chemin P2P** s’établit (LAN ou punch réussi).

Si A ajoute du lait et B a le téléphone éteint / hors réseau joignable → **ça n’arrive pas** tant qu’une fenêtre commune + chemin réseau n’apparaissent pas. L’outbox locale garde les envois côté A.

C’est un changement fort par rapport à Nostr (où le relais gardait l’événement pour B plus tard).

---

## 4. Architecture cible

```
┌─────────┐     HTTPS announce/lookup (éphémère)      ┌──────────────────┐
│  App A  │ ─────────────────────────────────────────▶│ colo-apps        │
│         │ ◀─────────────────────────────────────────│ peer directory   │
└────┬────┘     IP:port des pairs (TTL, RAM only)     │ (pas de blobs)   │
     │                                                 └──────────────────┘
     │              P2P direct (payloads chiffrés)
     └──────────────────────────────▶ App B
```

### 4.1 Service de découverte (esquisse)

- `POST /peers/announce` — deviceId, preuves liées au `channelTag` (sans envoyer `listKey`), IP/port LAN et/ou WAN, proto, TTL.
- `GET /peers/lookup?…` — liste des pairs récents pour ce canal.
- Stockage : **RAM uniquement**, expiration automatique (ex. 30–120 s), heartbeat.
- Auth : preuve de connaissance dérivée de la liste (ex. HMAC/`channelTag`), rate-limit anti-spam.

### 4.2 Transport données

- Session directe entre apps (protocole exact à choisir à l’implémentation : TCP/WS custom, QUIC, etc.).
- Même clair → chiffrement `listKey` qu’aujourd’hui ; seule l’enveloppe Nostr disparaît.
- Ack de livraison → sortie d’outbox (politique multi-pairs à trancher, voir §7).

### 4.3 Couche code actuelle à découpler

Fichiers centraux aujourd’hui :

- `src/app/syncengine.*` — orchestre encrypt / publish / merge / outbox
- `src/net/relaypool.*`, `relayclient.*`, `nostr.*` — transport Nostr
- `src/net/crypto.*` — `listKey`, `channelTag`, chiffrement (à garder) ; dérivation Nostr secp (à retirer plus tard)
- `src/store/database.*` — outbox = JSON d’événements Nostr signés (à rendre agnostique)
- Deploy : `deploy/relay/` (Strfry), nginx colo-apps, ntfy

Ordre d’implémentation recommandé plus tard :

1. Abstraction `SyncTransport` + adaptateur Nostr (comportement inchangé).
2. Service découverte + client announce/lookup.
3. Transport P2P (LAN d’abord).
4. Hybride éventuel, puis coupure Strfry.

---

## 5. NAT, hole punching, téléphone

### 5.1 App ouverte en même temps ?

**Nécessaire, pas suffisant.**

Il faut aussi un chemin réseau :

| Situation | Probabilité P2P |
|-----------|-----------------|
| Même Wi‑Fi (coloc) | **Élevée** (souvent IP LAN, sans punch) |
| Deux box domestiques « classiques » | Punch UDP **souvent** OK |
| Un ou deux côtés en **4G/5G (CGNAT)** | **Souvent échec** |
| App éteinte / process tué | Impossible (pas d’annonce, pas d’écoute) |

### 5.2 « Si on punch beaucoup, ça passe ? »

**Non** pour le CGNAT.

- Retries courts aident le **timing** et certains NAT à cône.
- Spammer le punch **ne crée pas** une IP publique joignable derrière CGNAT opérateur.
- C’est une limite de **topologie**, pas de nombre d’essais.

### 5.3 TURN / relais de trafic

Un serveur TURN ferait passer la 4G en relayant les **données** → contredit l’objectif « serveur = IP only ». **Hors scope** tant que cette contrainte tient.

### 5.4 Lien avec `docs/PLAN.md`

Le plan historique avait **écarté** le WebRTC P2P pour : appareils rarement co-online + CGNAT. Ce rapport **réouvre** le P2P en acceptant explicitement ces limites et en réduisant le serveur à la découverte.

---

## 6. ntfy (wake)

Aujourd’hui : ntfy sur le même domaine réveille Android (« sync ») ; ça ne transporte **pas** les données.

Avec P2P pur :

- **Utile** : prévenir B que A a des trucs / est annonçable → B ouvre l’app → fenêtre commune.
- **Pas magique** : si le chemin réseau ne passe pas (4G), l’ouverture ne suffit pas.
- Décision finale (garder / retirer) : **encore ouverte** (voir §7).

---

## 7. Questions encore ouvertes (à trancher avant de coder)

1. **Hors LAN** : v1 = LAN + punch best-effort seulement (échec 4G accepté), ou effort WAN poussé dès le début ?
2. **ntfy** : on garde le wake, ou on simplifie encore (moins de serveur) ?
3. **N appareils** sur une liste : ack outbox = tous les pairs actuellement annoncés, ou un seul suffit ?
4. **Plateformes MVP** : Android seul, ou Android + desktop Linux ?
5. **Migration** : période hybride Nostr + P2P, ou coupure dès que le LAN marche en coloc ?
6. **Sous-chemin découverte** exact sous colo-apps (`/peers`, autre) et format des messages wire P2P.

---

## 8. Risques (résumé)

| Risque | Impact |
|--------|--------|
| Pas de co-présence | Sync retardée ou bloquée jusqu’à fenêtre commune |
| CGNAT mobile | P2P WAN souvent impossible sans TURN |
| Bootstrap nouvel adhérent | Plus d’historique relais : il faut un pair en ligne qui envoie un snap |
| Outbox / ack multi-pairs | Sémantique plus dure qu’un ack relais unique |
| Android Doze | « App lancée » en arrière-plan ≠ socket fiable |
| Double stack hybride | Deux notions d’« online » si Nostr + P2P coexistent |

---

## 9. Critères de succès (brouillon)

- Deux téléphones **même Wi‑Fi**, apps ouvertes : ajout / coche / photo visibles sans passer par Strfry.
- Serveur découverte : redémarrage = perte des annonces seulement (aucune donnée liste sur disque).
- Contenu des listes jamais visible en clair ni en blob stocké sur colo-apps.
- Outbox se vide après ack P2P ; bandeau pending cohérent.
- (Optionnel) Join d’un 3ᵉ appareil : rattrapage via snap depuis un pair en ligne.

---

## 10. Prochaines étapes (quand on reprend)

1. Trancher §7.
2. Rédiger `docs/SPEC-P2P.md` (messages découverte + session P2P + sémantique outbox).
3. PR1 : abstraction transport (Nostr derrière interface).
4. PR2 : service découverte RAM + nginx.
5. PR3 : P2P LAN.
6. Mesure en conditions réelles coloc, puis décision coupure Strfry.

---

## 11. Références code / docs existants

- `docs/SPEC.md` — protocole actuel (transport §3 = Nostr)
- `docs/PLAN.md` — décision historique anti-WebRTC ; tâche mDNS reportée
- `docs/RELAY.md`, `deploy/relay/` — Strfry + nginx colo-apps
- `src/app/syncengine.*`, `src/net/relay*` — pile à remplacer derrière une interface
- Exploration architecture (2026-09-12) : confirmation absence totale de code P2P/mDNS/WebRTC actuel ; outbox = JSON Nostr ; online = WSS relais
