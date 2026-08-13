#!/bin/sh
set -eu

ROOTFS="${BINARIES_DIR}/rootfs.ext4"
OUT="${BINARIES_DIR}/microfx-imx6dl-dg1.rootfs"

cp "${ROOTFS}" "${OUT}"
gzip -n -f -9 -c "${OUT}" > "${OUT}.gz"
md5sum "${OUT}.gz" | awk '{print $1}' > "${OUT}.md5"
(
	cd "${BINARIES_DIR}"
	sha256sum "$(basename "${OUT}")" "$(basename "${OUT}.gz")" > SHA256SUMS
)
