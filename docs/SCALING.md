# Dimensionnement

Dernière mise à jour : **2026-11-12**. Le P2P/TURN est retiré : plus de maillage,
plus d'allocations TURN, plus de signaling. Le facteur limitant est le relais
Nostr et les notifications.

## Hypothèses

- 1 000 utilisateurs simultanés ;
- groupes de 15 ;
- 20 listes par utilisateur ;
- propagation cible < 10 s ;
- un changement = un événement chiffré de quelques centaines d'octets.

## Relais Nostr

- 1 000 WebSockets ;
- ~20 000 abonnements (`device × listes`) ;
- fan-out d'un événement à 14 autres membres : le relais rediffuse à ses
  abonnés ; pas de matrice événement × appareil côté serveur ;
- Strfry sur le serveur actuel absorbe ce volume ; valider par un test de charge
  avant une montée au-delà de quelques milliers de clients.

## Notifications

| Mode | Charge serveur |
|---|---|
| Différé | jobs locaux, aucun flux push permanent |
| FCM | un appel Google par changement, rate-limité 1/5 s/topic |
| UnifiedPush | un POST par endpoint enregistré et par changement |

- FCM : Google gère la livraison et la mise en veille ; coût serveur minime.
- UnifiedPush : un endpoint par appareil ; le broker fait un POST par endpoint.
  Pour de grands volumes, limiter les endpoints et regrouper les notifications.
- Le broker est un simple Node : le garder derrière nginx et le rate-limit.

## Stockage

- Relais : blobs chiffrés, deltas + snapshots ; rétention pilotée par les acks.
- Vignettes : taille limitée, pas de HD.
- Registre UnifiedPush : fichier JSON, quelques dizaines d'octets par endpoint.
- Pas de base relationnelle nécessaire pour l'usage actuel.

## Passer à l'échelle

1. Mesurer : nombre de WebSockets, abonnements, latence p95, volume/jour.
2. Ajouter des relais Nostr en écriture/lecture et configurer plusieurs URL.
3. Si le broker devient un goulot : file d'attente + workers, ou FCM/UnifiedPush
   en parallèle.
4. Séparer relais, broker et ntfy sur des machines distinctes si nécessaire.
5. Ne pas réintroduire de P2P/TURN pour ce besoin : le relais tient la charge et
   reste le chemin unique.
