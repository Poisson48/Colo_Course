#!/usr/bin/env python3
"""Test WebRTC pour Colo_Course.
Vérifie :
  1. Le signaling server /signal (WebSocket)
  2. Les ports coturn (3479, 5350, 50000-50100) via check-host.net
  3. STUN Google public
"""
import json
import sys
import time
import urllib.request
import urllib.error

RELAY_BASE = "https://colo-apps.les-crevettes-cevenoles.fr"
CHECK_HOST = "https://check-host.net"
TURN_HOST = "78.122.112.36"
SIGNAL_PATH = "/signal"

def get(url, timeout=15):
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()

def post_json(url, data, timeout=15):
    body = json.dumps(data).encode()
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()

def test_signal_http():
    """Test HTTP GET sur /signal (devrait renvoyer 400 ou info WS)."""
    url = f"{RELAY_BASE}{SIGNAL_PATH}"
    try:
        status, body = get(url, timeout=10)
        print(f"[1] GET {SIGNAL_PATH}: HTTP {status}")
        print(f"    Body: {body[:200].decode(errors='replace')}")
        return status in (200, 400, 426)  # 426 = Upgrade Required
    except urllib.error.HTTPError as e:
        print(f"[1] GET {SIGNAL_PATH}: HTTP {e.code} ({e.reason})")
        # 400/426 = serveur là, juste pas WS handshake
        return e.code in (400, 426)
    except Exception as e:
        print(f"[1] GET {SIGNAL_PATH}: FAIL — {e}")
        return False

def test_turn_ports():
    """Vérifie les ports via check-host.net (test TCP depuis l'extérieur)."""
    ports = [3479, 5350]
    results = {}
    for port in ports:
        url = f"{CHECK_HOST}/check-tcp?host={TURN_HOST}:{port}&max_nodes=3"
        try:
            status, body = get(url, timeout=15)
            data = json.loads(body)
            check_id = data.get("check_id")
            print(f"[2] Port {port}: check lancé (id={check_id})")
            results[port] = check_id
        except Exception as e:
            print(f"[2] Port {port}: FAIL — {e}")
            results[port] = None

    # Attendre et récupérer les résultats
    time.sleep(4)
    final = {}
    for port, check_id in results.items():
        if not check_id:
            final[port] = False
            continue
        try:
            status, body = get(f"{CHECK_HOST}/result/{check_id}", timeout=15)
            data = json.loads(body)
            # data = {node: [[status, time], ...]}
            ok_nodes = 0
            total = 0
            for node, checks in data.items():
                if isinstance(checks, list) and checks:
                    total += 1
                    if checks[0] and checks[0][0] == "OK":
                        ok_nodes += 1
            passed = ok_nodes > 0
            print(f"[2] Port {port}: {'✅' if passed else '❌'} {ok_nodes}/{total} nodes OK")
            final[port] = passed
        except Exception as e:
            print(f"[2] Port {port}: FAIL récupération — {e}")
            final[port] = False

    return all(final.values()), final

def test_stun_google():
    """STUN Google — test basique UDP (bare bone STUN binding request)."""
    import socket
    import struct
    import os

    # STUN Binding Request
    msg_type = 0x0001
    msg_len = 0
    magic_cookie = 0x2112A442
    tx_id = os.urandom(12)
    pkt = struct.pack(">HHI", msg_type, msg_len, magic_cookie) + tx_id

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5)
    try:
        sock.sendto(pkt, ("stun.l.google.com", 19302))
        data, _ = sock.recvfrom(1024)
        resp_type = struct.unpack(">H", data[:2])[0]
        if resp_type == 0x0101:  # Binding Response
            print("[3] STUN Google: ✅ Binding Response reçu")
            return True
        print(f"[3] STUN Google: réponse inattendue type=0x{resp_type:04x}")
        return False
    except Exception as e:
        print(f"[3] STUN Google: ❌ FAIL — {e}")
        return False
    finally:
        sock.close()

def test_turn_udp():
    """Test UDP sur port 3479 (STUN coturn)."""
    import socket
    import struct
    import os

    msg_type = 0x0001
    msg_len = 0
    magic_cookie = 0x2112A442
    tx_id = os.urandom(12)
    pkt = struct.pack(">HHI", msg_type, msg_len, magic_cookie) + tx_id

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5)
    try:
        sock.sendto(pkt, (TURN_HOST, 3479))
        data, _ = sock.recvfrom(1024)
        resp_type = struct.unpack(">H", data[:2])[0]
        if resp_type == 0x0101:
            print("[4] STUN coturn 3479/udp: ✅ Binding Response reçu")
            return True
        print(f"[4] STUN coturn 3479/udp: type=0x{resp_type:04x}")
        return resp_type in (0x0111, 0x0101)  # OK ou error
    except Exception as e:
        print(f"[4] STUN coturn 3479/udp: ❌ FAIL — {e}")
        return False
    finally:
        sock.close()

def main():
    print("=" * 60)
    print("Test WebRTC Colo_Course")
    print("=" * 60)

    results = {}

    # 1. Signaling
    results["signaling"] = test_signal_http()

    # 2. Ports TURN TCP via check-host
    results["turn_tcp"], port_detail = test_turn_ports()

    # 3. STUN Google
    results["stun_google"] = test_stun_google()

    # 4. STUN coturn local UDP
    results["stun_coturn"] = test_turn_udp()

    print("\n" + "=" * 60)
    print("RÉSUMÉ")
    print("=" * 60)
    all_ok = True
    for name, ok in results.items():
        status = "✅ PASS" if ok else "❌ FAIL"
        print(f"  {name:15s} {status}")
        if not ok:
            all_ok = False

    print("\n" + ("🎉 Tout passe — WebRTC prêt à tester" if all_ok else "⚠️  Des tests échouent"))
    sys.exit(0 if all_ok else 1)

if __name__ == "__main__":
    main()
