# STUN/TURN — colo-apps.les-crevettes-cevenoles.fr

| Élément | Valeur |
|---|---|
| **STUN** | `stun:colo-apps.les-crevettes-cevenoles.fr:3479` |
| **TURN UDP** | `turn:colo-apps.les-crevettes-cevenoles.fr:3479` |
| **TURNS (TLS)** | `turns:colo-apps.les-crevettes-cevenoles.fr:5350` |
| **Backend** | coturn (même serveur que Strfry + ntfy) |
| **Auth** | `use-auth-secret` (HMAC, clé partagée avec l'app) |
| **Certificat** | Let's Encrypt existant (même que le relay) |

⚠️ **Port 3478 = STUN meownopoly** (jeu + chat d'un autre utilisateur). On n'y touche pas. Notre coturn utilise le **3479**.

## Installation

```bash
cd deploy/coturn
sudo ./install.sh
```

Le script :
1. Vérifie que les ports 3479/5350 sont libres
2. Vérifie que le STUN meownopoly (:3478) est intact
3. Vérifie que les services existants (nginx, strfry) tournent
4. Installe coturn via apt
5. Génère un secret aléatoire et remplace le placeholder
6. Active et démarre le service
7. Vérifie que les ports sont bien ouverts et que :3478 est toujours là

## Ports à ouvrir sur la box

| Port | Protocole | Usage |
|------|-----------|-------|
| 3479 | UDP | STUN/TURN principal |
| 3479 | TCP | STUN/TURN fallback (réseaux restrictifs) |
| 5350 | TCP | TURNS (TURN over TLS) |
| 50000-50100 | UDP | Plage relay pour les data WebRTC (100 ports) |

## Vérification

```bash
# Service actif ?
systemctl status coturn

# Ports ouverts localement ?
ss -ulnp | grep 3479
ss -tlnp | grep 5350

# Test depuis l'extérieur :
curl -sS "https://check-host.net/check-tcp?host=78.122.112.36:3479&max_nodes=3" -H "Accept: application/json"
```

## Logs

```bash
journalctl -u coturn -f
# ou
tail -f /var/log/coturn/turnserver.log
```

## Architecture réseau

```
Internet :443   ──▶  nginx SNI router
                      ├─ colo-apps.*     → :11443 → Strfry (:7777) + ntfy (:8077) + releases
                      ├─ cloud.*         → :12443 → Nextcloud (:8083)
                      ├─ carte.*         → :13443 → Node (:18542)
                      └─ pattounecorp.*  → :4443  → meownopoly asset_server

Internet :3478  ──▶  STUN meownopoly (autre utilisateur — ne pas toucher)
Internet :3479  ──▶  coturn STUN/TURN (Colo Course WebRTC)
Internet :5350  ──▶  coturn TURNS TLS
Internet :50000-50100/udp ──▶  coturn relay
```

Les ports coturn (3479, 5350, 50000-50100) sont **séparés** de tous les autres services. Aucun conflit possible.