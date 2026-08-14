# Hardware validation map

This checklist separates host-tested platform behavior from work that requires
the physical i.MX6DL appliance. The normal firmware build, A/B SD-card layout,
SSH update workflow, and single DRM/GBM/EGL renderer remain the authoritative
general-development setup.

The independent boot work in `bootloader/` and the multi-plane compositor work
in `COMPOSITOR.md` are feasibility mappings only. Neither is packaged by the
normal build or installers. `tests/prototype-isolation-test.sh` enforces that
boundary.

## Verified without hardware

- all platform, engine, application, provisioning, peer protocol, Studio, and
  data-adapter tests pass under `tests/run.sh`;
- Wi-Fi and provisioning watchdog recovery thresholds are exercised with
  deterministic command and network-state fixtures;
- project retrieve, save, save-and-run, revision, activation, and renderer
  failure recovery are exercised through the real supervisor scripts;
- captive-portal control requests are exercised through the real supervisor;
- a missing or invalid selected project falls back to the bundled demo;
- quality configuration, dynamic pixel density, profiling records, assets,
  path confinement, and retained-scene behavior are covered by host tests;
- the normal image build requires the upstream 1,792-byte AR6003 SD31 board
  data and installs a deterministic `bdata.bin` alias; it also verifies both
  compatible AR6003 firmware files, `regulatory.db` and its signature, and
  rejects the obsolete `ath6kl mac=` module parameter. A missing or malformed
  artifact fails the completed target-tree build rather than producing a radio
  image with implicit fallback behavior.

## Validate on the current development image

Start each physical test position with a labeled, read-only acceptance capture:

```sh
./scripts/hardware-acceptance.sh 192.168.3.109 near artifacts/hardware
```

The command makes one SSH connection, stores the raw report and a deterministic
pass/fail summary locally, and identifies the active A/B root slot. It does not
restart services, enable profiling, or modify the device. `hardware-smoke.sh`
remains available when an unevaluated raw report is wanted. Repeat the labeled
capture after moving the unit, after AP interruption, after reboot, and after
changing root slots so reports remain comparable.

The image reports `/etc/microfx-release` as a small compatibility contract.
Acceptance requires schema `1`, platform `imx6dl-dg1`, and boot model
`existing-ab`; a missing marker means the device predates the currently tested
image even if individual services happen to be reachable. A completed one-shot
NTP process is healthy when the captured clock is valid, so acceptance checks
the resulting time state rather than requiring `ntpd` to remain resident.

1. Install the same completed image into root slots 2 and 3, preserving the
   normal A/B setup and persistent partition 4.
2. Boot each root slot at least once and confirm the demo appears, persistent
   data mounts, SSH starts, and the clock synchronizes.
3. Confirm the acceptance summary reports the SD31 alias, 1,792-byte size, a
   SHA-256 checksum, and zero driver fallback warnings. Record the negotiated
   channel, signal, retry rate, and reconnect time at near and far test
   positions. This proves packaging consistency; it does not replace radio
   range testing.
4. Interrupt the access point and upstream network independently. Confirm that
   reassociation, DHCP renewal, and full interface recovery happen without a
   reboot and without persistent debug logging.
5. Set `MICROFX_PROVISIONING=1` for this isolated test. Confirm the recovery
   setup SSID remains visible after onboarding, DHCP works,
   `http://10.42.0.1` opens, and Apple, Android/Chromium, and Windows captive
   probes open the portal. Confirm the same management page remains reachable
   over the normal LAN and reports `beacon=1`.
6. Open Studio and confirm it remains `verifying` with editing controls disabled
   until `system.ping` receives `system.pong`. Run **Check** and retain the
   interaction trace. Then perform Save & Run and match its browser request IDs
   with `PROTOCOL ... DEVICE` lines from the peer-bridge service log. Confirm
   `project.save-run` is acknowledged before `project.status` reaches `running`.
7. Reboot after project upload and revision rollback. Confirm the active project
   and assets survive and the browser Studio reconnects and retrieves state.
8. Run a repeatable page-flip campaign without changing the installed image:

   ```sh
   MICROFX_BENCHMARK_SECONDS=30 \
     ./scripts/canvas-benchmark.sh 192.168.3.109 artifacts/hardware/benchmark-near
   ```

   Repeat at the far test position and after a reboot. Retain the raw logs,
   per-profile JSON, and matrix. Record CPU, GPU submission, EGL swap, flip
   wait, frame time, missed target frames, and visual output. The helper uses a
   volatile `/run` override, removes it, and reloads the normal project after
   every capture; verify the normal project is visible before continuing.

## Later experimental validation

- Probe DRM plane formats and scaling with `scripts/drm-plane-probe.sh` before
  implementing a second 2D composition path. Keep it optional until blending,
  color conversion, synchronization, and fallback behavior are proven.
- Complete the DDR, storage offsets, pin multiplexing, and serial recovery facts
  in `bootloader/` before producing any custom boot image. Test it on a spare SD
  card; do not replace the current boot chain during general development.
