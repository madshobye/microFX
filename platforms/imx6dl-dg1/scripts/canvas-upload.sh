#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
APP_DIR="$REPO_DIR/apps/demo"
HOST=${1:-192.168.3.109}
VM_NAME=${VM_NAME:-microfx-build}
KEY="$PLATFORM_DIR/private/canvas_debug_ed25519"
RELEASE="release-$(date -u +%Y%m%d-%H%M%S)"
SSH="ssh -i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2"
CANVAS_STOPPED=0
mkdir -p "$PLATFORM_DIR/artifacts"
WORK=$(mktemp -d "$PLATFORM_DIR/artifacts/.microfx-upload.XXXXXX")
cleanup() {
  rm -rf "$WORK"
  if [ "$CANVAS_STOPPED" = "1" ]; then
    $SSH "root@$HOST" "/etc/init.d/S40canvas start" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

[ -f "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }

limactl shell "$VM_NAME" -- sh -lc "
  set -eu
  BR=\"\$HOME/buildroot-2025.02.16\"
  OUT=\"\$HOME/microfx-imx6dl-output\"
  make -C \"\$BR\" O=\"\$OUT\" microfx-demo-dirclean
  make -C \"\$BR\" O=\"\$OUT\" microfx-demo
  cp \"\$OUT/build/microfx-demo-1.0.0/canvas-demo\" '$WORK/canvas-demo'
  cp \"\$OUT/build/raylib-drm-5.5/src/libraylib.so.5.5.0\" '$WORK/libraylib.so.550'
"

chmod 0755 "$WORK/canvas-demo"
cp "$APP_DIR/assets/models/icosahedron.obj" "$WORK/icosahedron.obj"
cp "$APP_DIR/scripts/main.js" "$WORK/main.js"
cp "$APP_DIR/assets/shaders/Light.vs" "$WORK/Light.vs"
cp "$APP_DIR/assets/shaders/Light.fs" "$WORK/Light.fs"
printf '%s\n' "$RELEASE" >"$WORK/version"
(cd "$WORK" && shasum -a 256 canvas-demo libraylib.so.550 icosahedron.obj main.js ./*.vs ./*.fs version >manifest.sha256)

$SSH "root@$HOST" "/etc/init.d/S40canvas stop; killall -9 canvas-demo 2>/dev/null || true"
CANVAS_STOPPED=1
$SSH "root@$HOST" "mkdir -p /data/apps/incoming/$RELEASE"
scp -O -i "$KEY" -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new \
  -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2 \
  "$WORK/canvas-demo" "$WORK/libraylib.so.550" "$WORK/icosahedron.obj" "$WORK/main.js" "$WORK"/*.vs "$WORK"/*.fs \
  "$WORK/version" "$WORK/manifest.sha256" \
  "root@$HOST:/data/apps/incoming/$RELEASE/"
$SSH "root@$HOST" "/usr/sbin/canvas-activate $RELEASE"
CANVAS_STOPPED=0

echo "Uploaded and activated $RELEASE on $HOST"
