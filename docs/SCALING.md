# Dimensionnement : combien d’appareils, de listes, de ports TURN ?

## Ce qui compte n’est pas le nombre d’appareils

C’est le nombre de **connexions WebRTC simultanées** et le nombre d’allocations TURN
actives. Un appareil peut avoir 15 listes sans ouvrir 15 connexions s’il est seul
dans ces listes.

### P2P en maillage

Pour une liste avec `N` appareils en ligne, le maillage crée :

- `N × (N - 1) / 2` connexions pour toute la liste ;
- `N - 1` connexions par appareil.

Exemples :

| Appareils sur une liste | Connexions totales |
|---|---|
| 2 | 1 |
| 4 | 6 |
| 8 | 28 |
| 20 | 190 |
| 500 | 124 750 |

Un maillage complet à 500 appareils est **impossible** en pratique (signaling,
CPU, NAT, TURN). Ce n’est pas un réglage de ports : c’est une limite d’architecture.

### Limites actuelles

- `MAX_PEERS_PER_ROOM=8` : au-delà, les appareils suivants restent sur le relais ;
- `MAX_ROOMS_PER_CLIENT=32` ;
- une `PeerConnection` par couple **(appareil, liste)** ;
- relais Nostr = chemin durable pour tout le monde, P2P = accélération locale.

Le serveur refuse proprement les pairs au-delà de la limite : l’app continue en
relais, sans perte. C’est le mode à utiliser pour les grands groupes.

## Ports TURN

Coturn alloue **un port de relais par allocation TURN active**, pas un port par
paquet ni un port par appareil au repos. La taille de la plage = nombre maximum
d’allocations simultanées.

Avec la plage actuelle `50000–50019`, on a **20 allocations simultanées**. C’est
dimensionné pour une famille/coloc (2–5 appareils, quelques listes). Ce n’est pas
dimensionné pour 500 appareils.

Pour un plus grand déploiement, deux choix :

1. **Recommandé : relais seul / P2P limité aux petits groupes.**
   - `syncMode=0` (relais seul) pour les grandes listes ;
   - ou plafonner `MAX_PEERS_PER_ROOM` à 2–4 ;
   - TURN ne sert qu’aux quelques connexions P2P, la plage reste petite.
2. **P2P à grande échelle : SFU / cluster TURN.**
   - plusieurs IP publiques, plage de ports large, signaling réparti ;
   - ce n’est plus la même architecture ; à chiffrer séparément.

## 500 appareils × 15 listes : ce qu’il faut faire

- Si les 15 listes sont **petites** (2–4 appareils par liste) : le P2P reste local
  et la plage de 20 ports peut suffire ; sinon passer à 100 ports.
- Si les 500 appareils sont **sur les mêmes listes** : ne pas utiliser le maillage.
  Passer ces listes en **relais seul** (ou plafonner les pairs P2P), et laisser
  Strfry absorber le fan-out.
- Si le besoin est vraiment « 500 utilisateurs en P2P simultané » : revoir
  l’architecture (SFU/cluster), pas seulement la box.

## Réglages serveur

```ini
MAX_ROOMS_PER_CLIENT=32
MAX_PEERS_PER_ROOM=8
```

Pour un déploiement plus large, baisser `MAX_PEERS_PER_ROOM` (2–4) est plus sûr
que d’ouvrir des milliers de ports TURN. Le relais reste le chemin durable.
