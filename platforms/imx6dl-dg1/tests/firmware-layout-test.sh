#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
POST_BUILD="$ROOT/buildroot/board/imx6dl-dg1/post-build.sh"
BOARD_DATA_INSTALLER="$ROOT/buildroot/board/imx6dl-dg1/install-ar6003-board-data.sh"
TARGET_VERIFIER="$ROOT/buildroot/board/imx6dl-dg1/verify-target-firmware.sh"
PROVISION_PACKAGE="$ROOT/buildroot/package/microfx-provision/microfx-provision.mk"
RELEASE="$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/etc/microfx-release"
ACTIVE_ROOT_UPDATER="$ROOT/scripts/install-active-root-ssh.sh"
ACTIVE_ROOT_TARGET="$ROOT/scripts/lib/active-root-update-target.sh"

grep -q 'install-ar6003-board-data.sh' "$POST_BUILD"
grep -q 'verify-target-firmware.sh' "$POST_BUILD"
test -x "$BOARD_DATA_INSTALLER"
test -x "$TARGET_VERIFIER"
grep -q 'web/editor/.*www/studio' "$PROVISION_PACKAGE"
grep -q 'web/editor/vendor/.*www/studio/vendor' "$PROVISION_PACKAGE"
grep -q 'interaction-check.js' "$PROVISION_PACKAGE"
grep -q 'BR2_PACKAGE_LINUX_FIRMWARE_ATHEROS_6003=y' "$ROOT/buildroot/configs/imx6dl_dg1_defconfig"
grep -q '^MICROFX_IMAGE_SCHEMA=1$' "$RELEASE"
grep -q '^MICROFX_PLATFORM=imx6dl-dg1$' "$RELEASE"
grep -q '^MICROFX_BOOT_MODEL=existing-ab$' "$RELEASE"
test -x "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-benchmark-override"
test -x "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-benchmark-capture"
test -x "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/etc/init.d/S39dropbear-debug"
test -x "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/etc/init.d/S39recovery-client"
test -x "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-recovery-guardian"
test -x "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-recovery-client"
test -x "$ACTIVE_ROOT_UPDATER"
test -x "$ACTIVE_ROOT_TARGET"
test ! -e "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/etc/init.d/S42dropbear-debug"
grep -q 'root-update-backups' "$ACTIVE_ROOT_TARGET"
grep -q "default .* dev wlan1" "$ACTIVE_ROOT_TARGET"
grep -q '\[d\]ropbear' "$ACTIVE_ROOT_TARGET"
if grep -E 'S41wifi|S39dropbear-debug|wifi-connect|wpa_supplicant' "$ACTIVE_ROOT_TARGET" >/dev/null; then
  echo "active-root updater is allowed to change the client Wi-Fi or SSH service" >&2
  exit 1
fi
if grep -E 'hostapd[[:space:]].*-B|dnsmasq[[:space:]]+--|set type __ap|power_save|radio-policy|wifi-policy' \
     "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/usr/sbin/microfx-recovery-client" >/dev/null; then
  echo "stored-network recovery contains non-baseline radio behavior" >&2
  exit 1
fi

# Renaming two concurrently probing AR6003 interfaces from udev can collide and
# keep udevadm settle from completing, which blocks the entire SysV boot.  The
# platform scripts retain configurable interface roles, but boot must never use
# udev NAME assignments for these radios.
if grep -R -E 'SUBSYSTEM=="net".*KERNELS=="mmc[12]:0001:1".*NAME="wlan[01]"' \
     "$ROOT/buildroot/board/imx6dl-dg1/rootfs-overlay/etc/udev" >/dev/null 2>&1; then
  echo "unsafe AR6003 boot-time interface rename rule found" >&2
  exit 1
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/microfx-board-data.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
FIRMWARE_DIR="$WORK/valid/lib/firmware/ath6k/AR6003/hw2.1.1"
mkdir -p "$FIRMWARE_DIR"
dd if=/dev/zero of="$FIRMWARE_DIR/bdata.SD31.bin" bs=1792 count=1 2>/dev/null
"$BOARD_DATA_INSTALLER" "$WORK/valid"
test "$(readlink "$FIRMWARE_DIR/bdata.bin")" = bdata.SD31.bin
test "$(wc -c <"$FIRMWARE_DIR/bdata.bin" | tr -d '[:space:]')" = 1792
printf firmware >"$FIRMWARE_DIR/fw-2.bin"
printf firmware >"$FIRMWARE_DIR/fw-3.bin"
mkdir -p "$WORK/valid/lib/firmware"
printf database >"$WORK/valid/lib/firmware/regulatory.db"
printf signature >"$WORK/valid/lib/firmware/regulatory.db.p7s"
"$TARGET_VERIFIER" "$WORK/valid"

rm "$WORK/valid/lib/firmware/regulatory.db"
if "$TARGET_VERIFIER" "$WORK/valid" >/dev/null 2>&1; then
  echo "target verifier accepted a missing regulatory database" >&2
  exit 1
fi
printf database >"$WORK/valid/lib/firmware/regulatory.db"
mkdir -p "$WORK/valid/etc/modprobe.d"
printf '%s\n' 'options ath6kl_core mac=00:03:7f:00:00:01' >"$WORK/valid/etc/modprobe.d/ath6kl.conf"
if "$TARGET_VERIFIER" "$WORK/valid" >/dev/null 2>&1; then
  echo "target verifier accepted the obsolete ath6kl mac parameter" >&2
  exit 1
fi

if "$BOARD_DATA_INSTALLER" "$WORK/missing" >/dev/null 2>&1; then
  echo "board-data installer accepted a missing firmware file" >&2
  exit 1
fi

BAD_DIR="$WORK/bad/lib/firmware/ath6k/AR6003/hw2.1.1"
mkdir -p "$BAD_DIR"
dd if=/dev/zero of="$BAD_DIR/bdata.SD31.bin" bs=1791 count=1 2>/dev/null
if "$BOARD_DATA_INSTALLER" "$WORK/bad" >/dev/null 2>&1; then
  echo "board-data installer accepted an invalid firmware file" >&2
  exit 1
fi

# Experimental boot and compositor mappings must never become implicit inputs
# to the normal development image. Enabling either requires an explicit,
# separately reviewed hardware-test workflow.
if grep -R -E 'bootloader/(layout\.json|validate-layout|genimage-prototype|imx6dl-dg1-ddr)|COMPOSITOR\.md' \
     "$ROOT/buildroot" "$ROOT/scripts" >/dev/null 2>&1; then
  echo "isolated boot/compositor prototype leaked into the development build" >&2
  exit 1
fi

echo "firmware layout policy tests passed"
