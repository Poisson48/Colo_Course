# Ouvrir les ports TURN sur la box NB6

Le serveur Colo est derrière une **NB6** (`192.168.1.1`). Le signaling passe par
nginx en 443 : **aucun port supplémentaire** n'est nécessaire pour lui. TURN,
lui, exige des redirections NAT.

## Valeurs à saisir

| Règle | Port externe | Proto | IP interne | Port interne |
|---|---|---|---|---|
| `colosync-turn-udp` | 3479 | **UDP** | 192.168.1.40 | 3479 |
| `colosync-turn-tcp` | 3479 | **TCP** | 192.168.1.40 | 3479 |
| `colosync-turns` | 5350 | **TCP** | 192.168.1.40 | 5350 |
| `colosync-turn-relay` | 50000–50100 | **UDP** | 192.168.1.40 | 50000–50100 |

Ne pas toucher au **3478** (STUN meownopoly). Ne pas utiliser de DMZ.

## Navigation NB6

1. `http://192.168.1.1`, compte admin.
2. **Paramètres du routeur → NAT → Redirection de ports**
   (ou **Game & Application Sharing → Create a new game or application**
   selon le firmware).
3. Créer chaque règle avec les valeurs ci-dessus.
4. Pour la plage 50000–50100, créer une application personnalisée si le champ
   n'accepte qu'un port.
5. Vérifier qu'aucune règle existante ne pointe 3479/5350/50000–50100 ailleurs.
6. Appliquer. Réserver si possible `192.168.1.40` dans le DHCP.

## Vérification depuis l'extérieur

```bash
nc -vz 78.122.112.36 3479
nc -vz 78.122.112.36 5350
python3 scripts/stun-probe.py 78.122.112.36 3479   # Binding Response attendu
curl -fsS 'https://colo-apps.les-crevettes-cevenoles.fr/turn-credentials?app=colocourse&device_id=probe'
# puis allocation TURN réelle depuis un téléphone 4G
```

Un test depuis le serveur lui-même ne prouve rien : la NB6 n'a pas de NAT
loopback.
