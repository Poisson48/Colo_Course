#!/usr/bin/env bash
# Ancien installateur — conservé pour compatibilité.
# Il écrasait tout le vhost colo-apps et lançait le service en root ; ne pas l'utiliser.
# Le remplacement idempotent vit dans deploy/server/deploy-signaling.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
echo "→ redirection vers deploy/server/deploy-signaling.sh (patch ciblé, service non-root)" >&2
exec bash "$ROOT/deploy/server/deploy-signaling.sh" "$@"
