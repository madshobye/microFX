#!/bin/sh

# Pure recovery policy used by wifi-watchdog and host tests.
# Arguments: wpa_state, has_address(0/1), association_failures, address_failures.
microfx_wifi_action() {
  state=${1:-}
  has_address=${2:-0}
  association_failures=${3:-0}
  address_failures=${4:-0}

  if [ "$state" = COMPLETED ] && [ "$has_address" = 0 ] &&
     [ "$address_failures" -ge 3 ]; then
    printf '%s\n' renew-dhcp
  elif [ "$state" != COMPLETED ] && [ "$association_failures" -ge 9 ]; then
    printf '%s\n' rebuild
  elif [ "$state" != COMPLETED ] && [ "$association_failures" -eq 3 ]; then
    printf '%s\n' reassociate
  else
    printf '%s\n' wait
  fi
}

