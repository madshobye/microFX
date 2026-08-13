#!/bin/sh
set -eu

PLATFORM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST=${1:-192.168.3.109}
RATE=${2:-634}

case "$RATE" in
  528) FRACTION=0x00001200 ;;
  594) FRACTION=0x00001000 ;;
  634) FRACTION=0x00000F00 ;;
  679) FRACTION=0x00000E00 ;;
  *)
    echo "Usage: $0 [host] {528|594|634|679}" >&2
    exit 2
    ;;
esac

"$PLATFORM_DIR/scripts/canvas-ssh.sh" "$HOST" "
  set -eu
  /etc/init.d/S40canvas stop
  killall -9 canvas-demo 2>/dev/null || true

  # Gate PFD1, change its fractional divider, then ungate it.
  devmem 0x020c8104 32 0x00008000
  devmem 0x020c8108 32 0x00003F00
  devmem 0x020c8104 32 $FRACTION
  devmem 0x020c8108 32 0x00008000

  /etc/init.d/S40canvas start

  if [ -r /tmp/canvas-gpu-watchdog.pid ]; then
    kill \"\$(cat /tmp/canvas-gpu-watchdog.pid)\" 2>/dev/null || true
  fi

  (
    while sleep 5; do
      temperature=\$(cat /sys/class/thermal/thermal_zone0/temp)
      if [ \"\$temperature\" -ge 82000 ]; then
        /etc/init.d/S40canvas stop
        killall -9 canvas-demo 2>/dev/null || true
        devmem 0x020c8104 32 0x00008000
        devmem 0x020c8108 32 0x00003F00
        devmem 0x020c8104 32 0x00001200
        devmem 0x020c8108 32 0x00008000
        /etc/init.d/S40canvas start
        echo \"thermal rollback at \$temperature millidegrees\"
        exit 0
      fi
    done
  ) >/tmp/canvas-gpu-watchdog.log 2>&1 </dev/null &
  echo \$! >/tmp/canvas-gpu-watchdog.pid

  echo requested_gpu_clock=${RATE}MHz
  printf 'pfd_register='
  devmem 0x020c8100 32
  printf 'temperature_mC='
  cat /sys/class/thermal/thermal_zone0/temp
"
