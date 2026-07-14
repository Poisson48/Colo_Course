# Colo_Course — Spécification technique (v1)

> Référence d'implémentation. Les sessions codeur suivent ce document à la lettre.
> Toute modification de cette spec est un commit `docs:` séparé, jamais mélangé à du code.

## 1. Identifiants et types de base

- **deviceId** : UUID v4 généré au premier lancement, stocké en local, jamais changé.
  Identifie une installation (pas une personne).
- **listId** : 16 octets aléatoires, encodés base64url (22 caractères). Généré à la création
  de la liste.
- **itemId** : UUID v4, généré à la création de l'item.
- **listKey** : clé symétrique 32 octets, générée à la création de la liste
  (`crypto_aead_xchacha20poly1305_ietf_keygen` de libsodium).
- **Horodatage logique (`ver`)** : paire `[lamport, deviceId]`. Comparaison :
  `a > b` si `a.lamport > b.lamport`, ou égalité de lamport et `a.deviceId > b.deviceId`
  (comparaison lexicographique des chaînes UUID). Total order garanti.
- **Horloge de Lamport** : un compteur entier **par liste**, persisté en SQLite.
  - Modification locale : `clock = clock + 1`, la nouvelle valeur tamponne le champ modifié.
  - Réception distante : `clock = max(clock, max des lamport reçus)`.

## 2. Modèle de données CRDT

CRDT **à état (delta-state), LWW par champ**. Le merge est idempotent, commutatif,
associatif : l'ordre d'arrivée et les doublons sont sans effet. C'est la propriété
fondamentale — la couche réseau n'a AUCUNE garantie d'ordre ni d'unicité à fournir.

### 2.1 Item

Chaque champ porte sa propre version :

| Champ | Type | Sémantique |
|---|---|---|
| `name` | string (≤ 200 chars) | libellé de l'article |
| `qty` | string (≤ 50 chars, libre : "2", "1 kg"…) | quantité |
| `done` | bool | coché = acheté |
| `del` | bool | tombstone (supprimé) |

