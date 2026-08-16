#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
APP_DIR="$REPO_DIR/apps/demo"
HOST=${1:-192.168.3.109}
. "$PLATFORM_DIR/scripts/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"
microfx_require_build_vm
KEY="$PLATFORM_DIR/private/canvas_debug_ed25519"
RELEASE="release-$(date -u +%Y%m%d-%H%M%S)"
SSH="ssh -i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2"
mkdir -p "$PLATFORM_DIR/artifacts"
WORK=$(mktemp -d "$PLATFORM_DIR/artifacts/.microfx-upload.XXXXXX")
cleanup() {
  rm -rf "$WORK"
}
trap cleanup EXIT

[ -f "$KEY" ] || { echo "Missing SSH key: $KEY" >&2; exit 1; }

# Build only the graphics/runtime packages. Firmware and the Linux cache are
# deliberately outside this development upload path.
"$PLATFORM_DIR/scripts/build-graphics.sh"

GUEST_HOME=$(limactl shell "$VM_NAME" -- printenv HOME)
limactl shell "$VM_NAME" -- test -f \
  "$GUEST_HOME/microfx-imx6dl-output/build/raylib-drm-5.5/src/libraylib.so.5.5.0"
limactl copy --backend=scp \
  "$VM_NAME:$GUEST_HOME/microfx-imx6dl-output/build/microfx-demo-1.0.0/canvas-demo" \
  "$WORK/canvas-demo"
limactl copy --backend=scp \
  "$VM_NAME:$GUEST_HOME/microfx-imx6dl-output/build/raylib-drm-5.5/src/libraylib.so.5.5.0" \
  "$WORK/libraylib.so.550"
QR_LIBRARY=$(limactl shell "$VM_NAME" -- readlink -f \
  "$GUEST_HOME/microfx-imx6dl-output/target/usr/lib/libqrencode.so.4")
[ -n "$QR_LIBRARY" ] || { echo "Cross-build did not install libqrencode.so.4" >&2; exit 1; }
limactl copy --backend=scp "$VM_NAME:$QR_LIBRARY" "$WORK/libqrencode.so.4"
HTTP_LIBRARY=$(limactl shell "$VM_NAME" -- readlink -f \
  "$GUEST_HOME/microfx-imx6dl-output/target/usr/lib/libcurl.so.4")
[ -n "$HTTP_LIBRARY" ] || { echo "Cross-build did not install libcurl.so.4" >&2; exit 1; }
limactl copy --backend=scp "$VM_NAME:$HTTP_LIBRARY" "$WORK/libcurl.so.4"
HDF5_LIBRARY=$(limactl shell "$VM_NAME" -- readlink -f \
  "$GUEST_HOME/microfx-imx6dl-output/target/usr/lib/libhdf5.so.310")
[ -n "$HDF5_LIBRARY" ] || { echo "Cross-build did not install libhdf5.so.310" >&2; exit 1; }
limactl copy --backend=scp "$VM_NAME:$HDF5_LIBRARY" "$WORK/libhdf5.so.310"
HDF5_HL_LIBRARY=$(limactl shell "$VM_NAME" -- readlink -f \
  "$GUEST_HOME/microfx-imx6dl-output/target/usr/lib/libhdf5_hl.so.310")
[ -n "$HDF5_HL_LIBRARY" ] || { echo "Cross-build did not install libhdf5_hl.so.310" >&2; exit 1; }
limactl copy --backend=scp "$VM_NAME:$HDF5_HL_LIBRARY" "$WORK/libhdf5_hl.so.310"

chmod 0755 "$WORK/canvas-demo"
cp "$APP_DIR/assets/models/icosahedron.obj" "$WORK/icosahedron.obj"
cp "$APP_DIR/scripts/main.js" "$WORK/main.js"
cp "$APP_DIR/assets/shaders/Light.vs" "$WORK/Light.vs"
cp "$APP_DIR/assets/shaders/Light.fs" "$WORK/Light.fs"
printf '%s\n' "$RELEASE" >"$WORK/version"
(cd "$WORK" && shasum -a 256 canvas-demo libraylib.so.550 libqrencode.so.4 \
  libcurl.so.4 libhdf5.so.310 libhdf5_hl.so.310 icosahedron.obj main.js \
  ./*.vs ./*.fs version >manifest.sha256)

# The current runtime stays running while the isolated incoming directory is
# transferred. canvas-activate validates it, replaces the one fixed runtime
# directory, and owns the single stop/start transition.
$SSH "root@$HOST" "mkdir -p /data/apps/incoming/$RELEASE"
scp -O -i "$KEY" -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new \
  -o ConnectTimeout=5 -o ServerAliveInterval=3 -o ServerAliveCountMax=2 \
  "$WORK/canvas-demo" "$WORK/libraylib.so.550" "$WORK/libqrencode.so.4" \
  "$WORK/libcurl.so.4" "$WORK/libhdf5.so.310" "$WORK/libhdf5_hl.so.310" \
  "$WORK/icosahedron.obj" "$WORK/main.js" "$WORK"/*.vs "$WORK"/*.fs \
  "$WORK/version" "$WORK/manifest.sha256" \
  "root@$HOST:/data/apps/incoming/$RELEASE/"
$SSH "root@$HOST" "/usr/sbin/canvas-activate $RELEASE"

echo "Uploaded and activated $RELEASE on $HOST"
