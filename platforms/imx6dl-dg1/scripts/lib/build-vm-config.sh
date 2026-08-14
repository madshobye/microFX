#!/bin/sh

MICROFX_BUILDROOT_VERSION=2025.02.16
MICROFX_BUILDROOT_SHA256=15305e3d366eeaf4a5ecaf2ed42f685fd6af7fe5dbf1f62e1de5f46ee83225e2

microfx_resolve_build_vm() {
    platform_dir=${1:?platform directory required}
    vm_name_file="$platform_dir/private/build-vm"

    if [ -n "${VM_NAME:-}" ]; then
        :
    elif [ -r "$vm_name_file" ]; then
        IFS= read -r VM_NAME <"$vm_name_file"
        [ -n "$VM_NAME" ] || {
            echo "Empty Buildroot VM name in $vm_name_file" >&2
            return 1
        }
    else
        VM_NAME=microfx-build
    fi
    export VM_NAME
}

microfx_require_build_vm() {
    command -v limactl >/dev/null 2>&1 || {
        echo "Lima is missing; install it with: brew install lima" >&2
        return 1
    }
    if ! limactl list -q 2>/dev/null | grep -Fxq "$VM_NAME"; then
        echo "Build VM '$VM_NAME' does not exist." >&2
        echo "Create it with: ./scripts/setup-build-vm.sh" >&2
        return 1
    fi
    limactl start "$VM_NAME" >/dev/null
    # Lima guest user names do not always match the host. Resolve through the
    # login shell's HOME rather than constructing a /home path.
    limactl shell "$VM_NAME" -- sh -lc \
        "test -d \"\$HOME/buildroot-$MICROFX_BUILDROOT_VERSION\"" || {
        echo "Buildroot $MICROFX_BUILDROOT_VERSION is missing in '$VM_NAME'." >&2
        echo "Provision it with: ./scripts/setup-build-vm.sh" >&2
        return 1
    }
}
