#!/usr/bin/env python3
# Politique d'écriture Strfry pour Colo Course.
# Chaque liste dérive sa propre clé Nostr : une whitelist de pubkeys ne convient pas.
# On accepte uniquement le kind 4545 (payload chiffré Colo Course).

import json
import sys

COLO_KIND = 4545


def eprint(*args, **kwargs):
    print(*args, **kwargs, file=sys.stderr, flush=True)


def respond(request, action, msg=None):
    out = {"id": request["event"]["id"], "action": action}
    if msg:
        out["msg"] = msg
    print(json.dumps(out, separators=(",", ":")), flush=True)


def main():
    for line in sys.stdin:
        request = json.loads(line)

        if request.get("type") == "lookback":
            continue
        if request.get("type") != "new":
            eprint("unexpected request type in write policy plugin")
            continue

        event = request.get("event") or {}
        if not event.get("id"):
            eprint("input without event id in write policy plugin")
            continue

        try:
            kind = int(event.get("kind", 0))
        except (TypeError, ValueError):
            respond(request, "reject", "blocked: invalid kind")
            continue

        if kind == COLO_KIND:
            respond(request, "accept")
        else:
            respond(request, "reject", f"blocked: kind {kind} not allowed on this relay")


if __name__ == "__main__":
    main()
