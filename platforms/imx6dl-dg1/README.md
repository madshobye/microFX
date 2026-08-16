# microFX i.MX6DL-DG1 Platform

This directory is the microFX platform adapter for the i.MX6 DualLite DG1
profile. It combines Buildroot, mainline Linux, Mesa/Etnaviv, DRM/KMS,
OpenGL ES 2, raylib, the microFX engine, and the optional network services into
a boot-to-HDMI appliance image. It is the authoritative general-development setup;
the independent bootloader work remains an isolated prototype.

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
./scripts/setup-build-vm.sh
./scripts/test-vm.sh
./scripts/build-linux.sh
```

The mount-free Lima VM uses Buildroot 2025.02.16. `setup-build-vm.sh` creates
and provisions it from pinned inputs. A development machine can reuse a
differently named local VM by putting its name on one line in ignored
`private/build-vm`, or by setting `VM_NAME` for one invocation.
Generated files go in the ignored `artifacts/` directory. The main output,
`microfx-imx6dl-dg1.rootfs`, fits either Linux root slot.

For a faster compile-only check run:

```sh
./scripts/build-graphics.sh
```

This is the normal development path for engine, renderer, raylib backend,
shader, and embedded JavaScript changes. It preserves the Linux cache and does
not generate a root image. `build-linux.sh` is the explicit complete-image
workflow. `build-linux.sh --clean` moves the existing output to a timestamped
cache backup instead of deleting it. The ambiguous `build.sh` entry point is
disabled.

The VM verifies the ARM build and package graph. DRM page flips, HDMI modes,
Etnaviv behavior, SDIO Wi-Fi, and the device tree still require physical
hardware testing.

## Experimental mappings

The custom bootloader files in `bootloader/` are an isolated prototype. They
are not inputs to `build-linux.sh`, the Buildroot configuration, the SD installers,
or the runtime supervisor. The existing boot chain and single Etnaviv OpenGL ES
render path remain authoritative. Hardware validation gates are recorded in
[`HARDWARE-VALIDATION.md`](HARDWARE-VALIDATION.md).

## SD installation

The generated root filesystem is a partition image, not a whole-card image.
Use a prepared card containing the proven raw bootloader, DG1 partition table,
and boot environment. A blank card is not currently supported. After carefully
confirming the removable disk identifier, the supported installer validates
the card and writes both root slots while preserving p1, p4, and the raw area:

```sh
diskutil list
sudo ./scripts/install-full-sd.sh /dev/diskN
```

Writing the wrong disk destroys data. UART is 3.3 V, 115200 8N1 on `ttymxc0`.
The single current application runtime is independent of the root slots and
lives at `/data/apps/runtime` on persistent partition 4. Only web-UI project
scripts retain revision history.

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
MICROFX_DENSITY_SAMPLE_FRAMES=60
MICROFX_DENSITY_STEP=0.1
MICROFX_DENSITY_DOWN_THRESHOLD=1.08
MICROFX_DENSITY_UP_THRESHOLD=0.72
MICROFX_DENSITY_UP_SAMPLES=4
```

Zero dimensions select the monitor's preferred HDMI mode. Automatic density
steps down through advertised modes under load and recovers upward only after
sustained spare capacity, using separate thresholds to avoid oscillation. Setting
both dimensions forces one mode and disables automatic resolution changes. The
scene renders directly at the selected global mode without a full-screen
upscale pass.

## Onboarding and networking

At boot the platform supervisor runs the portable microFX onboarding app for 40
seconds before starting the active project. It shows the configured PeerJS
device ID and a countdown. On first boot, a board-derived (or randomly seeded)
human-readable suffix creates unique setup SSID, setup password, and PeerJS
defaults in `/data/config/device-identity.conf`; the editable PeerJS value is
kept separately in `/data/config/peer-id`. Setup-network information and a
Wi-Fi registration QR are included only when the platform setup network is
enabled.

Development images default to `MICROFX_PROVISIONING=0` in
`/etc/microfx.conf`. This prevents hostapd, dnsmasq, the setup HTTP server, and
the provisioning watchdog from starting, leaving stored-network client Wi-Fi
and key-only SSH as the sole recovery path. Set it to `1` only for an explicit
setup-network hardware test; the setup implementation and its host tests remain
available while disabled.

Product defaults are centralized in `/etc/microfx-product.conf`. The platform
adapter owns hostapd, dnsmasq, HTTP, Wi-Fi interface selection, and init
integration; none of those concerns enter the graphics engine or JavaScript
application API.