Plus deux métadonnées non versionnées, fixées à la création : `created` (ms epoch, pour le
tri d'affichage) et `by` (deviceId créateur).

### 2.2 Liste

- Champ versionné : `title` (string ≤ 100).
- Registre `members` : map `deviceId → displayName` versionné par entrée (LWW). Chaque
  appareil n'écrit que sa propre entrée. Purement informatif (affichage "ajouté par Léo") —
  l'appartenance réelle = possession de la clé.

### 2.3 Règle de merge (unique et universelle)

Pour chaque champ d'un enregistrement reçu : si `ver_reçu > ver_local`, adopter valeur et
version reçues ; sinon ignorer. Item inconnu localement → l'insérer tel quel. C'est tout.

### 2.4 Suppression et GC des tombstones

- Supprimer = poser `del=true` (LWW). Un `del=false` postérieur ressuscite l'item (undo).
- GC local : purger de SQLite les items `del=true` dont le lamport de `del` est plus vieux
  que le lamport courant local d'au moins `GC_AGE = 10000` ET dont la dernière écriture
  locale (`touched`, voir §6) date de plus de 30 jours d'horloge murale. `touched` est une
  métadonnée purement locale (ms epoch, mise à jour à chaque upsert de l'item) : elle ne
  circule JAMAIS dans les payloads. Un pair très en retard peut théoriquement ressusciter
  un item purgé — accepté en v1 (impact : un article réapparaît).

## 3. Transport : relais Nostr

### 3.1 Principe

Store-and-forward via relais Nostr publics (NIP-01, WebSocket + JSON). Les relais ne voient
que des blobs chiffrés. Ensemble par défaut (modifiable dans les réglages) :
`wss://relay.damus.io`, `wss://nos.lol`, `wss://relay.nostr.band`, `wss://offchain.pub`.
Publication sur TOUS les relais joignables ; réception dédupliquée par id d'événement.

### 3.2 Canal par liste

- `channelTag = hex(SHA256("colo-course/v1/channel" || listKey))[0..31]` (32 hex chars).
- Clé Nostr de publication : keypair secp256k1 **dérivé de la liste** :
  `nostrSeed = SHA256("colo-course/v1/nostrkey" || listKey)` → clé privée. Tous les membres
  signent avec la même clé : anonymat des membres entre eux vis-à-vis des relais, et
  filtrage possible par auteur.
- Événement Nostr : `kind = 4545` (kind régulier, stocké par les relais), tags
  `[["t", channelTag]]`, `content` = base64(nonce ‖ ciphertext).
- Souscription : `{"kinds":[4545], "#t":[channelTag], "since": <lastSync - 3600>}`
  (chevauchement d'1 h ; la dédup rend le rejeu inoffensif).

### 3.3 Chiffrement du contenu

- XChaCha20-Poly1305 (libsodium), clé = listKey, nonce aléatoire 24 octets préfixé au
  ciphertext. AD (additional data) = channelTag.
- Payload clair = JSON UTF-8 compact (§4). Taille max visée 32 Ko ; si un snapshot dépasse,
  le scinder en plusieurs événements `snap` partiels (chacun auto-suffisant : le merge
  s'en moque).

### 3.4 Deltas et snapshots

- **`delta`** : publié à CHAQUE modification locale (débounce 300 ms pour grouper une
  rafale). Contient les enregistrements complets des items modifiés (état, pas opération).
- **`snap`** : état complet de la liste (items non purgés + title + members). Publié si
  ≥ 100 deltas émis par cet appareil depuis son dernier snap, ou si son dernier snap date
  de > 7 jours. Protège contre la rétention limitée des relais.
- **Adhésion (nouveau membre)** : souscrire sans `since`, `limit: 500` ; merger tout ce qui
  arrive (snaps + deltas). Aucune logique spéciale : le merge fait foi.

### 3.5 File hors-ligne et notifications

- Modification locale sans réseau → l'événement chiffré est rangé dans la table `outbox`,
  bandeau "hors ligne — N modifs en attente" dans l'UI. Au retour du réseau : flush outbox
  puis rattrapage de la souscription.
- Après un merge distant qui change quelque chose (app ouverte ou au lancement) :
  notification locale système, ex. "Courses maison : 3 articles ajoutés par Marie".

## 4. Format des messages (payload clair)

```json
{"v":1,"t":"delta","list":"<listId>",
 "items":[
   {"id":"<uuid>","created":1721000000000,"by":"<deviceId>",
    "f":{"name":["Lait",[12,"<devA>"]],
         "qty":["1L",[12,"<devA>"]],
         "done":[true,[15,"<devB>"]],
         "del":[false,[12,"<devA>"]]}}]}
```

`snap` : même schéma avec `"t":"snap"` plus `"title":["Courses",[3,"<devA>"]]` et
`"members":{"<deviceId>":["Léo",[1,"<devA>"]]}`. Un `delta` peut aussi porter `title` ou
`members` s'ils viennent de changer. Champs inconnus : ignorés (forward-compat). `"v"` ≠ 1 :
événement ignoré.

## 5. Appairage (QR / clé)

- URI : `colocourse://join/1/<listId>/<base64url(listKey)>/<urlencode(titre)>`
  affichée en QR code (et copiable en texte pour partage hors QR).
- Le récepteur scanne (ou colle), crée la liste localement, dérive canal + clé Nostr,
  souscrit sans `since` (§3.4), publie son entrée `members` et un delta vide de présence.
- Révocation non supportée en v1 : quiconque a la clé est membre à vie de cette liste.
  Documenter : "pour exclure quelqu'un, recréer une liste".

## 6. Persistance SQLite

Base unique `colocourse.db`, une seule connexion, WAL activé.

```sql
lists(list_id TEXT PK, key BLOB, title TEXT, title_ver_l INT, title_ver_d TEXT,
      lamport INT, last_sync INT, created INT);
items(list_id TEXT, item_id TEXT, created INT, by TEXT,
      name TEXT,  name_l INT,  name_d TEXT,
      qty  TEXT,  qty_l  INT,  qty_d  TEXT,
      done INT,   done_l INT,  done_d TEXT,
      del  INT,   del_l  INT,  del_d  TEXT,
      touched INT,   -- ms epoch de la dernière écriture locale ; jamais synchronisé (§2.4)
      PRIMARY KEY(list_id, item_id));
members(list_id TEXT, device_id TEXT, name TEXT, ver_l INT, ver_d TEXT,
        PRIMARY KEY(list_id, device_id));
outbox(rowid, list_id TEXT, event_json TEXT, created INT);
seen_events(event_id TEXT PK, seen INT);   -- purge des entrées > 30 jours
settings(key TEXT PK, value TEXT);         -- deviceId, displayName, relays…
```

Toute application d'un merge + mise à jour de `lamport`/`last_sync` est transactionnelle.

## 7. Architecture du code

```
src/
  core/      # PUR C++ (aucun include Qt GUI, testable) : Crdt, types, merge, GC
  store/     # persistance SQLite (QSqlDatabase)
  net/       # RelayClient (QWebSocket), NostrEvent, crypto (libsodium), outbox
  app/       # QML engine, QAbstractListModel, contrôleurs, notifications
  qml/       # vues
tests/       # Qt Test : unités core/, intégration store+core, net simulé
```

Dépendances : Qt 6 (Core, Quick, Sql, WebSockets), libsodium, un encodeur/décodeur QR
(qzxing ou équivalent — le codeur de la tâche choisit et le note ici).

- `core/` ne dépend que de la STL : les tests CRDT tournent sans display.
- UI : tri des items = non cochés d'abord, puis `created` croissant. Cocher = tap ;
  supprimer = swipe ; champ d'ajout rapide en bas.

## 8. Notifications & cycle de vie Android (décision v1)

Pas de FCM/Firebase (contrainte : rien de propriétaire ni de compte tiers).

- **App au premier plan** : souscription WebSocket vivante, merge en direct.
- **Lancement / retour au premier plan** : rattrapage immédiat + notification locale si
  changements (§3.5).
- **App fermée** : PAS de réception. Limitation assumée et documentée dans le README.
  (Piste v2, hors scope : foreground service optionnel "garder la synchro active".)
- Desktop Linux : QSystemTrayIcon + notification, connexion permanente.

## 9. Invariants à tester (base des tests de la tâche 2.1)

1. `merge(a, merge(b, c)) == merge(merge(a, b), c)` ; `merge(a, b) == merge(b, a)` ;
   `merge(a, a) == a` — vérifiés par fuzzing de séquences d'ops aléatoires sur 3+ répliques
   avec livraison aléatoire/dupliquée, convergence exigée à l'identique.
2. Deux éditions concurrentes du même champ → gagne le `ver` le plus grand, sur toutes les
   répliques.
3. Édition concurrente de champs différents du même item → les deux survivent.
4. `del` concurrent avec `done` → les deux versions de champ s'appliquent indépendamment.
5. Un item créé hors-ligne sur A apparaît sur B après flush de l'outbox, exactement une fois.
6. Rejeu complet de l'historique (adhésion tardive) → état identique aux répliques anciennes.
