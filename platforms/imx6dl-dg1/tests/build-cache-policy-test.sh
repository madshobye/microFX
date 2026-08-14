#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$ROOT/scripts/build.sh"
smoke="$ROOT/scripts/test-vm.sh"
upload="$ROOT/scripts/canvas-upload.sh"
setup="$ROOT/scripts/setup-build-vm.sh"
helper="$ROOT/scripts/lib/build-vm-config.sh"

sh -n "$build" "$smoke" "$upload" "$setup" "$helper"
grep -q 'PREVIOUS_CONFIG=' "$build"
grep -q 'PREVIOUS_CONFIG_NORMALIZED=' "$build"
grep -q 'CURRENT_CONFIG_NORMALIZED=' "$build"
grep -q 'microfx-src\\\.\[\^/' "$build"
grep -q '@MICROFX_SOURCE@' "$build"
grep -q 'cmp -s.*PREVIOUS_CONFIG_NORMALIZED.*CURRENT_CONFIG_NORMALIZED' "$build"
grep -q 'make -C.*BR.*O=.*OUT.*clean' "$build"
grep -q '.microfx-build-state' "$build"
grep -q 'BUILD_STATE_VERSION=' "$build"
grep -q 'private/build-vm' "$helper"
grep -q 'MICROFX_BUILDROOT_VERSION=2025.02.16' "$helper"
grep -q 'MICROFX_BUILDROOT_SHA256=' "$helper"
grep -q 'setup-build-vm.sh' "$helper"
grep -q 'template:ubuntu' "$setup"
grep -q 'sha256sum -c' "$setup"
grep -q 'limactl copy --backend=scp' "$smoke"
grep -q -- '--exclude=platforms/imx6dl-dg1/private' "$smoke"
grep -q -- '--exclude=platforms/imx6dl-dg1/private' "$build"
grep -q 'canvas_debug_ed25519.pub' "$build"
if grep -q 'private/canvas_debug_ed25519 ' "$build"; then
  echo "Build script must not stage the SSH private key" >&2
  exit 1
fi
grep -q 'scripts/test-vm.sh' "$upload"
if grep -q 'microfx-demo-dirclean' "$upload"; then
  echo "Upload script must delegate authoritative source staging to test-vm.sh" >&2
  exit 1
fi

for removed in install-development-sd.sh install-data-recovery-sd.sh \
  install-network-recovery-sd.sh install-ssh-fix-sd.sh; do
  [ ! -e "$ROOT/scripts/$removed" ] || {
    echo "Obsolete ignored-payload installer remains: $removed" >&2
    exit 1
  }
done

grep -q 'The generated `.rootfs` is not a whole-card image' "$ROOT/../../HANDOVER.md"
grep -q 'install-full-sd.sh' "$ROOT/../../HANDOVER.md"

echo "Buildroot cache invalidation policy tests passed"
