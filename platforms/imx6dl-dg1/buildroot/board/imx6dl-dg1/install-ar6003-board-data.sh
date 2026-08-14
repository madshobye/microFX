#!/bin/sh
set -eu

TARGET_DIR=${1:?usage: install-ar6003-board-data.sh TARGET_DIR}
AR6003_DIR="${TARGET_DIR}/lib/firmware/ath6k/AR6003/hw2.1.1"
BOARD_DATA="${AR6003_DIR}/bdata.SD31.bin"
EXPECTED_BYTES=1792

if [ ! -f "${BOARD_DATA}" ]; then
    echo "missing required upstream AR6003 board data: ${BOARD_DATA}" >&2
    exit 1
fi

actual_bytes=$(wc -c <"${BOARD_DATA}" | tr -d '[:space:]')
if [ "${actual_bytes}" != "${EXPECTED_BYTES}" ]; then
    echo "invalid AR6003 board data size: expected ${EXPECTED_BYTES}, got ${actual_bytes}" >&2
    exit 1
fi

# ath6kl requests bdata.bin first. Make the kernel-declared upstream SD31
# default explicit so both radios use a deterministic, redistributable file.
ln -snf bdata.SD31.bin "${AR6003_DIR}/bdata.bin"

if [ "$(readlink "${AR6003_DIR}/bdata.bin")" != "bdata.SD31.bin" ]; then
    echo "failed to install deterministic AR6003 board-data alias" >&2
    exit 1
fi
