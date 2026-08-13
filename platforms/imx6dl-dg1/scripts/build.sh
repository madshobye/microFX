#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILDROOT_EXTERNAL="$PLATFORM_DIR/buildroot"
ARTIFACTS="$PLATFORM_DIR/artifacts"
BUILDROOT_VERSION=2025.02.16
VM_NAME=${VM_NAME:-microfx-build}

limactl shell "$VM_NAME" -- sh -lc "
  set -eu
  BR=\"\$HOME/buildroot-${BUILDROOT_VERSION}\"
  OUT=\"\$HOME/microfx-imx6dl-output\"
  make -C \"\$BR\" BR2_EXTERNAL='${BUILDROOT_EXTERNAL}' O=\"\$OUT\" imx6dl_dg1_defconfig
  # Local-source packages are not content-hashed by Buildroot. Clear all three
  # so an image can never silently retain an older app or service binary.
  make -C \"\$BR\" O=\"\$OUT\" \
    microfx-demo-dirclean \
    microfx-provision-dirclean \
    microfx-peer-bridge-dirclean
  make -C \"\$BR\" O=\"\$OUT\" -j\"\$(nproc)\"
  mkdir -p '${ARTIFACTS}'
  cp --sparse=always \"\$OUT/images/microfx-imx6dl-dg1.rootfs\" '${ARTIFACTS}/'
  cp \"\$OUT/images/microfx-imx6dl-dg1.rootfs.gz\" \"\$OUT/images/microfx-imx6dl-dg1.rootfs.md5\" '${ARTIFACTS}/'
  cd '${ARTIFACTS}'
  sha256sum microfx-imx6dl-dg1.rootfs microfx-imx6dl-dg1.rootfs.gz > SHA256SUMS
"
