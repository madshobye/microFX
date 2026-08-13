# microFX i.MX6DL-DG1 Platform

This directory is the microFX platform adapter for the i.MX6 DualLite DG1
profile. It combines Buildroot, mainline Linux, Mesa/Etnaviv, DRM/KMS,
OpenGL ES 2, raylib, the microFX engine, and the optional network services into
a boot-to-HDMI appliance image.

The current adapter uses the board's four-partition SD layout:

- partition 1: boot configuration;
- partitions 2 and 3: interchangeable 1536 MiB Linux root slots;
- partition 4: persistent application and configuration data mounted at
  `/data`.

The root filesystem carries the kernel and device tree as
`/boot/microfx-imx6dl-dg1.img` and `/boot/microfx-imx6dl-dg1.dtb`, produced
from the microFX kernel configuration and `imx6dl-microfx-dg1.dts`.

## Build

On macOS install `lima`, `e2fsprogs`, and `dtc`, then run:

```sh
./scripts/build.sh
```

The `microfx-build` Lima VM uses Buildroot 2025.02.16. Generated files go in the
ignored `artifacts/` directory. The main output,
`microfx-imx6dl-dg1.rootfs`, fits either Linux root slot.

For a faster compile-only check run:

```sh
./scripts/test-vm.sh
```

The VM verifies the ARM build and package graph. DRM page flips, HDMI modes,
Etnaviv behavior, SDIO Wi-Fi, and the device tree still require physical
hardware testing.

## SD installation

Use a card containing the DG1 partition table and boot environment. After
carefully confirming the removable disk identifier, write the image to root
slot 2 and optionally slot 3:

```sh
diskutil unmountDisk /dev/diskN
sudo dd if=artifacts/microfx-imx6dl-dg1.rootfs of=/dev/rdiskNs2 bs=4m
sync
diskutil eject /dev/diskN
```

Writing the wrong disk destroys data. UART is 3.3 V, 115200 8N1 on `ttymxc0`.
Application releases are independent of the root slots and live below
`/data/apps` on persistent partition 4.

## Runtime and resolution

Renderer defaults are stored in `/etc/microfx.conf`. They can be overridden by
`/data/config/microfx.conf`, application-level `fx.configure({...})`, or
`MICROFX_*` environment variables:

```text
MICROFX_OUTPUT_WIDTH=0
MICROFX_OUTPUT_HEIGHT=0
MICROFX_PIXEL_DENSITY=auto
MICROFX_MIN_PIXEL_DENSITY=0.50
MICROFX_TARGET_FPS=30
```

Zero dimensions select the monitor's preferred HDMI mode. Automatic density
can step through advertised modes when measured FPS misses the target. Setting
both dimensions forces one mode and disables automatic resolution changes. The
scene renders directly at the selected global mode without a full-screen
upscale pass.

## Onboarding and networking

At boot the platform supervisor runs the portable microFX onboarding app for 40
seconds before starting the active project. It shows setup Wi-Fi information,
the captive-portal QR code, the configured PeerJS device ID, and a countdown.

Product defaults are centralized in `/etc/microfx-product.conf`. The platform
adapter owns hostapd, dnsmasq, HTTP, Wi-Fi interface selection, and init
integration; none of those concerns enter the graphics engine or JavaScript
application API.

The development onboarding profile displays the setup password on HDMI. A
production deployment should provide its own credential and security policy.

The board profile has no persistent real-time clock. `S42time` waits for
networking and uses BusyBox NTP to initialize the clock from public time
servers. It runs before the PeerJS service, retries asynchronously while Wi-Fi
comes online, and writes diagnostics only to `/tmp/microfx-time-sync.log`.

## SSH deployment

Local development credentials belong in the ignored `private/` directory:
`canvas-debug.conf`, `canvas_debug_ed25519`, and its public key. Connect, upload,
and retrieve a screenshot with:

```sh
./scripts/canvas-ssh.sh 192.168.3.109
./scripts/canvas-upload.sh 192.168.3.109
./scripts/canvas-screenshot.sh 192.168.3.109
```

Uploads stage below `/data/apps/incoming`, verify SHA-256, and atomically select
the new release. The default `CANVAS_FAIL_FAST=1` stops when an application
fails instead of silently restoring another revision. SSH remains independent
for diagnosis.

`/etc/canvas.conf` controls development diagnostics. Routine logs stay in RAM
under `/tmp`; enable `CANVAS_PERSIST_LOGS=1` only when persistence is worth the
additional SD writes.

## Distribution

Firmware distributions must carry the source, license texts, and notices
required by Linux, Buildroot, Mesa, raylib, QuickJS, libpeer, and packaged
firmware components. Generated images, device credentials, and local diagnostic
captures are deliberately excluded from this repository.
