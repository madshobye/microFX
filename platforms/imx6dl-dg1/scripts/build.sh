#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
BUILDROOT_EXTERNAL="$PLATFORM_DIR/buildroot"
ARTIFACTS="$PLATFORM_DIR/artifacts"
BUILDROOT_VERSION=2025.02.16
VM_NAME=${VM_NAME:-microfx-build}

# The VM may predate a repository move and therefore have stale host mounts.
# Stage the authoritative working tree explicitly for every build.
SOURCE_ARCHIVE=$(mktemp /tmp/microfx-build-source.XXXXXX.tar)
trap 'rm -f "$SOURCE_ARCHIVE"' EXIT
COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 tar -C "$REPO_DIR" \
  --exclude=.git \
  --exclude='._*' \
  --exclude=platforms/imx6dl-dg1/artifacts \
  -cf "$SOURCE_ARCHIVE" .
limactl copy --backend=scp "$SOURCE_ARCHIVE" "$VM_NAME:/tmp/microfx-build-source.tar"

limactl shell "$VM_NAME" -- sh -lc "
  set -eu
  BR=\"\$HOME/buildroot-${BUILDROOT_VERSION}\"
  OUT=\"\$HOME/microfx-imx6dl-output\"
  SRC=\"\$(mktemp -d \"\$HOME/microfx-src.XXXXXX\")\"
  tar -xf /tmp/microfx-build-source.tar -C \"\$SRC\"
  EXTERNAL=\"\$SRC/platforms/imx6dl-dg1/buildroot\"
  make -C \"\$BR\" BR2_EXTERNAL=\"\$EXTERNAL\" O=\"\$OUT\" imx6dl_dg1_defconfig
  # Local-source packages are not content-hashed by Buildroot. Clear all three
  # and BusyBox so an image cannot silently retain older runtime components.
  make -C \"\$BR\" O=\"\$OUT\" \
    busybox-dirclean \
    raylib-drm-dirclean \
    libpeer-microfx-dirclean \
    microfx-demo-dirclean \
    microfx-provision-dirclean \
    microfx-peer-bridge-dirclean
  make -C \"\$BR\" O=\"\$OUT\" -j\"\$(nproc)\"
"

GUEST_HOME=$(limactl shell "$VM_NAME" -- printenv HOME)
mkdir -p "$ARTIFACTS"
limactl copy --backend=scp \
  "$VM_NAME:$GUEST_HOME/microfx-imx6dl-output/images/microfx-imx6dl-dg1.rootfs.gz" \
  "$VM_NAME:$GUEST_HOME/microfx-imx6dl-output/images/microfx-imx6dl-dg1.rootfs.md5" \
  "$ARTIFACTS/"
gzip -dc "$ARTIFACTS/microfx-imx6dl-dg1.rootfs.gz" >"$ARTIFACTS/microfx-imx6dl-dg1.rootfs"
(
  cd "$ARTIFACTS"
  shasum -a 256 microfx-imx6dl-dg1.rootfs microfx-imx6dl-dg1.rootfs.gz >SHA256SUMS
)
