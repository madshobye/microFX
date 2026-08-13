#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST=${1:-${CANVAS_HOST:-192.168.3.109}}
OUTPUT=${2:-"$PLATFORM_DIR/artifacts/device-screen-$(date -u +%Y%m%d-%H%M%S).png"}
KEY="$PLATFORM_DIR/private/canvas_debug_ed25519"
SSH="ssh -i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2"

[ -f "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }
mkdir -p "$(dirname "$OUTPUT")"

$SSH "root@$HOST" "rm -f /tmp/canvas-screen.png /tmp/canvas-screen.tmp.png; touch /tmp/canvas-capture.request"

ready=0
for attempt in $(seq 1 40); do
  if $SSH "root@$HOST" "test -s /tmp/canvas-screen.png"; then
    ready=1
    break
  fi
  sleep 0.25
done

[ "$ready" = "1" ] || { echo "Timed out waiting for device screenshot" >&2; exit 1; }
scp -O -i "$KEY" -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new \
  -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2 \
  "root@$HOST:/tmp/canvas-screen.png" "$OUTPUT"
echo "$OUTPUT"
