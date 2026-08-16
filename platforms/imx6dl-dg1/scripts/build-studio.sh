#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
BUILD_STATE_VERSION=2
. "$PLATFORM_DIR/scripts/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"
microfx_require_build_vm

# Fast Studio workflow: peer bridge plus static web editor. It preserves the
# configured Linux image and never invokes a Buildroot clean.
SOURCE_ARCHIVE=$(mktemp /tmp/microfx-studio-source.XXXXXX)
trap 'rm -f "$SOURCE_ARCHIVE"' EXIT HUP INT TERM
COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 tar -C "$REPO_DIR" \
  --exclude=.git \
  --exclude='._*' \
  --exclude=platforms/imx6dl-dg1/artifacts \
  --exclude=platforms/imx6dl-dg1/private \
  -cf "$SOURCE_ARCHIVE" .
limactl copy --backend=scp "$SOURCE_ARCHIVE" "$VM_NAME:/tmp/microfx-studio-source.tar"

limactl shell "$VM_NAME" -- sh -lc "
  set -eu
  BR=\"\$HOME/buildroot-${MICROFX_BUILDROOT_VERSION}\"
  OUT=\"\$HOME/microfx-imx6dl-output\"
  [ -r \"\$OUT/.config\" ] || {
    echo 'No configured Linux build cache. Run build-linux.sh once.' >&2
    exit 2
  }
  [ \"\$(cat \"\$OUT/.microfx-build-state\" 2>/dev/null || true)\" = \"${BUILD_STATE_VERSION}\" ] || {
    echo 'Linux build-cache policy changed. Run build-linux.sh; use --clean only when it requests it.' >&2
    exit 2
  }
  SRC=\"\$(mktemp -d \"\$HOME/microfx-studio-src.XXXXXX\")\"
  trap 'rm -rf \"\$SRC\" /tmp/microfx-studio-source.tar' EXIT
  tar -xf /tmp/microfx-studio-source.tar -C \"\$SRC\"
  EXTERNAL=\"\$SRC/platforms/imx6dl-dg1/buildroot\"
  PREVIOUS_CONFIG=\"\$(mktemp)\"
  PREVIOUS_CONFIG_NORMALIZED=\"\$(mktemp)\"
  CURRENT_CONFIG_NORMALIZED=\"\$(mktemp)\"
  cp \"\$OUT/.config\" \"\$PREVIOUS_CONFIG\"
  make -C \"\$BR\" BR2_EXTERNAL=\"\$EXTERNAL\" O=\"\$OUT\" imx6dl_dg1_defconfig
  sed -E 's#/home/[^/]+/microfx-(linux|graphics|studio)-src\.[^/\"]+#@MICROFX_SOURCE@#g; s#/home/[^/]+/microfx-(src|test-src)\.[^/\"]+#@MICROFX_SOURCE@#g' \"\$PREVIOUS_CONFIG\" >\"\$PREVIOUS_CONFIG_NORMALIZED\"
  sed -E 's#/home/[^/]+/microfx-(linux|graphics|studio)-src\.[^/\"]+#@MICROFX_SOURCE@#g; s#/home/[^/]+/microfx-(src|test-src)\.[^/\"]+#@MICROFX_SOURCE@#g' \"\$OUT/.config\" >\"\$CURRENT_CONFIG_NORMALIZED\"
  if ! cmp -s \"\$PREVIOUS_CONFIG_NORMALIZED\" \"\$CURRENT_CONFIG_NORMALIZED\"; then
    cp \"\$PREVIOUS_CONFIG\" \"\$OUT/.config\"
    echo 'Buildroot configuration changed; Studio build refused to touch the Linux cache.' >&2
    echo 'Run build-linux.sh and use --clean only if that command explicitly requests it.' >&2
    exit 2
  fi
  rm -f \"\$PREVIOUS_CONFIG\" \"\$PREVIOUS_CONFIG_NORMALIZED\" \"\$CURRENT_CONFIG_NORMALIZED\"
  make -C \"\$BR\" O=\"\$OUT\" microfx-peer-bridge-dirclean
  make -C \"\$BR\" O=\"\$OUT\" -j\"\$(nproc)\" microfx-peer-bridge
  test -x \"\$OUT/target/usr/bin/microfx-peer-bridge\"
  test -s \"\$OUT/target/www/studio/app.js\"
  echo 'Studio/peer-bridge cross-build passed; Linux image and cache were preserved.'
"
