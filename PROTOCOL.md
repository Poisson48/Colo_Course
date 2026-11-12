# Protocole Colo

Référence normative du protocole utilisé par **Colo Course** et **Colo Tâches**.
Version du protocole : **1**. Dernière mise à jour : **2026-11-12**.

> Ce document remplace l'ancienne `docs/SPEC.md`. Il décrit uniquement le produit
> actuel : **synchronisation par relais chiffré, sans P2P, sans TURN, sans WebRTC**.
> Les anciens documents P2P/WebRTC ont été supprimés.

## 1. Principes

- Chaque appareil garde une **copie locale complète** (SQLite).
- Une liste = une **clé symétrique 32 octets** = un canal chiffré.
- Le serveur est un **relais Nostr** store-and-forward : il ne peut pas lire les
  listes, seulement stocker et rediffuser des événements chiffrés.
- Le merge est un **CRDT delta-state** : idempotent, commutatif, associatif.
- Aucun compte, aucun identifiant de personne : seule l'installation (`deviceId`)
  est identifiée.
- Les notifications sont un **réveil sans contenu** (`sync`), jamais le nom d'une
  liste ni un article.

## 2. Identifiants

| Nom | Taille / format | Rôle |
|---|---|---|
| `listId` | 16 octets, base64url (22 car.) | identifiant opaque de liste |
| `itemId` | UUID v4 | identifiant d'article |
| `deviceId` | UUID v4 | installation, généré une fois |
| `listKey` | 32 octets | clé symétrique de la liste |
| `channelTag` | 16 octets → 32 hex | tag public du canal Nostr |
| `ver` | `[lamport, deviceId]` | version LWW d'un champ |

### Dérivations

```text
channelTag = hex(SHA256("colo-course/v1/channel" || listKey))[0..31]
nostrSeed  = SHA256("colo-course/v1/nostrkey" || listKey)
```

La clé Nostr de publication est un keypair secp256k1 dérivé de `listKey`. Tous les
membres d'une liste partagent donc la même identité de publication : les relais ne
distinguent pas les membres.

## 3. Modèle CRDT

Chaque champ versionné porte sa propre version `[lamport, deviceId]`. Règle unique :

```text
si ver_recu > ver_local : adopter valeur + version
sinon : ignorer
```

### Article

Champs versionnés : `name`, `qty`, `note`, `aisle`, `order`, `done`, `del`.
Métadonnées non versionnées : `created`, `by`, `doneAt` (satellite de `done`).
`del=true` est un tombstone ; `del=false` postérieur ressuscite l'article.

### Liste

- `title` versionné ;
- `members` : `deviceId → displayName`, LWW par entrée, informatif ;
- `kind` : `""`/`"shopping"` ou `"recipe"`, immuable à la création ;
- préférences de tri **locales** stockées dans `settings`, jamais synchronisées.

### Lamport

Un compteur par liste, persisté. Toute modification locale fait `+1` ; à la
réception, `clock = max(clock, lamport reçus)`.

### Tombstones

Les articles `del=true` sont purgés localement après :

- lamport de `del` plus vieux d'au moins `GC_AGE = 10000` que le lamport courant ;
- et dernière écriture locale (`touched`) de plus de 30 jours.

Un appareil très en retard peut théoriquement ressusciter un article purgé. C'est
accepté pour des listes de courses/tâches.

## 4. Transport Nostr

- Relais par défaut : `wss://colo-apps.les-crevettes-cevenoles.fr`.
- Relais configurable dans Réglages, une URL `wss://…` par ligne.
- Plusieurs relais possibles : publication sur tous, réception dédupliquée par
  identifiant d'événement.
- Événement Nostr : `kind = 4545`, tag `["t", channelTag]`.
- Souscription :

```json
{"kinds":[4545],"#t":[channelTag],"since":<lastSync-3600>}
```

- Contenu : `base64(nonce || ciphertext)`.
- Chiffrement : **XChaCha20-Poly1305** (libsodium), nonce aléatoire 24 octets,
  AD = `channelTag`.
- Taille visée : 32 Ko par événement ; les snapshots trop gros sont scindés en
  plusieurs événements `snap` partiels.

## 5. Payloads

Le clair est un JSON UTF-8 compact :

```json
{
  "v": 1,
  "t": "delta",
  "list": "<listId>",
  "by": "<deviceId>",
  "title": ["Courses", [3, "<deviceId>"]],
  "items": [
    {
      "id": "<uuid>",
      "created": 1721000000000,
      "by": "<deviceId>",
      "doneAt": 0,
      "f": {
        "name": ["Lait", [12, "<deviceId>"]],
        "qty": ["1 L", [12, "<deviceId>"]],
        "done": [false, [12, "<deviceId>"]],
        "del": [false, [12, "<deviceId>"]]
      }
    }
  ]
}
```

