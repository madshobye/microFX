#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$PLATFORM_DIR/scripts/lib/build-vm-config.sh"
microfx_resolve_build_vm "$PLATFORM_DIR"

command -v limactl >/dev/null 2>&1 || {
    echo "Lima is missing; install host tools with:" >&2
    echo "  brew install lima e2fsprogs dtc" >&2
    exit 1
}

if limactl list -q 2>/dev/null | grep -Fxq "$VM_NAME"; then
    echo "Starting existing Lima VM: $VM_NAME"
    limactl start "$VM_NAME" >/dev/null
else
    echo "Creating mount-free Lima VM: $VM_NAME"
    limactl start --yes --name="$VM_NAME" --cpus=6 --memory=8 --disk=60 \
        --mount-none template:ubuntu
fi

limactl shell "$VM_NAME" -- sh -lc "
    set -eu
    sudo apt-get update
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
        bc build-essential bzip2 cpio curl file git gzip libncurses-dev \
        locales patch perl python3 rsync sed tar unzip wget xz-utils

    archive=\"\$HOME/buildroot-${MICROFX_BUILDROOT_VERSION}.tar.xz\"
    source_dir=\"\$HOME/buildroot-${MICROFX_BUILDROOT_VERSION}\"
    if [ ! -d \"\$source_dir\" ]; then
        curl -fL \
            'https://buildroot.org/downloads/buildroot-${MICROFX_BUILDROOT_VERSION}.tar.xz' \
            -o \"\$archive\"
        printf '%s  %s\n' '${MICROFX_BUILDROOT_SHA256}' \"\$archive\" | sha256sum -c -
        tar -xJf \"\$archive\" -C \"\$HOME\"
    fi
    test -f \"\$source_dir/Makefile\"
"

mkdir -p "$PLATFORM_DIR/private"
printf '%s\n' "$VM_NAME" >"$PLATFORM_DIR/private/build-vm"

echo "Build VM ready: $VM_NAME"
echo "Next: ./scripts/test-vm.sh"
