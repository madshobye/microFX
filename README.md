# microFX

microFX is a portable graphics platform for building single-purpose HDMI
applications on low-power Linux devices. Applications are written in
JavaScript against a compact retained graphics API; the native runtime handles
DRM/KMS, OpenGL ES, resource batching, display modes, and application lifecycle.

The first hardware adapter targets an i.MX6 DualLite board with a Vivante GC880
GPU and 512 MB RAM. The platform boundary is explicit so additional adapters,
including Raspberry Pi and desktop Linux, can reuse the same engine,
applications, provisioning service, and remote editor.

## Platform components

```text
apps/                         Portable JavaScript applications and assets
engine/                       QuickJS runtime and retained GPU scene engine
engine/runtime/               Native executable and render loop
services/provision/           Optional Wi-Fi provisioning service and portal
services/peer-bridge/         Optional PeerJS project and asset transport
platforms/imx6dl-dg1/         i.MX6DL-DG1 Buildroot platform adapter
tools/mesh/                   Offline asset preparation tools
web/editor/                   Browser-based JavaScript and asset editor
```

The engine and applications do not depend on Wi-Fi, provisioning, PeerJS, a
particular init system, or the i.MX6DL board. Those integrations belong to
services and platform adapters.

The current i.MX6DL demo renders through DRM/KMS and OpenGL ES 2, supports
native or FPS-managed HDMI modes, keeps routine logs in RAM, and can deploy
applications transactionally over SSH.

Start with [HANDOVER.md](HANDOVER.md) for a reproducible host/VM/device setup.
Read [codex.md](codex.md) before changing architecture or deployment behavior.
For the current firmware target, see
[platforms/imx6dl-dg1/README.md](platforms/imx6dl-dg1/README.md).

Run the portable host-side unit and policy suite with:

```sh
./tests/run.sh
```

Current host evidence and the remaining physical-hardware gates are tracked in
[`V1-STATUS.md`](V1-STATUS.md). It deliberately distinguishes passing policy
tests from Wi-Fi, HDMI, WebRTC, and boot behavior that still needs a device.

The custom bootloader work under `platforms/imx6dl-dg1/bootloader/` is an
isolated prototype only. It is not consumed by the firmware build, SD-card
installers, or the general SSH development workflow.
