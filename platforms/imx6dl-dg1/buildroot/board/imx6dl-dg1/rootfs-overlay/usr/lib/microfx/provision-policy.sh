#!/bin/sh

# Pure setup-service recovery policy. Arguments are health booleans for the
# radio device, hostapd, dnsmasq, httpd, AP mode, link/address configuration
# local portal response and active beacon state, followed by consecutive
# failures.
microfx_provision_action() {
  radio=${1:-0}
  hostapd=${2:-0}
  dnsmasq=${3:-0}
  httpd=${4:-0}
  ap_mode=${5:-0}
  link=${6:-0}
  address=${7:-0}
  portal=${8:-0}
  beacon=${9:-0}
  failures=${10:-0}

  if [ "$radio" = 1 ] && [ "$hostapd" = 1 ] &&
     [ "$dnsmasq" = 1 ] && [ "$httpd" = 1 ] &&
     [ "$ap_mode" = 1 ] && [ "$link" = 1 ] &&
     [ "$address" = 1 ] && [ "$portal" = 1 ] &&
     [ "$beacon" = 1 ]; then
    printf '%s\n' healthy
  elif [ "$failures" -ge 3 ]; then
    printf '%s\n' repair
  else
    printf '%s\n' wait
  fi
}
