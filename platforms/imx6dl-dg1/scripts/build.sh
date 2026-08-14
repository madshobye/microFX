#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
BUILDROOT_EXTERNAL="$PLATFORM_DIR/buildroot"
ARTIFACTS="$PLATFORM_DIR/artifacts"
BUILD_STATE_VERSION=2
. "$PLATFORM_DIR/scripts/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"
microfx_require_build_vm
BUILDROOT_VERSION=$MICROFX_BUILDROOT_VERSION

# The VM may predate a repository move and therefore have stale host mounts.
# Stage the authoritative working tree explicitly for every build.
SOURCE_ARCHIVE=$(mktemp /tmp/microfx-build-source.XXXXXX)
trap 'rm -f "$SOURCE_ARCHIVE"' EXIT
COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 tar -C "$REPO_DIR" \
  --exclude=.git \
  --exclude='._*' \
  --exclude=platforms/imx6dl-dg1/artifacts \
  --exclude=platforms/imx6dl-dg1/private \
  -cf "$SOURCE_ARCHIVE" .
# Only these target inputs may leave the ignored private directory. Never copy
# the SSH private key or the host-specific VM selector into the build guest.
for private_input in \
  platforms/imx6dl-dg1/private/wpa_supplicant.conf \
  platforms/imx6dl-dg1/private/canvas_debug_ed25519.pub \
  platforms/imx6dl-dg1/private/canvas-debug.conf; do
  if [ -f "$REPO_DIR/$private_input" ]; then
    COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 \
      tar -C "$REPO_DIR" -rf "$SOURCE_ARCHIVE" "$private_input"
  fi
done
limactl copy --backend=scp "$SOURCE_ARCHIVE" "$VM_NAME:/tmp/microfx-build-source.tar"

limactl shell "$VM_NAME" -- sh -lc "
  set -eu
  BR=\"\$HOME/buildroot-${BUILDROOT_VERSION}\"
  OUT=\"\$HOME/microfx-imx6dl-output\"
  SRC=\"\$(mktemp -d \"\$HOME/microfx-src.XXXXXX\")\"
  trap 'rm -rf \"\$SRC\" /tmp/microfx-build-source.tar' EXIT
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
