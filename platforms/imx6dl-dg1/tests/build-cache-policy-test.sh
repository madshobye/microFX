#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$ROOT/scripts/build.sh"

sh -n "$build"
grep -q 'PREVIOUS_CONFIG=' "$build"
grep -q 'PREVIOUS_CONFIG_NORMALIZED=' "$build"
grep -q 'CURRENT_CONFIG_NORMALIZED=' "$build"
grep -q 'microfx-src\\\.\[\^/' "$build"
grep -q '@MICROFX_SOURCE@' "$build"
grep -q 'cmp -s.*PREVIOUS_CONFIG_NORMALIZED.*CURRENT_CONFIG_NORMALIZED' "$build"
grep -q 'make -C.*BR.*O=.*OUT.*clean' "$build"
grep -q '.microfx-build-state' "$build"
grep -q 'BUILD_STATE_VERSION=' "$build"
grep -q 'VM_NAME_FILE=.*private/build-vm' "$ROOT/scripts/canvas-upload.sh"
grep -q 'limactl copy --backend=scp' "$ROOT/scripts/canvas-upload.sh"

echo "Buildroot cache invalidation policy tests passed"
