# Relais Nostr Colo Course — guide d’installation

> Héberger `wss://relay.VOTRE-DOMAINE` sur un VPS, avec sous-domaine OVH.
> Le relais ne voit que des blobs chiffrés (kind `4545`) — pas de comptes, pas de données en clair.

## Prérequis

| Élément | Détail |
|---|---|
| **Domaine** | Géré chez OVH (ou ailleurs, DNS pointé vers le VPS) |
| **VPS** | Ubuntu 22.04/24.04 ou Debian 12 — **pas** un hébergement mutualisé OVH |
| **Ports** | 22 (SSH), 80 et 443 ouverts (pare-feu + groupe de sécurité OVH si applicable) |
| **Accès** | SSH root ou sudo sur le VPS |

Remplacer partout :

- `VOTRE-DOMAINE` → ex. `colocourse.fr`
- `relay.VOTRE-DOMAINE` → ex. `relay.colocourse.fr`
- `IP_DU_VPS` → l’IPv4 publique du serveur

---

## Étape 1 — Sous-domaine OVH

1. Connectez-vous à [OVH](https://www.ovh.com/manager/) → **Noms de domaine**.
2. Cliquez sur votre domaine → onglet **Zone DNS**.
3. **Ajouter une entrée** :
   - **Type** : `A`
   - **Sous-domaine** : `relay` (donne `relay.VOTRE-DOMAINE`)
   - **Cible** : `IP_DU_VPS`
   - **TTL** : 3600 (1 h) ou minimum OVH
4. Validez. Propagation DNS : souvent 5–30 min, parfois jusqu’à 24 h.

Vérifier depuis votre PC :

```bash
dig +short relay.VOTRE-DOMAINE A
# doit afficher IP_DU_VPS
```

---

## Étape 2 — Préparer le VPS

Connectez-vous :

```bash
ssh root@IP_DU_VPS
```

Mises à jour et outils de base :

```bash
apt update && apt upgrade -y
apt install -y curl git build-essential pkg-config \
  liblmdb-dev libssl-dev zlib1g-dev \
  ufw
```

Pare-feu minimal :

```bash
ufw allow OpenSSH
ufw allow 80/tcp
ufw allow 443/tcp
ufw enable
```

Créer un utilisateur dédié (recommandé) :

```bash
adduser --disabled-password --gecos "" strfry
```

---

## Étape 3 — Installer Strfry (relais Nostr)

Strfry est léger et gère bien le trafic WebSocket Nostr.

```bash
su - strfry
cd ~
git clone https://github.com/hoytech/strfry.git
cd strfry
make -j"$(nproc)"
sudo make install   # binaire → /usr/local/bin/strfry
exit   # retour root
```

Configuration :

```bash
mkdir -p /var/lib/strfry
chown strfry:strfry /var/lib/strfry

cat > /etc/strfry.conf << 'EOF'
# Colo Course — relais Nostr local (TLS terminé par Caddy devant)

relay {
    db = "/var/lib/strfry"
    maxEventSize = 65536
    maxReqLimit = 500
}

router {
    bind = "127.0.0.1"
    port = 7777
}
EOF
```

Service systemd :

```bash
cat > /etc/systemd/system/strfry.service << 'EOF'
[Unit]
Description=Strfry Nostr relay (Colo Course)
After=network-online.target
Wants=network-online.target

[Service]
User=strfry
Group=strfry
ExecStart=/usr/local/bin/strfry relay /etc/strfry.conf
Restart=on-failure
RestartSec=5
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now strfry
systemctl status strfry   # doit être active (running)
```

Test local :

```bash
curl -i -N \
  -H "Connection: Upgrade" \
  -H "Upgrade: websocket" \
  -H "Sec-WebSocket-Version: 13" \
  -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
  http://127.0.0.1:7777/
# une réponse 101 Switching Protocols ou une frame WebSocket = OK
```

---

## Étape 4 — TLS avec Caddy (Let’s Encrypt automatique)

```bash
apt install -y debian-keyring debian-archive-keyring apt-transport-https curl
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | tee /etc/apt/sources.list.d/caddy-stable.list
apt update && apt install -y caddy
```

Remplacez le domaine dans la config :

```bash
cat > /etc/caddy/Caddyfile << 'EOF'
relay.VOTRE-DOMAINE {
    reverse_proxy 127.0.0.1:7777
}
EOF
```

```bash
systemctl reload caddy
systemctl status caddy
```

Caddy obtient le certificat Let’s Encrypt au premier accès HTTPS. Vérifier :

```bash
curl -I https://relay.VOTRE-DOMAINE
```

---

## Étape 5 — Tester le relais Nostr

Depuis votre PC (avec `nodejs` ou un client Nostr) :

```bash
# Ping WebSocket (nécessite wscat : npm i -g wscat)
wscat -c wss://relay.VOTRE-DOMAINE
```

Envoyer un message Nostr minimal (optionnel, pour valider publish) :

```json
["REQ","test",{"kinds":[4545],"limit":1}]
```

Réponse attendue : `["EOSE","test"]` ou des événements — pas de coupure immédiate.

Sur le VPS, logs :

```bash
journalctl -u strfry -f
journalctl -u caddy -f
```

---

## Étape 6 — Brancher Colo Course

URL du relais : `wss://relay.VOTRE-DOMAINE`

### Option A — Default dans l’app (release)

Modifier `src/net/relaypool.cpp` :

```cpp
return {
    QUrl("wss://relay.VOTRE-DOMAINE"),
    // secours public optionnel :
    // QUrl("wss://nos.lol"),
};
```

### Option B — Déjà installé (base SQLite locale)

La clé `relays` dans la table `settings` (virgules entre URLs). À changer uniquement si vous savez où est la base — préférer Option A pour les utilisateurs finaux.

### Stratégie recommandée

1. **Relais principal** : le vôtre (`wss://relay.VOTRE-DOMAINE`)
2. **Secours** (optionnel) : un relais public (`wss://nos.lol`) si le vôtre est en maintenance

Publier sur 1 relais principal suffit pour Colo Course ; le multi-relais actuel (×4) multiplie la charge inutilement.

---

## Maintenance

| Tâche | Commande |
|---|---|
| Logs Strfry | `journalctl -u strfry -f` |
| Redémarrer | `systemctl restart strfry` |
| Espace disque | `du -sh /var/lib/strfry` |
| Màj Strfry | `cd ~/strfry && git pull && make && sudo make install && systemctl restart strfry` |

Strfry purge les vieux événements selon sa config ; pour Colo Course (petits deltas chiffrés), **10–20 Go** de disque suffisent largement au départ.

---

## Dépannage

| Symptôme | Piste |
|---|---|
| `dig` ne renvoie pas la bonne IP | Attendre propagation DNS ; vérifier l’entrée A chez OVH |
| Certificat TLS échoue | Le port 80 doit être joignable depuis Internet (Let’s Encrypt HTTP-01) |
| App « hors ligne » | `systemctl status strfry caddy` ; pare-feu OVH (manager → IP → pare-feu) |
| Publish rejeté | `journalctl -u strfry` ; événement > 64 Ko rare pour Colo Course |

---

## Sécurité (bonnes pratiques)

- SSH par clé, `PasswordAuthentication no` une fois la clé en place
- Pas d’exposition directe du port 7777 sur Internet (127.0.0.1 + Caddy seulement)
- Sauvegardes optionnelles de `/var/lib/strfry` (les données sont chiffrées — perte = resync via snapshots CRDT côté clients)

---

## Checklist rapide

- [ ] Entrée DNS `A` `relay` → IP du VPS (OVH)
- [ ] Strfry actif sur `127.0.0.1:7777`
- [ ] Caddy + certificat HTTPS pour `relay.VOTRE-DOMAINE`
- [ ] Test `wscat` ou client Nostr OK
- [ ] URL `wss://…` dans Colo Course (default ou réglages)
- [ ] Deux téléphones : créer une liste, vérifier la synchro
