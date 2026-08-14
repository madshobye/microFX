# microFX project manual

## Purpose

microFX is a small GPU-first graphics appliance runtime. It is designed for
constrained Linux hardware and a single full-screen application, not as a
general Processing, p5.js, browser, desktop, or raylib compatibility layer.
Scene changes should become compact GPU state updates; avoid rebuilding meshes,
issuing many draw calls, or evaluating thousands of shapes on the CPU per frame.

The first proven board profile is i.MX6DL-DG1: NXP i.MX6 DualLite (two
Cortex-A9 cores), Vivante GC880 exposed through Mesa/Etnaviv OpenGL ES 2,
512 MB DDR3, microSD, AR6003 Wi-Fi and HDMI. The same engine should remain
portable to Raspberry Pi and desktop Linux by keeping platform boot/network
code outside the engine.

## Source layout and ownership

- `apps/demo`: executable demonstration and its models/shaders.
- `apps/onboarding`: portable 40-second boot information scene and portal QR.
- `engine`: reusable scene, script, renderer and asset modules. Platform-neutral
  interfaces belong here; direct DRM, Wi-Fi and init scripts do not.
- `services/provision`: optional portable captive-portal handler and web UI;
  platforms decide whether and how to package it.
- `services/peer-bridge`: optional Linux PeerJS DataChannel and project-file
  protocol built on the C libpeer library; it must not enter the render loop.
- `platforms/imx6dl-dg1/buildroot`: Buildroot external tree,
  kernel/DT configuration, root overlay and raylib DRM patches.
- `platforms/imx6dl-dg1/scripts`: board build, SSH release upload, screenshot,
  SD installer and hardware diagnostics.
- `platforms/imx6dl-dg1/private`: local Wi-Fi and SSH material. Its contents are
  ignored and must never be committed.
- `platforms/imx6dl-dg1/artifacts`: generated firmware, screenshots and temporary
  update files. Only `.gitkeep` is versioned.
- `tools`: offline conversion/validation tools that do not run on the appliance.
- `web/editor`: static Ace editor and PeerJS project/asset client.

Do not place reusable renderer logic inside Buildroot packages. The Buildroot
package should point to the repository-level app/engine source.

## Rendering constraints

The GC880 supports OpenGL ES 2, not compute shaders or ES 3.1. The proven fast
path batches the current 3D scene into one static VBO, one shader, one draw call,
and updates object transforms/colors through uniforms. Lighting is per vertex.
Cube outlines are baked into the same VBO. Per-fragment procedural outlines,
separate line passes and full-screen layered effects have all exceeded the frame
budget on this GPU.

QuickJS is statically embedded with a 16 MiB heap limit and 512 KiB stack. The
implemented JS API uses a cheap retained quad/geometry batch for ordinary
rectangles, gradients and circles. Experimental SDF circles and rounded
rectangles remain an explicit second batch. Elements update by handle; each
renderer submits one compact GLES2 draw.
Script exceptions are fail-fast. Keep construction out of `update(time, delta)`
and extend the bindings only with bounded retained operations. See
`engine/README.md` for the current API.

The output mode defaults to the first preferred connected DRM mode. Renderer
settings are read in this order: compiled defaults, `/etc/microfx.conf`,
`/data/config/microfx.conf`, the application's top-level `fx.configure({...})`,
then `MICROFX_*` environment variables. This lets an application carry its own
normal policy while preserving a final operator override. Supported keys are:

```text
MICROFX_OUTPUT_WIDTH=0
MICROFX_OUTPUT_HEIGHT=0
MICROFX_PIXEL_DENSITY=auto
MICROFX_MIN_PIXEL_DENSITY=0.50
MICROFX_TARGET_FPS=30
```

Zero output dimensions mean native DRM mode. A numeric pixel density is fixed;
`auto` starts at native, measures running frame time and restarts into the next
lower mode advertised by DRM when the target cannot be met. Density is a
convenient linear control over that discrete HDMI mode list; values mapping to
the current mode are skipped. Explicit non-zero output dimensions disable
automatic mode changes. The entire scene and UI render directly at the selected
global mode and the monitor performs any physical scaling. Do not reintroduce a
lower-resolution full-screen FBO: on GC880 the final texture upscale cost more
than it saved. RGB565 is intentional: it halves scanout bandwidth versus
RGBA8888 at the cost of color precision.

OpenGL ES 2 has no reliable GPU timer query on this target. The status bar's CPU
average uses process CPU time. “GPU” is the remaining wall-clock render/present
time, so it includes driver and page-flip waits rather than being a pure shader
timer.

