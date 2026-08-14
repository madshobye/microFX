#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

fail() {
  echo "prototype isolation test failed: $*" >&2
  exit 1
}

for path in \
  "$ROOT/scripts/build.sh" \
  "$ROOT/scripts/install-full-sd.sh" \
  "$ROOT/buildroot/configs/imx6dl_dg1_defconfig" \
  "$ROOT/buildroot/board/imx6dl-dg1/post-build.sh"; do
  if grep -Eq 'bootloader/|layout\.json|validate-layout|genimage-prototype|imx6dl-dg1-ddr|engine/experimental|COMPOSITOR\.md' "$path"; then
    fail "experimental boot/compositor mapping leaked into ${path#$ROOT/}"
  fi
done

grep -q 'isolated prototype only' "$ROOT/../../README.md"
grep -q 'not consumed by the firmware build' "$ROOT/../../README.md"
grep -q 'authoritative general-development setup' "$ROOT/README.md"
grep -q 'isolated feasibility prototype' "$ROOT/bootloader/README.md"
grep -q 'not an enabled render path' "$ROOT/COMPOSITOR.md"

python3 "$ROOT/tests/boot-layout-test.py"

BOOT="$ROOT/bootloader"
DEFCONFIG="$BOOT/u-boot/configs/microfx_imx6dl_dg1_defconfig"
HEADER="$BOOT/u-boot/include/configs/imx6dl_dg1.h"

grep -q '^CONFIG_ENV_IS_NOWHERE=y$' "$DEFCONFIG"
grep -q '^# CONFIG_EFI_LOADER is not set$' "$DEFCONFIG"
grep -q '^# CONFIG_TOOLS_MKEFICAPSULE is not set$' "$DEFCONFIG"
grep -q 'microfx-root-a' "$HEADER"
grep -q 'microfx-root-b' "$HEADER"
grep -q 'run boot_a || run boot_b' "$DEFCONFIG"
grep -q 'run boot_b || run boot_a' "$DEFCONFIG"

# Exercise the overlay against a minimal representation of its pinned upstream
# insertion boundary. Applying it twice must not duplicate Kconfig entries.
FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/microfx-u-boot-overlay.XXXXXX")
trap 'rm -rf "$FIXTURE"' EXIT HUP INT TERM
mkdir -p "$FIXTURE/arch/arm/mach-imx/mx6"
cat >"$FIXTURE/Makefile" <<'EOF'
VERSION = 2025
PATCHLEVEL = 01
EOF
cat >"$FIXTURE/arch/arm/mach-imx/mx6/Kconfig" <<'EOF'
config TARGET_MX6Q_ENGICAM
	bool "fixture"
source "board/boundary/nitrogen6x/Kconfig"
EOF
python3 "$BOOT/apply-u-boot-overlay.py" "$FIXTURE" >/dev/null
python3 "$BOOT/apply-u-boot-overlay.py" "$FIXTURE" >/dev/null
[ "$(grep -c '^config TARGET_MICROFX_IMX6DL_DG1$' "$FIXTURE/arch/arm/mach-imx/mx6/Kconfig")" -eq 1 ]
[ "$(grep -c '^source "board/microfx/imx6dl_dg1/Kconfig"$' "$FIXTURE/arch/arm/mach-imx/mx6/Kconfig")" -eq 1 ]
cmp "$BOOT/imx6dl-dg1-ddr.cfg" \
  "$FIXTURE/board/microfx/imx6dl_dg1/imximage.cfg"

# The explicit prototype builder is allowed to reference the overlay; the
# authoritative development build and installers are not.
grep -q 'This script neither changes the normal firmware build nor writes an SD card' \
  "$BOOT/build-u-boot-prototype.sh"

echo "experimental boot and compositor isolation tests passed"
