# Proposition : serveur de synchronisation chiffré (hub)

> Cible : 1 000 utilisateurs simultanés, groupes de 15, 20 listes par personne,
> propagation < 10 s, sans perte. Le P2P n’est plus le chemin principal.

## 1. Principe

Un serveur de synchronisation reçoit des **événements chiffrés** (deltas, snaps,
vignettes), les pousse aux membres de la liste, et suit les accusés de réception
par appareil. Il ne peut pas déchiffrer le contenu.

```
User1 ──(event chiffré + seq)──► Colo Sync Server ──► User2…User15
                                      │
                                      ├─ roster de la liste (device IDs)
                                      ├─ watermarks d’ack par device
                                      └─ snapshots chiffrés
```

Le serveur ne stocke **aucune clé** et ne lit aucun payload.

## 2. Ce que le serveur doit connaître

Pour savoir « qui a reçu quoi », il faut un **registre par liste** :

- `listId` opaque (dérivé de la clé, comme aujourd’hui) ;
- appareils membres (`deviceId`) et clé publique de signature ;
- preuve d’appartenance à l’ajout (HMAC/token dérivé de la clé de liste) ;
- dernier `seq` acquitté par appareil ;
- date de dernière activité.

Le serveur voit donc **des métadonnées** : liste, appareils, tailles, horodatages.
Il ne voit ni les articles, ni les quantités, ni les photos.

## 3. Protocole

### Publication

```json
{
  "type": "publish",
  "list_id": "…",
  "event_id": "…",
  "kind": "delta|snap|thumb",
  "ciphertext": "…",
  "sender": "device-…",
  "sig": "…"
}
```

Le serveur :

1. vérifie la signature et l’appartenance ;
2. attribue un `seq` monotone par liste ;
3. stocke l’événement ;
4. pousse `event` aux appareils membres connectés ;
5. attend les `ack`.

### Ack

```json
{ "type": "ack", "list_id": "…", "seq": 42 }
```

Le client ack **après merge SQLite réussi**, pas à la réception réseau. Un ack
perdu est rejoué ; il est idempotent.

### Rattrapage

```json
{ "type": "subscribe", "list_id": "…", "after_seq": 41 }
```

Le serveur envoie les événements `seq > 41` encore disponibles. S’ils ont été
supprimés, il envoie `gap { from_seq, to_seq }` et le dernier snapshot connu.

## 4. Suppression et rétention

Le serveur ne supprime pas « quand tout le monde a reçu » au sens vague. Il
utilise un **watermark** :

```
minAck = min(last_ack[device]) pour les appareils actifs
delete events where seq <= minAck - grace
```

- `active` = vu dans les **30 derniers jours** ;
- `grace` = quelques jours pour les retards/retries ;
- un appareil inactif depuis 30 jours est marqué `gap` et **ne bloque plus** la
  suppression ;
- un appareil qui revient reçoit `gap` + snapshot.

### Pourquoi garder un snapshot

Supprimer tous les deltas dès que les 15 membres actuels ont ack serait correct
pour eux, mais :

- un **nouvel appareil** n’aurait aucun historique ;
- un appareil resté 31 jours hors ligne n’aurait plus rien ;
- si personne n’est en ligne, le serveur ne peut pas « demander à un user de
  renvoyer ».

**Règle recommandée** : supprimer les deltas acquittés, mais conserver **le
dernier snapshot chiffré par liste** (et les vignettes référencées) tant que la
liste existe. C’est petit, borné, et cela permet le bootstrap sans dépendre d’un
pair.

Le « flag non reçu » devient un `gap` par appareil : à la reconnexion, le client
sait qu’il lui manque `[from_seq, to_seq]` et demande le snapshot.

## 5. Le « serveur demande à un user de renvoyer »

À utiliser seulement si aucun snapshot n’est disponible :

1. le serveur choisit un appareil actif avec `last_ack >= gap.to_seq` ;
2. il lui envoie `snapshot_request { list_id, to_seq }` ;
3. l’appareil chiffre son état courant et le publie comme `snap` ;
4. le serveur le stocke et le pousse au demandeur.

Il faut un timeout, plusieurs candidats, et un fallback « réessayer plus tard ».
Un snapshot serveur est plus simple et plus fiable ; garder cette procédure en
secours.

## 6. Dimensionnement 1 000 utilisateurs

- 1 000 WebSockets, 67 groupes, 15 membres par groupe.
- 20 000 abonnements (1 000 × 20 listes) : le serveur peut les indexer en
  mémoire ; la base stocke les watermarks (`device × list`), pas une matrice
  événement × appareil.
- Un événement par modification ; fan-out à 15 membres ; < 1 s sur le serveur.
- Le serveur n’a plus besoin de TURN ni de maillage WebRTC pour ce chemin.
- Postgres (ou SQLite pour un petit déploiement) + Redis/pub-sub pour plusieurs
  instances ; WebSocket derrière un load balancer avec sticky sessions.
- Objets : snapshots/vignettes dans un stockage objet chiffré ou sur disque ;
  quotas par liste.

## 7. Sécurité

- Chiffrement E2E existant (XChaCha20-Poly1305) ; le serveur ne voit que des
  blobs.
- Signature d’appareil (Ed25519) sur chaque événement.
- Preuve d’appartenance dérivée de la clé de liste ; la clé n’est jamais envoyée.
- Rate-limit, taille max, quota par liste, anti-replay par `event_id`.
- Le serveur voit les métadonnées (membres, horaires, tailles) : à documenter.
- Suppression logique ≠ effacement physique (SSD) : le dire clairement.

## 8. Migration depuis le système actuel

1. Garder le relais Nostr comme fallback pendant la transition.
2. Ajouter le serveur hub en **dual-write** : les clients publient au hub et au
   relais.
3. Les clients lisent le hub en priorité ; le relais sert de secours.
4. Une fois 1 000 utilisateurs validés, le hub devient le chemin principal ;
   le P2P/TURN reste optionnel pour les petits groupes.
5. Les anciens clients continuent via le relais.

## 9. Ce que cela change pour la v0.32.0

La v0.32.0 famille (P2P hybride, 2–8 appareils/liste) reste une base utile.
Le hub chiffré est une **v0.33** : il remplace le maillage pour les grands
groupes, sans TURN cluster et sans 7 000 ports.

## 10. Décisions à prendre

- Faut-il des comptes utilisateurs, ou seulement des appareils + preuve de liste ?
- Le snapshot serveur est-il conservé jusqu’à suppression de la liste, ou 30/90 j ?
- Que fait-on si un appareil reste inactif 30 j et qu’aucun snapshot n’existe ?
- Les vignettes sont-elles stockées sur le serveur (oui recommandé) et les HD
  restent-elles P2P (oui) ?
- Quotas : taille par liste, nombre d’appareils, débit.
