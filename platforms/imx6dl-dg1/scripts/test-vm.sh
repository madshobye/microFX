#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO_DIR=$(CDPATH= cd -- "$PLATFORM_DIR/../.." && pwd)
. "$PLATFORM_DIR/scripts/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"
microfx_require_build_vm

# Stage the authoritative repository instead of relying on a Lima host mount.
# This remains valid after a checkout is moved or the VM predates its path.
SOURCE_ARCHIVE=$(mktemp /tmp/microfx-test-source.XXXXXX)
trap 'rm -f "$SOURCE_ARCHIVE"' EXIT HUP INT TERM
COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 tar -C "$REPO_DIR" \
    --exclude=.git \
    --exclude='._*' \
    --exclude=platforms/imx6dl-dg1/artifacts \
    --exclude=platforms/imx6dl-dg1/private \
    -cf "$SOURCE_ARCHIVE" .
limactl copy --backend=scp "$SOURCE_ARCHIVE" "$VM_NAME:/tmp/microfx-test-source.tar"

limactl shell "$VM_NAME" -- sh -lc "
    set -eu
    BR=\"\$HOME/buildroot-${MICROFX_BUILDROOT_VERSION}\"
    OUT=\"\$HOME/microfx-imx6dl-output\"
    SRC=\"\$(mktemp -d \"\$HOME/microfx-test-src.XXXXXX\")\"
    trap 'rm -rf \"\$SRC\" /tmp/microfx-test-source.tar' EXIT
    tar -xf /tmp/microfx-test-source.tar -C \"\$SRC\"
    EXTERNAL=\"\$SRC/platforms/imx6dl-dg1/buildroot\"
    make -C \"\$BR\" BR2_EXTERNAL=\"\$EXTERNAL\" O=\"\$OUT\" imx6dl_dg1_defconfig
    make -C \"\$BR\" O=\"\$OUT\" microfx-demo-dirclean
    make -C \"\$BR\" O=\"\$OUT\" microfx-demo
    test -x \"\$OUT/build/microfx-demo-1.0.0/canvas-demo\"
    echo 'VM cross-build smoke test passed: microfx-demo'
"