## i.MX6DL-DG1 boot and storage model

The firmware is Buildroot + mainline Linux + Mesa/Etnaviv. The current DG1
platform contract uses the existing U-Boot and MBR layout; root slots are partitions 2 and 3 and
persistent `/data` is partition 4. The generated root filesystem fits either
1536 MiB root slot.

Application releases are copied to `/data/apps/incoming`, checksum-verified,
renamed on the same filesystem and selected by `/data/apps/current`. The current
development policy is fail-fast: a bad app stops rather than silently rolling
back. SSH is independent so failures remain diagnosable. Root A/B firmware
updates are separate and are not yet automated.

Once per firmware boot, the supervisor first runs the firmware-owned engine with
`apps/onboarding/scripts/main.js`. For 40 seconds it shows the setup AP, portal
QR, current PeerJS ID and a countdown, then exits cleanly and starts the active
project. The scene is regular JavaScript; only the supervisor's environment
values are platform-specific. Restarting the canvas service does not replay it
because `/run/microfx-onboarding-shown` survives until reboot.

Provisioning is an optional platform feature and is disabled by default in the
development image until its radio path completes hardware validation. When
explicitly enabled, it uses the otherwise-idle `wlan0` PHY as a WPA2 setup AP
and reserves `wlan1` for managed client connections. The captive portal appends
validated networks transactionally to `/data/config/wpa_supplicant.conf`;
wpa_supplicant chooses among all saved networks. The configurable peer ID is
stored at `/data/config/peer-id`. Captive HTTP/DNS are platform services and
must remain outside the renderer.

Remote editing is also optional and separate. `services/peer-bridge` answers
PeerJS DataChannels through pinned `sepfy/libpeer`, stores `main.js` and assets
atomically below `/data/apps/current`, and emits `/run/microfx-project-reload`.
The DG1 supervisor consumes that signal and restarts the app; other platform
adapters may implement the same signal differently. `web/editor` is a static
Ace client. The renderer has no PeerJS, WebRTC, portal, or Wi-Fi dependency.

Production defaults disable SSH/debug/persistent logs. Development can supply
`platforms/imx6dl-dg1/private/canvas-debug.conf` with key-only SSH enabled. Normal
logs live in `/tmp`; persistent logging must be deliberately enabled because SD
write endurance matters.

## Build and test workflow

On macOS install `lima`, `e2fsprogs`, and `dtc`. The existing Lima VM is named
`microfx-build` and uses Buildroot 2025.02.16.

```sh
cd platforms/imx6dl-dg1
./scripts/build.sh
./scripts/canvas-upload.sh 192.168.3.109
./scripts/canvas-ssh.sh 192.168.3.109
./scripts/canvas-screenshot.sh 192.168.3.109
```

Run shell syntax checks and a full Buildroot build before firmware commits.
Application changes should be uploaded transactionally and verified from
`/tmp/canvas.log`; use screenshots sparingly because 1080p readback temporarily
stalls frame timing. Test both native density and any changed fixed/automatic
density path on hardware. VM/desktop tests cannot prove DRM page flips, Vivante
shader behavior, HDMI mode selection, SDIO Wi-Fi or the DG1 device tree.

The development-only active-root updater has a narrow, tested whitelist for
init/configuration hardening. It must preserve the client Wi-Fi and SSH services,
create a persistent backup, verify live health, and never be treated as an A/B
firmware installer. Release images are still written to both root slots from SD.

## Device-side audit baseline

The repository overlay is authoritative for `canvas-supervisor`, activation,
Wi-Fi, `/data`, Dropbear and init scripts. The development unit may run an older
root while newer applications are exercised by SSH release upload. Compare live
files before adopting a device-side change; do not copy an older script back
over the source. `/etc/canvas.conf` intentionally enables key-only SSH on that
unit while routine logs remain in RAM. The optional 634 MHz GPU setting is
volatile, has a thermal watchdog, and is represented by
`canvas-gpu-clock.sh`; reboot restores stock clocking.

## Engineering rules

- Keep secrets and generated firmware out of Git.
- Prefer static buffers, retained geometry/quads, uniform updates and atlases;
  reserve SDF fragment math for shapes that materially benefit from it.
- Add APIs only when they map to a bounded GPU-efficient implementation.
- Fail clearly on shader, FBO, asset or release validation errors.
- Keep network provisioning and peer transport outside the render loop.
- Persist configuration/assets transactionally under `/data`; keep logs in RAM.
- Preserve original assets when generating optimized derivatives and keep the
  conversion tool reproducible.
- Update this manual when architecture or operator workflow changes; do not turn
  it into a chronological logbook.
