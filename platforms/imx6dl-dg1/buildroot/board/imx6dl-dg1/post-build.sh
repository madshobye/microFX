#!/bin/sh
set -eu

rm -f "${TARGET_DIR}/etc/init.d/S99canvas"
rm -f "${TARGET_DIR}/etc/init.d/S50dropbear"
find "${TARGET_DIR}" -name '._*' -delete

PRIVATE_DIR="${BR2_EXTERNAL_IMX6DL_DG1_PATH}/../private"
PRIVATE_WIFI="${PRIVATE_DIR}/wpa_supplicant.conf"
if [ -f "${PRIVATE_WIFI}" ]; then
    install -D -m 0600 "${PRIVATE_WIFI}" "${TARGET_DIR}/etc/wpa_supplicant.conf"
fi

DEBUG_PUBLIC_KEY="${PRIVATE_DIR}/canvas_debug_ed25519.pub"
if [ -f "${DEBUG_PUBLIC_KEY}" ]; then
    install -d -m 0700 "${TARGET_DIR}/root/.ssh"
    install -m 0600 "${DEBUG_PUBLIC_KEY}" "${TARGET_DIR}/root/.ssh/authorized_keys"
fi

PRIVATE_RUNTIME_CONFIG="${PRIVATE_DIR}/canvas-debug.conf"
if [ -f "${PRIVATE_RUNTIME_CONFIG}" ]; then
    install -m 0644 "${PRIVATE_RUNTIME_CONFIG}" "${TARGET_DIR}/etc/canvas.conf"
fi

mkdir -p "${TARGET_DIR}/boot"
find "${TARGET_DIR}/boot" -mindepth 1 -maxdepth 1 -type f \
    ! -name uEnv.txt \
    ! -name microfx-imx6dl-dg1.img \
    ! -name microfx-imx6dl-dg1.dtb \
    -delete
cp "${BINARIES_DIR}/zImage" "${TARGET_DIR}/boot/microfx-imx6dl-dg1.img"
cp "${BINARIES_DIR}/imx6dl-microfx-dg1.dtb" "${TARGET_DIR}/boot/microfx-imx6dl-dg1.dtb"