If Save & Run activates JavaScript that fails to parse or throws during the
health check, the supervisor keeps the failed activation status and starts the
firmware-owned error scene. It presents the QuickJS message and source line as
white text on black until the next activation request; SSH remains independent.

The setup-network watchdog validates more than daemon PIDs: it requires hostapd
to report an active beacon, AP mode, an UP link, `10.42.0.1/24`, and a successful
local portal request. Its RAM-only status is exposed on the management page
alongside client signal, rate, and address so hardware tests identify the failed
layer without enabling persistent logging. Captive-probe paths for Apple,
Android/Chromium, and Windows are generated and host-tested as actual response
files rather than directories.

The same management server publishes the vendored browser Studio at
`http://DEVICE/studio/`. The portal supplies the configured Peer ID in the
Studio link, removing the separate Python web-server step from normal device
development. Page assets are local; PeerJS signalling still needs network
access to the configured signalling service.

The host suite also executes the real setup SysV service with isolated runtime,
sysfs, interface, and content roots. It forces one failed beacon attempt before
success and verifies AP mode/address setup, stable locally administered MAC,
hostapd/dnsmasq/httpd liveness, captive content, cleanup, and the invariant that
the setup path never issues a command against the independent client interface.
All injectable values retain the target paths and `wlan0`/`wlan1` names as their
production defaults.

Renderer profiling is opt-in and RAM-only. After enabling it with
`scripts/canvas-profile.sh HOST on`, use `scripts/canvas-profile.sh HOST report`
for a frame-weighted budget, worst-frame, missed-frame, CPU, non-CPU/pacing,
render-stage, and DRM page-flip summary. Non-CPU/pacing is not presented as a
true GPU timer because it includes display synchronization on this GLES 2 stack.

`S44data-adapters` is the optional network-to-project boundary for live demos.
It polls only while a matching project is active, normalizes remote responses
with isolated QuickJS adapter files, and atomically publishes bounded JSON under
`/run/microfx-data/PROJECT/`. The flight adapter uses a small OpenSky bounding
box at five-minute cadence; the electricity adapter uses Energinet's DK2
Elspotprices data at hourly cadence. Requests wait for a synchronized clock,
failed requests use a shorter retry interval, oversized data is rejected, and
the last good RAM result remains available through transient failures. Status,
attempt, and success timestamps also remain under `/run`. Both retain bundled
project snapshots when no live result exists, and neither writes routine feed
updates to the SD card. Disable all adapters with
`MICROFX_DATA_ADAPTERS=0` in the persistent platform configuration.

The development onboarding profile displays the setup password on HDMI. A
production deployment should provide its own credential and security policy.

The board profile has no persistent real-time clock. `S42time` immediately
seeds an invalid clock from `/data/state/last-known-time`, or from the firmware
release-file timestamp on a fresh card. It then retries BusyBox NTP for the
entire boot instead of giving up before delayed Wi-Fi recovery. One usable
timestamp is atomically retained per boot; diagnostics and service status stay
in RAM. Credited random seeds are also stored below `/data/state/seedrng` after
the persistent partition mounts, instead of writing the root slot. The PeerJS
service does not attempt TLS until the clock is usable.

`microfx-status` produces one tab-separated health report containing release
ID, active root slot, mount state, selected Wi-Fi firmware and checksum,
network/time/recovery state, boot-loop counters, and core service liveness.
`S45status` refreshes the same report atomically at `/run/microfx-status`; it
never writes routine status to the SD card.

## SSH deployment

The complete setup, recovery decision tree, live checks, and script inventory
are maintained in [`../../HANDOVER.md`](../../HANDOVER.md).

Local development credentials belong in the ignored `private/` directory:
`canvas-debug.conf`, `canvas_debug_ed25519`, and its public key. Connect, upload,
and retrieve a screenshot with:

```sh
./scripts/canvas-ssh.sh 192.168.3.109
./scripts/canvas-upload.sh 192.168.3.109
./scripts/canvas-screenshot.sh 192.168.3.109
./scripts/install-active-root-ssh.sh 192.168.3.109
./scripts/install-network-hardening-ssh.sh 192.168.3.109
./scripts/canvas-profile.sh 192.168.3.109 on
./scripts/canvas-profile.sh 192.168.3.109 status
./scripts/canvas-profile.sh 192.168.3.109 off
```