- `"t"` : `"delta"` ou `"snap"`.
- `"snap"` ajoute `"members"` et l'état complet des articles non purgés.
- `"kind"` éventuel : `"recipe"` ; absent = liste de courses.
- Champs inconnus ignorés ; un champ connu absent vaut `[0, ""]` et ne peut donc
  pas effacer une valeur réelle.
- `"v" != 1` : événement ignoré.

### Publication

- **delta** : à chaque modification locale, avec débounce 300 ms.
- **snap** : après ≥ 100 deltas depuis le dernier snap, ou après 7 jours.
- **nouvel appareil** : souscription sans `since`, `limit: 500`, merge de tout.

### Hors-ligne

Tout événement non publié est conservé dans la table `outbox`. Au retour du réseau :
flush de l'outbox puis rattrapage de la souscription. Aucune modification n'est
supprimée sans accusé durable du relais.

## 6. Appairage

```text
colocourse://join/1/<listId>/<base64url(listKey)>/<urlencode(titre)>
```

- Génération QR : `qrcodegen` (Nayuki, MIT), rendu QML.
- Android : scan caméra ou collage du lien.
- Desktop : collage du lien.
- Le lien contient la clé : **ne pas le publier**.
- Le nouvel appareil crée la liste localement, dérive `channelTag` + clé Nostr,
  souscrit sans `since`, publie son entrée `members` et un delta de présence.
- Révocation d'un membre : non supportée. Pour exclure quelqu'un, recréer la liste
  et repartager la nouvelle clé.

## 7. Notifications

Le contenu d'une liste ne circule **jamais** dans une notification.

### Broker

```http
POST /push
{"app":"colocourse|colotaches","topic":"colo-<32 hex>"}
→ 202 | 400 | 429 | 502
```

- FCM : message **data-only** `{app, kind:"sync"}`.
- UnifiedPush : endpoint par appareil, enregistré via
  `POST /push {"action":"register","app":...,"endpoint":"https://..."}`.
- `/config` expose les capacités (`fcm`, `unifiedpush`).
- Rate-limit : 1 envoi / 5 s / topic.

### Côté application

- FCM/UnifiedPush ne transportent que le jeton `sync`.
- La notification locale est construite par l'app, selon les préférences :
  `notif.mode`, `notif.provider`, `notif.alert`, `notif.signal`.
- `signal = none` : aucune notification ; la synchro reste silencieuse.

### Modes

| `notif.mode` | Effet |
|---|---|
| `off` | aucun travail de fond |
| `deferred` | réveil périodique 15/30/60 min, via ntfy/JobScheduler |
| `immediate` | réveil push (FCM ou UnifiedPush) |

### Fournisseurs

| `notif.provider` | Transport |
|---|---|
| `fcm` | Google FCM, choix explicite, avertissement affiché |
| `unifiedpush` | distributeur UnifiedPush (ntfy), sans Google |
| `auto` | FCM pour l'immédiat, ntfy pour le différé |

## 8. Comportement attendu

1. Modification locale → merge SQLite → delta chiffré → outbox → relais.
2. Réception distante → déchiffrement → merge → notification locale si autorisée.
3. Retour réseau → flush outbox → rattrapage `since`.
4. App au premier plan : WebSocket Qt direct.
5. App en arrière-plan : réveil par FCM/UnifiedPush (immédiat) ou JobScheduler
   (différé).
6. App tuée : la notification push peut arriver ; la synchronisation de données à
   froid n'est pas garantie sur toutes les versions Android. Le mode différé ou
   l'ouverture de l'app rattrape l'état.

## 9. Sécurité et limites

- Le relais voit : `listId`, `channelTag`, tailles, horodatages, adresses IP.
- Il ne voit pas : titre, articles, quantités, membres, photos.
- Aucun secret n'est stocké dans l'APK (hors `google-services.json`, identifiant
  public Firebase).
- Les notifications FCM passent par Google ; UnifiedPush permet de l'éviter.
- Perte si tous les appareils disparaissent : le relais supprime les deltas
  acquittés ; sans appareil pour renvoyer un snapshot, la liste peut être perdue.
- Photos : seules les vignettes voyagent. Pas de HD synchronisée.

## 10. Compatibilité

- Le protocole est **v1**. Les champs inconnus sont ignorés.
- Les anciens payloads `sortMode` restent lisibles mais n'ont plus d'effet.
- Les anciens clients P2P/WebRTC ne sont plus supportés par le produit.
