#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
BUILDROOT_EXTERNAL="$PLATFORM_DIR/buildroot"
ARTIFACTS="$PLATFORM_DIR/artifacts"
BUILD_STATE_VERSION=2

CLEAN_BUILD=0
case "${1:-}" in
  "") ;;
  --clean) CLEAN_BUILD=1 ;;
  *) echo "Usage: $0 [--clean]" >&2; exit 2 ;;
esac
. "$PLATFORM_DIR/scripts/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"
microfx_require_build_vm
BUILDROOT_VERSION=$MICROFX_BUILDROOT_VERSION

# This is deliberately the complete Linux/root-filesystem workflow. For normal
# renderer, engine, shader, and JavaScript work use build-graphics.sh instead.
SOURCE_ARCHIVE=$(mktemp /tmp/microfx-linux-build-source.XXXXXX)
trap 'rm -f "$SOURCE_ARCHIVE"' EXIT
COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 tar -C "$REPO_DIR" \
  --exclude=.git \
  --exclude='._*' \
  --exclude=platforms/imx6dl-dg1/artifacts \
  --exclude=platforms/imx6dl-dg1/private \
  -cf "$SOURCE_ARCHIVE" .
for private_input in \
  platforms/imx6dl-dg1/private/wpa_supplicant.conf \
  platforms/imx6dl-dg1/private/canvas_debug_ed25519.pub \
  platforms/imx6dl-dg1/private/canvas-debug.conf; do
  if [ -f "$REPO_DIR/$private_input" ]; then
    COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 \
      tar -C "$REPO_DIR" -rf "$SOURCE_ARCHIVE" "$private_input"
  fi
done
limactl copy --backend=scp "$SOURCE_ARCHIVE" "$VM_NAME:/tmp/microfx-linux-build-source.tar"

limactl shell "$VM_NAME" -- sh -lc "
  set -eu
  BR=\"\$HOME/buildroot-${BUILDROOT_VERSION}\"
  OUT=\"\$HOME/microfx-imx6dl-output\"
  SRC=\"\$(mktemp -d \"\$HOME/microfx-linux-src.XXXXXX\")\"
  trap 'rm -rf \"\$SRC\" /tmp/microfx-linux-build-source.tar' EXIT
  tar -xf /tmp/microfx-linux-build-source.tar -C \"\$SRC\"
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
  CLEAN_CONFIG=0
  if [ \"\$HAD_CONFIG\" = 1 ]; then
    sed -E 's#/home/[^/]+/microfx-(linux|graphics|studio)-src\.[^/\"]+#@MICROFX_SOURCE@#g; s#/home/[^/]+/microfx-(src|test-src)\.[^/\"]+#@MICROFX_SOURCE@#g' \"\$PREVIOUS_CONFIG\" >\"\$PREVIOUS_CONFIG_NORMALIZED\"
    sed -E 's#/home/[^/]+/microfx-(linux|graphics|studio)-src\.[^/\"]+#@MICROFX_SOURCE@#g; s#/home/[^/]+/microfx-(src|test-src)\.[^/\"]+#@MICROFX_SOURCE@#g' \"\$OUT/.config\" >\"\$CURRENT_CONFIG_NORMALIZED\"
    if ! cmp -s \"\$PREVIOUS_CONFIG_NORMALIZED\" \"\$CURRENT_CONFIG_NORMALIZED\"; then
      CLEAN_CONFIG=1
    fi
  fi
  if [ \"\$HAD_CONFIG\" = 1 ] && [ \"\$(cat \"\$OUT/.microfx-build-state\" 2>/dev/null || true)\" != \"${BUILD_STATE_VERSION}\" ]; then
    CLEAN_CONFIG=1
  fi
  if [ \"\$CLEAN_CONFIG\" = 1 ] && [ \"${CLEAN_BUILD}\" != 1 ]; then
    cp \"\$PREVIOUS_CONFIG\" \"\$OUT/.config\"
    rm -f \"\$PREVIOUS_CONFIG\" \"\$PREVIOUS_CONFIG_NORMALIZED\" \"\$CURRENT_CONFIG_NORMALIZED\"
    echo 'microFX Linux configuration/cache policy changed.' >&2
    echo 'The cache was preserved. Review the change, then run build-linux.sh --clean explicitly.' >&2
    exit 2
  fi
  # defconfig was used only to compare policy. Preserve the exact prior config
  # inside the archived cache when a fresh build was explicitly approved.
  if [ \"\$CLEAN_CONFIG\" = 1 ] && [ \"${CLEAN_BUILD}\" = 1 ] && [ \"\$HAD_CONFIG\" = 1 ]; then
    cp \"\$PREVIOUS_CONFIG\" \"\$OUT/.config\"
  fi
  rm -f \"\$PREVIOUS_CONFIG\" \"\$PREVIOUS_CONFIG_NORMALIZED\" \"\$CURRENT_CONFIG_NORMALIZED\"
  if [ \"\$CLEAN_CONFIG\" = 1 ] || [ \"${CLEAN_BUILD}\" = 1 ]; then
    CACHE_BACKUP=\"\${OUT}.cache-\$(date -u +%Y%m%d-%H%M%S)\"
    [ ! -e \"\$CACHE_BACKUP\" ] || {
      echo \"Refusing to overwrite existing cache backup: \$CACHE_BACKUP\" >&2
      exit 2
    }
    if [ -e \"\$OUT\" ]; then
      echo \"Preserving previous Linux cache as \$CACHE_BACKUP\"
      mv \"\$OUT\" \"\$CACHE_BACKUP\"
    fi
    make -C \"\$BR\" BR2_EXTERNAL=\"\$EXTERNAL\" O=\"\$OUT\" imx6dl_dg1_defconfig
  fi
  printf '%s\n' '${BUILD_STATE_VERSION}' >\"\$OUT/.microfx-build-state\"
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
