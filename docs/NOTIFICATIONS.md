# Notifications — plan POST-migration v4

> **Statut : à lancer après le merge de la v4 (v0.32.0).** Ne pas démarrer avant.
> Référence : « Plan de migration totale v4 — Colo Course + Colo Tâches ».
> Contrats à valider avec l'agent v4 s'il est encore actif au moment du lancement.

Objectif : remplacer la boucle de poll toutes les 5 s (≈ 16 000 requêtes/jour,
2–12 Go/mois) par un choix explicite à trois niveaux, avec un budget cible de
**< 100 requêtes/jour et < 15 Mo/mois**, sans dépendance obligatoire à Google ni
à colo-apps.

| Décision | Contenu |
|---|---|
| **Trois modes** | Désactivées · Différées (15 min) · Immédiates |
| **Défaut** | Différées |
| **Différées** | WorkManager, une requête multi-topics, aucun service permanent, aucune notification persistante |
| **Immédiates** | Push système (FCM) ou connexion maintenue ; capacités découvertes via `/config` |
| **Self-host** | ntfy est déjà dans le kit v4 → Différées fonctionne sans Google ; FCM reste une commodité de l'instance Colo |
| **Signal** | `POST /push {app, topic}` → fan-out FCM + ntfy. Aucun titre, aucune donnée de liste |
| **Cible** | < 100 requêtes/jour, < 15 Mo/mois |

La refonte des notifications ne figure pas dans la v4, et sa §14 exclut
explicitement « Notifications push Tâches ». C'est donc un chantier séparé, à
insérer **après** la migration.

---

## 0. Prérequis à vérifier avant d'écrire une ligne (30 min)

| Point | Vérification |
|---|---|
| Version | `main` = v0.32.0, CI verte, tag posé |
| `/config` | répond `protocol: 2`, `apps`, `signaling`, `turnCredentials`, `turn` |
| nginx | bloc `# BEGIN colosync` présent ; `/ntfy/` et `/releases/` intacts |
| Dialogue | structure post-phase D (Serveur P2P + bouton Tester + Avancé) |
| Hooks | `platformConfigurePush`, `pushEnabled`, `setDeferBackgroundNotificationsToPush` toujours présents |
| Android | APK v0.32.0 s'installe, OpenSSL lié, `scripts/build-android.sh` OK |
| Partagé | mécanisme « fichiers canoniques Course → Tâches » + checksum CI en place |

Si un point manque : petit préalable de 0,5 j (déclarer `/config.push`, ajouter
`location = /push`, rétablir les hooks) avant de commencer. Ne pas l'empiler sur
la migration.

---

## 1. Contrats à figer

- **Réglages** : `notif.mode` = `off|deferred|immediate`, `notif.provider` =
  `auto|fcm|ntfy|socket`, `notif.interval` (15/30/60), `notif.wifiOnly`,
  `notif.lastCheck` (epoch).
- **Migration** : `pushEnabled=1` → `deferred`, `0` → `off`.
  `pushBaseUrl` devient `notif.ntfyUrl`.
- **`/config`** (additif) : `push: { enabled, provider, endpoint }`,
  `ntfy: { baseUrl }` — optionnels, ignorés s'ils sont absents.
- **`/push`** : `POST {app, topic}`, 202/429, rate-limit par topic, aucun titre,
  aucune donnée.
- **Topics** : `colo-<channelTag>` pour ntfy et FCM.
- **Notification** : data-only, titre lu localement, deep link
  `colocourse://list/<id>`.

---

## 2. M1 — v0.33.0 : le choix et la sobriété · 5,5 j

Objectif : plus de boucle 5 s, un choix à trois modes, la batterie réglée.
Aucune dépendance Google, aucun changement de protocole.

### P1 — Socle Android · 1,5 j

- `PushService` → Worker WorkManager : `GET /t1,…,tN/json?poll=1&since=<ts>`,
  toutes les listes en **une** requête, `since` persisté, intervalle 15/30/60,
  contrainte réseau, persistance reboot.
- Notification groupée, deep link, plus de notification permanente.
- Suppression de la boucle actuelle.
- **Fichiers** : `android/…/PushService.java`, `Platform.java`,
  `AndroidManifest.xml`.
- **Acceptation** : 0 requête en `off`, ≤ 96/jour en différé, Doze/reboot/swipe OK.

### P2 — Pont C++, ping, service silencieux · 1,5 j

- `configureNotifications(mode, provider, interval, wifiOnly, topics)` +
  `notificationsStatus()`.
- Clés `notif.*`, migration, `pushBaseUrl` → `notif.ntfyUrl`.
- **Mode socket** : le service reste vivant mais **ne fait aucune requête** quand
  la WS est connectée ; ping applicatif toutes les 90 s ; exemption batterie
  demandée ; repli sur différé si refus.
- **Fichiers** : `src/app/platform.cpp/.h`, `appcontroller.cpp`,
  `syncengine.*`, `RelayClient` (ping), `PushService`.
- **Acceptation** : WS vivante 12 h sans coupure sur mobile ; aucune requête du
  service tant qu'elle l'est.

### P3 — UX · 1,5 j

- Écran 1er démarrage (3 options + « Service de push »), section Réglages (même
  choix, état réel, dernière vérif, données du mois, bouton test).
