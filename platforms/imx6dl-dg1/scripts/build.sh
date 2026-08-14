#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
BUILDROOT_EXTERNAL="$PLATFORM_DIR/buildroot"
ARTIFACTS="$PLATFORM_DIR/artifacts"
BUILDROOT_VERSION=2025.02.16
BUILD_STATE_VERSION=2
VM_NAME_FILE="$PLATFORM_DIR/private/build-vm"

# Keep host-specific VM names out of tracked configuration. VM_NAME remains
# the highest-priority override; an ignored private/build-vm file makes the
# normal command work with an existing local Buildroot cache.
if [ -n "${VM_NAME:-}" ]; then
  :
elif [ -r "$VM_NAME_FILE" ]; then
  IFS= read -r VM_NAME <"$VM_NAME_FILE"
  [ -n "$VM_NAME" ] || {
    echo "Empty build VM name in $VM_NAME_FILE" >&2
    exit 1
  }
else
  VM_NAME=microfx-build
fi

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
  PREVIOUS_CONFIG=\"\$(mktemp)\"
  PREVIOUS_CONFIG_NORMALIZED=\"\$(mktemp)\"
  CURRENT_CONFIG_NORMALIZED=\"\$(mktemp)\"
  HAD_CONFIG=0
  if [ -r \"\$OUT/.config\" ]; then
    cp \"\$OUT/.config\" \"\$PREVIOUS_CONFIG\"
    HAD_CONFIG=1
  fi
  make -C \"\$BR\" BR2_EXTERNAL=\"\$EXTERNAL\" O=\"\$OUT\" imx6dl_dg1_defconfig
  # Buildroot does not remove files from packages disabled by a changed
  # configuration, and an existing library can retain its old TLS backend.
  # Clean only when configuration/cache policy changes; source builds remain
  # incremental in the common case.
  CLEAN_CONFIG=0
  if [ \"\$HAD_CONFIG\" = 1 ]; then
    # The unique temporary source root appears in BR2_EXTERNAL, BR2_DEFCONFIG,
    # generated menu comments, and every external file option. It changes on
    # every invocation but not the meaning of the configuration. Normalize the
    # complete staging prefix while preserving the rest of every path/value.
    sed -E 's#/home/[^/]+/microfx-src\.[^/\"]+#@MICROFX_SOURCE@#g' \"\$PREVIOUS_CONFIG\" >\"\$PREVIOUS_CONFIG_NORMALIZED\"
    sed -E 's#/home/[^/]+/microfx-src\.[^/\"]+#@MICROFX_SOURCE@#g' \"\$OUT/.config\" >\"\$CURRENT_CONFIG_NORMALIZED\"
    if ! cmp -s \"\$PREVIOUS_CONFIG_NORMALIZED\" \"\$CURRENT_CONFIG_NORMALIZED\"; then
      CLEAN_CONFIG=1
    fi
  fi
  if [ \"\$(cat \"\$OUT/.microfx-build-state\" 2>/dev/null || true)\" != \"${BUILD_STATE_VERSION}\" ]; then
    CLEAN_CONFIG=1
  fi
  rm -f \"\$PREVIOUS_CONFIG\" \"\$PREVIOUS_CONFIG_NORMALIZED\" \"\$CURRENT_CONFIG_NORMALIZED\"
  if [ \"\$CLEAN_CONFIG\" = 1 ]; then
    echo 'microFX Buildroot configuration changed; rebuilding target from clean state'
    make -C \"\$BR\" O=\"\$OUT\" clean
    make -C \"\$BR\" BR2_EXTERNAL=\"\$EXTERNAL\" O=\"\$OUT\" imx6dl_dg1_defconfig
  fi
  printf '%s\n' '${BUILD_STATE_VERSION}' >\"\$OUT/.microfx-build-state\"
  # Local-source packages are not content-hashed by Buildroot. Clear all three
  # and BusyBox so an image cannot silently retain older runtime components.
  make -C \"\$BR\" O=\"\$OUT\" busybox-dirclean raylib-drm-dirclean libpeer-microfx-dirclean microfx-demo-dirclean microfx-provision-dirclean microfx-peer-bridge-dirclean
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
