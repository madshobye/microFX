#!/bin/sh

# Derive a stable, locally administered AP address that cannot collide with the
# client radio. Some dual AR6003 boards expose the same factory address on both
# SDIO functions, which makes an otherwise running AP unreliable to discover.
microfx_ap_mac() {
  client=${1:-}
  case "$client" in
    ??:??:??:??:??:??) ;;
    *) return 1 ;;
  esac
  first=${client%%:*}
  remainder=${client#*:}
  first_value=$((0x$first))
  first_value=$(((first_value | 2) & 254))
  tail=${remainder##*:}
  prefix=${remainder%:*}
  tail_value=$(((0x$tail + 1) & 255))
  printf '%02x:%s:%02x\n' "$first_value" "$prefix" "$tail_value"
}