Uploads stage below `/data/apps/incoming`, verify SHA-256, and replace the single
fixed `/data/apps/runtime` directory. No dated or previous runtime is retained,
and there is no previous-release rollback target. A failed pending runtime
stops loudly while SSH and the factory error renderer remain independent for
diagnosis.

Project uploads follow the same development policy: they replace the one fixed
project directory without keeping a project backup. If the new project fails
its health window, it remains installed and the renderer stays stopped so the
error is visible; no older project is restored automatically.

`install-active-root-ssh.sh` is the narrow development-only exception for
updating tested init/configuration hardening without rewriting an SD card. It
requires a healthy `/data` mount, client default route, and Dropbear session;
checksums a strict file whitelist; backs up replaced files below
`/data/state/root-update-backups`; and checks Wi-Fi, SSH, the graphics
supervisor, and renderer after installation. It never changes the client Wi-Fi
service, SSH service, kernel, DTB, inactive root, or partition layout, and it
does not reboot. Use a complete SD image for a release or when both root slots
must match.

`install-network-hardening-ssh.sh` is the still narrower exception for the
reviewed development recovery services. It checksums a fixed file list, never
includes WPA credentials, backs up every replacement, installs atomically, and
does not restart the live Wi-Fi or SSH session. It updates only the active root;
the full SD installer remains authoritative for matching both A/B slots.

In the development profile, `S39dropbear-debug` starts key-only SSH before the
graphics and normal networking services. The normal `S41wifi` connector keeps
its hardware-proven boot position and performs up to four clean association
rounds. Its watchdog and the RAM-only recovery guardian wait 120 seconds so
they cannot compete with those rounds. After three failed health checks the guardian stops the normal Wi-Fi processes and
uses the frozen, known-working `wlan1` + `wpa_supplicant` + `udhcpc` sequence
with `/data/config/wpa_supplicant.conf` (or the image fallback configuration),
then starts SSH again. Recovery is bounded; on failure it removes its ownership
flag and restarts normal Wi-Fi. This path never creates an access point.

The same SSH-development guardian counts boots in `/data/state`. A boot becomes
stable after three minutes of uptime; two consecutive boots that end before
that marker cause `S40canvas` to remain off for the next boot while normal
Wi-Fi and SSH recover. No counters are written when `/data` is not actually
mounted, and the guardian is disabled unless `CANVAS_SSH=1`.

`/etc/canvas.conf` controls development diagnostics. Routine logs stay in RAM
under `/tmp`; enable `CANVAS_PERSIST_LOGS=1` only when persistence is worth the
additional SD writes.

Profiling is similarly opt-in and RAM-only. It reports average JavaScript,
background, mesh, overlay, interface, presentation, process-CPU, and complete
frame times alongside the lower-level DRM swap breakdown. Enabling it restarts
the renderer but does not alter project data or the normal boot configuration.

For repeatable comparisons, run the volatile benchmark campaign from the host:

```sh
./scripts/canvas-benchmark.sh 192.168.3.109 artifacts/benchmarks/near
```

By default it captures `native-fixed`, `native-75`, `native-50`, and
`720-fixed`. Override that list with `MICROFX_BENCHMARK_PROFILES`, and the
capture duration with `MICROFX_BENCHMARK_SECONDS`. Optional
`MICROFX_BENCHMARK_MAX_BUDGET_USE` and
`MICROFX_BENCHMARK_MAX_OVER_BUDGET` values turn measured limits into failing
regression checks. Raw target output, per-profile text/JSON reports, and a
combined comparison matrix are retained only in the requested host directory.
For quality isolation, `1080-msaa` changes only MSAA from `1080-fixed`, while
`1080-color` changes only scanout color/depth. The `-60` variants of fixed and
MSAA exercise the display-rate boundary without conflating it with resolution;
`720-fixed-60` does the same at 720p. Unsupported EGL quality combinations fail
loudly and the capture helper restores the normal project; on the current
GC880/Mesa target, `msaa4` has no suitable EGL configuration.
`1080-quality` enables RGBA8888, 24-bit depth, and MSAA together.

Each target profile is written below `/run`, applied to one renderer child by a
strict variable whitelist, and removed before the normal project is reloaded.
The campaign does not edit `/data`, either root slot, the SD boot environment,
or the isolated boot/compositor prototypes. It is therefore part of the
existing SSH development workflow, not the experimental boot work.

## Distribution

Firmware distributions must carry the source, license texts, and notices
required by Linux, Buildroot, Mesa, raylib, QuickJS, libpeer, and packaged
firmware components. Generated images, device credentials, and local diagnostic
captures are deliberately excluded from this repository.
