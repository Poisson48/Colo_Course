#!/usr/bin/env bash
# Ancien installateur — conservé pour compatibilité.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
echo "→ redirection vers deploy/server/deploy-all.sh" >&2
exec bash "$ROOT/deploy/server/deploy-all.sh" "$@"
