#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST=${CANVAS_HOST:-192.168.3.109}
KEY="$PLATFORM_DIR/private/canvas_debug_ed25519"

# Accept the documented `canvas-ssh.sh HOST [COMMAND...]` form while keeping
# CANVAS_HOST and command-only usage compatible.
case "${1:-}" in
  *.*.*.*|*:* ) HOST=$1; shift ;;
esac

[ -f "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }
exec ssh -i "$KEY" -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new \
  -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2 \
  "root@$HOST" "$@"