- Copie validée : vouvoiement, coûts factuels, aucun jargon.
- Un seul encart pour les installations existantes : « Choisissez votre mode de
  notifications » (non bloquant, une fois).
- **Fichiers** : `src/qml/ListsPage.qml`, écran d'accueil.

*Tests et validation : 1 j inclus.*

---

## 3. M2 — v0.34.0 : le push système · 4,5 à 5,5 j

Objectif : « Immédiates » à coût quasi nul, sans changer le choix utilisateur.

### P4 — `/push` broker + `/config` · 1,5 j

- `server.js` : `/push` → fan-out **FCM + ntfy**, rate-limit, métriques,
  `/config.push` et `/config.ntfy`.
- nginx : `location = /push` via `deploy/server/patch-nginx.py` (pas d'édition
  manuelle).
- Client : POST vers `/push` si déclaré, sinon POST direct ntfy (compatibilité
  self-host).
- **Acceptation** : 202/429 corrects, aucun titre transmis, self-host sans
  `/push` → repli sans erreur.

### P5 — FCM · 2 j

- Gradle + `google-services.json` + `FirebaseMessagingService`, abonnement
  `colo-<channelTag>`, message data-only vide.
- Activation **uniquement** si `/config.push` est présent.
- **Acceptation** : notification < 5 s app fermée et en Doze ; aucune clé dans
  l'APK ; APK sans Play Services → repli propre.

### P6 — Déclencheur côté relais · 1 j *(optionnel)*

- Démon qui écoute strfry et appelle `/push` sur chaque kind 4545. Supprime la
  sonnette perdue quand l'émetteur est hors ligne.
- À ne faire que si M2 est stable et le temps disponible.

### P7 — Tâches · 1 j

- Copier les fichiers canoniques (Java + pont + worker), brancher le réglage,
  ajouter au checksum partagé.
- Colo Tâches n'a aujourd'hui aucun service de push : c'est un ajout net, pas une
  migration.

---

## 4. Ordre, branches, PR

| PR | Contenu | Base | Parallélisable |
|---|---|---|---|
| 1 | P1 (Java seul) | `main` | oui, avec la PR 3 |
| 2 | P2 (pont + ping) | après PR 1 | non |
| 3 | P4 (serveur) | `main` | oui, avec la PR 1 |
| 4 | P3 (UI) | après PR 2 + PR 3 | non |
| 5 | P5 (FCM) | après PR 4 | non |
| 6 | P6/P7 | après PR 5 | oui |

Chaque PR doit être déployable seule. Le mode par défaut reste **Différées** tant
que M2 n'est pas validé.

---

## 5. Tests et critères d'acceptation

**Mesures**

- `off` : 0 requête, 0 Mo, 0 % batterie.
- `deferred` : ≤ 96 requêtes/jour, < 15 Mo/mois, < 1 % sur 24 h écran éteint.
- `socket` : 3–8 %/jour en 4G, mesuré et affiché dans l'UI.
- `fcm` : < 5 s, Doze inclus, ~0 %.

**Cas**

- Doze (`adb shell dumpsys deviceidle force-idle`), reboot, swipe, force-stop,
  permission refusée, exemption refusée.
- Émetteur hors ligne puis reconnecté (données OK, notification seulement si P6).
- Deux apps sur le même serveur : pas de collision de topic, métriques séparées.
- Self-host : différé fonctionne, `/push` absent → repli, FCM masqué.
- `grep -rn "colo-apps" src/` : uniquement des valeurs par défaut.

---

## 6. Migration, release, rollback

- **v0.33.0** : M1. Changelog : « la veille ne consomme plus rien : vérification
  toutes les 15 min, choix dans Réglages ». Migration silencieuse vers Différées
  + encart unique.
- **v0.34.0** : M2. Changelog : « notifications instantanées via le système,
  optionnelles ».
- **Rollback** : serveur d'abord (retirer `/push`), le client retombe sur ntfy
  direct ; côté app, un revert de P5 suffit à revenir au mode socket.
- Garder `protocol: 2` : `/push` et `push` dans `/config` sont additifs, aucune
  rupture.

---

## 7. Risques

- **Gradle/Firebase** : le vrai point dur. PR dédiée, build CI vérifié, test d'un
  APK sans Play Services.
- **WorkManager qui réveille Qt** : à mesurer, sinon le gain disparaît.
- **Doze** : le différé peut glisser de plusieurs heures — l'écrire dans l'UI.
- **FCM lié à l'instance Colo** : un self-host n'a pas de projet Firebase →
  l'option doit disparaître quand `/config.push` est absent.
- **Deux apps** : rate-limits et métriques par app dès P4.

---

## 8. Hors périmètre

- UnifiedPush (à décider après M2, seulement si « instantané sans Google » est un
  besoin réel).
- Profils multi-serveurs par liste.
- Refonte du transport / des photos / de l'outbox (v4).
- Notifications desktop (le tray reste tel quel).

**Total : ~10 à 11 jours**, dont 5,5 j sans aucune dépendance serveur ni Google —
c'est le lot à lancer en premier.

---

## 9. J0 — premières actions

1. Vérifier les prérequis du §0 (30 min).
2. Écrire/compléter ce document avec les contrats du §1 validés.
3. Ouvrir `fix/notif-battery` (PR 1) et `feat/push-broker` (PR 3) en parallèle.
4. Lancer P1 et P4 le même jour : ce sont les deux seules tâches sans dépendance
   croisée.
