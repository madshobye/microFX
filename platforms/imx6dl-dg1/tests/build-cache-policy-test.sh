#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
chooser="$ROOT/scripts/build.sh"
linux_build="$ROOT/scripts/build-linux.sh"
graphics_build="$ROOT/scripts/build-graphics.sh"
smoke="$ROOT/scripts/test-vm.sh"
upload="$ROOT/scripts/canvas-upload.sh"
setup="$ROOT/scripts/setup-build-vm.sh"
helper="$ROOT/scripts/lib/build-vm-config.sh"

sh -n "$chooser" "$linux_build" "$graphics_build" "$smoke" "$upload" "$setup" "$helper"
grep -q 'intentionally disabled because it was ambiguous' "$chooser"
grep -q 'PREVIOUS_CONFIG=' "$linux_build"
grep -q 'PREVIOUS_CONFIG_NORMALIZED=' "$linux_build"
grep -q 'CURRENT_CONFIG_NORMALIZED=' "$linux_build"
grep -q '@MICROFX_SOURCE@' "$linux_build"
grep -q 'cmp -s.*PREVIOUS_CONFIG_NORMALIZED.*CURRENT_CONFIG_NORMALIZED' "$linux_build"
grep -q -- '--clean' "$linux_build"
grep -q 'cache was preserved' "$linux_build"
grep -q 'cp.*PREVIOUS_CONFIG.*OUT/.config' "$linux_build"
grep -q '.microfx-build-state' "$linux_build"
grep -q 'BUILD_STATE_VERSION=' "$linux_build"
grep -q 'CACHE_BACKUP=' "$linux_build"
grep -q 'mv.*OUT.*CACHE_BACKUP' "$linux_build"
if grep -q 'O=.*OUT.*[[:space:]]clean[[:space:]"$]' "$linux_build"; then
  echo "Linux clean workflow must archive the cache, not delete it" >&2
  exit 1
fi
grep -q 'graphics build refused to touch the Linux cache' "$graphics_build"
grep -q 'raylib-drm-dirclean microfx-demo-dirclean' "$graphics_build"
if grep -q 'O=.*OUT.*[[:space:]]clean[[:space:]"$]' "$graphics_build"; then
  echo "Graphics workflow must never clean the Linux cache" >&2
  exit 1
fi
grep -q 'private/build-vm' "$helper"
grep -q 'MICROFX_BUILDROOT_VERSION=2025.02.16' "$helper"
grep -q 'MICROFX_BUILDROOT_SHA256=' "$helper"
grep -q 'setup-build-vm.sh' "$helper"
grep -q 'template:ubuntu' "$setup"
grep -q 'sha256sum -c' "$setup"
grep -q 'exec.*build-graphics.sh' "$smoke"
grep -q 'limactl copy --backend=scp' "$graphics_build"
grep -q -- '--exclude=platforms/imx6dl-dg1/private' "$graphics_build"
grep -q -- '--exclude=platforms/imx6dl-dg1/private' "$linux_build"
grep -q 'canvas_debug_ed25519.pub' "$linux_build"
if grep -q 'private/canvas_debug_ed25519 ' "$linux_build"; then
  echo "Build script must not stage the SSH private key" >&2
  exit 1
fi
grep -q 'scripts/build-graphics.sh' "$upload"
if grep -q 'microfx-demo-dirclean' "$upload"; then
  echo "Upload script must delegate authoritative source staging to build-graphics.sh" >&2
  exit 1
fi
if grep -q '/etc/init.d/S40canvas stop' "$upload"; then
  echo "Runtime upload must let canvas-activate own the single restart" >&2
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
