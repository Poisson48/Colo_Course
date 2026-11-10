#!/usr/bin/env python3
"""Envoie un STUN Binding Request UDP et affiche la réponse.

Usage : python3 scripts/stun-probe.py HOST PORT
Sortie 0 si une Binding Response (0x0101) est reçue.
"""
import os
import socket
import struct
import sys

def main() -> int:
    if len(sys.argv) != 3:
        print("usage: stun-probe.py HOST PORT", file=sys.stderr)
        return 2
    host, port = sys.argv[1], int(sys.argv[2])
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5)
    try:
        txid = os.urandom(12)
        pkt = struct.pack(">HHI", 0x0001, 0, 0x2112A442) + txid
        sock.sendto(pkt, (host, port))
        data, _ = sock.recvfrom(2048)
        if len(data) < 20:
            print("réponse trop courte", file=sys.stderr)
            return 1
        msg_type = struct.unpack(">H", data[:2])[0]
        print(f"STUN response type=0x{msg_type:04x} len={len(data)}")
        return 0 if msg_type == 0x0101 else 1
    except Exception as exc:
        print(f"STUN {host}:{port} échec: {exc}", file=sys.stderr)
        return 1
    finally:
        sock.close()

if __name__ == "__main__":
    raise SystemExit(main())
