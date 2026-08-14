#!/bin/sh

microfx_write_network_status() {
  printf '%s|%s|%s\n' "$1" "$2" "$3" >>"$MOCK_STATUSES"
}
