#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILDROOT_EXTERNAL="$PLATFORM_DIR/buildroot"
BUILDROOT_VERSION=2025.02.16
VM_NAME=${VM_NAME:-microfx-build}

# This is a fast cross-build smoke test. The application deliberately uses the
# DRM/GLES2 backend directly, so a desktop/X11 build would test a different
# renderer and cannot validate physical HDMI, Etnaviv or mode selection.
limactl shell "$VM_NAME" -- sh -lc "
  set -eu
  BR=\"\$HOME/buildroot-${BUILDROOT_VERSION}\"
  OUT=\"\$HOME/microfx-imx6dl-output\"
  make -C \"\$BR\" BR2_EXTERNAL='${BUILDROOT_EXTERNAL}' O=\"\$OUT\" imx6dl_dg1_defconfig
  make -C \"\$BR\" O=\"\$OUT\" microfx-demo-rebuild
  test -x \"\$OUT/build/microfx-demo-1.0.0/canvas-demo\"
  echo 'VM cross-build smoke test passed: microfx-demo'
"
