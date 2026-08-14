# microFX v1 evidence map

This is a verification map, not a release claim. A requirement is marked
host-verified only when the portable suite exercises the real implementation
boundary. Physical-radio, HDMI, browser/WebRTC, and boot-ROM behavior remains
hardware validation even when its policy code has deterministic tests.

## Live development baseline — 2026-08-14

The active hardware root is slot B (`/dev/mmcblk0p3`). It received the
whitelisted active-root userspace update without a reboot, followed by the
current engine/demo release `release-20260814-081649`. Post-update checks
confirmed `/data`, the `wlan1` default route at `192.168.3.109`, Dropbear,
the graphics supervisor/renderer, status service, and Peer bridge. The setup
AP/provisioning path is intentionally disabled for this development baseline.
A captured HDMI frame showed the updated demo at 1280x720 and approximately
31 FPS.

The active root also received the Buildroot-produced `regulatory.db` and
signature after checksum validation. They take effect on the next boot. The
complete Buildroot build and host suite pass, and the root image and compressed
image match `artifacts/SHA256SUMS` (artifacts remain deliberately untracked).

This is not yet a both-slot or reboot acceptance result. Slot A was not changed,
the complete image was not flashed to either slot during this update, and the
next reboot must verify the new boot policy, wireless firmware load, persistent
data mount, client Wi-Fi, SSH, Peer bridge, and graphics startup on hardware.

| Area | Current evidence | Remaining v1 gate |
| --- | --- | --- |
| Client Wi-Fi | Recovery policy and runtime fixtures cover reassociation, DHCP renewal, sustained rebuild, RAM-only status, power-save disablement, multiple profiles, and atomic same-SSID replacement. The target image includes upstream AR6003 firmware; the build rejects missing or malformed SD31 board data, and hardware acceptance verifies the alias, byte count, checksum, and absence of driver fallback warnings. | Near/far and AP-interruption soak tests on the board; confirm reconnect time, signal, retry rate, and TX power. |
| Setup AP and captive discovery | Hostapd beacon state, AP mode, address, HTTP health, repair policy, distinct radio MAC, and Apple/Android/Windows probe files are host-tested. A runtime fixture executes the real setup service, forces beacon retry, verifies all three portal daemons remain alive, checks cleanup, and proves it never commands the independent client interface. | Confirm the SSID and automatic captive opening on representative clients after cold boot and after service recovery. |
| Setup/control portal | Wi-Fi and Peer ID persistence, network/setup health, project selection, restart, and acknowledged renderer health are exercised through the real CGI and supervisor scripts. The browser controller has deterministic UI tests for health rendering, refresh-safe selection, activation, failure recovery, and malformed responses; a real local-browser fixture passes project Run and Restart. The platform management server publishes the self-contained Studio at `/studio/`, exposes the validated current Peer ID, and links into Studio with that target preselected. Packaging and the runtime publication boundary are host-tested. | Browser interaction pass on both `10.42.0.1` and the client-network address. |
| Projects and persistence | Project folders, metadata, independent assets, active selection, restart, bounded whole-project revisions, inspect, restore, and fallback are protocol/integration tested. | Reboot and A/B-slot persistence pass on hardware. |
| Bundled fallback | The demo is installed as the factory application; missing, invalid, and failed selected projects exercise fallback/fail-fast paths in supervisor tests. | Cold-boot visual confirmation on both root slots. |
| Retained JavaScript graphics | Host tests cover retained scene mutation, translation/visibility groups, open/closed quad-batch polylines, project-confined model/image/font assets, a bounded four-face font cache API, quality policy, dynamic density, color/depth/dither/AA configuration, and the ten bundled application scripts. The live-flight concept uses one retained group per aircraft while preserving the same native element and draw counts. An instrumented reusable app harness stress-runs all ten projects plus the fallback for 18,000 frames each and rejects per-frame GPU allocation, invalid handles, non-finite mutations, asset traversal, and capacity overflow while reporting mutation pressure. | Visual/API acceptance, custom-font rendering, and long-run memory test on target GLES 2. |
| Live data concepts | Flight and energy adapters normalize bounded OpenSky and Energinet fixtures and atomically publish size-limited results only to RAM; bundled snapshots and the last good live result survive request or normalization failures. Runtime fixtures exercise clock gating, normal cadence, shorter failure retry, stale-data preservation, publication, unchanged data, and renderer reload without SD writes. | Live endpoint/rate-limit behavior and a multi-hour clock/network interruption recovery test on the target network. |
| Studio | Connection history, reconnect/disconnect, correlated requests, timeout/error handling, independent resumable asset transfers, console polling, revisions, Save & Run, Restore & Run, renderer failure, and recovery are host-tested. A DataChannel is not reported online until a versioned application handshake succeeds. A visible payload-free interaction trace and a read-only staged check cover protocol, project list/retrieve, and console paths; matching device logs identify every request/response by negotiation, SID, ID, type, and result. Save & Run is one rollback-safe device transaction for code, selection, and activation; replay after a lost acknowledgement creates neither another revision nor another restart. Interactive project requests share one reconnect session. A stateful browser/PeerJS harness imports the real page module and drives Connect, handshake gating, Check, Retrieve, project creation/switching, plain Save, Save & Run, metadata, independent upload, asset download/delete, revision inspection, both restore modes, console clearing, and Disconnect through the actual controls. It proves a one-file upload preserves an existing asset. The exact Studio action controller also runs against the real C++ handler and real supervisor, proving submission locking, persistence, activation health, renderer failure, and recovery. Real bridge-interruption tests cover large asset resume, Save & Run, and Restore & Run recovery. Pinned PeerJS and Ace browser assets, JavaScript mode, theme, worker, licenses, and checksums are vendored; a real local-browser pass confirms they load with no CDN requests or console errors. | Chrome-to-device WebRTC soak test including large assets, reconnect during an operation, reboot, and revision rollback. |
| Profiling and performance | Frame budget, worst frame, missed frames, CPU/non-CPU pacing, named-stage accounting, EGL/GBM/DRM stages, and opt-in page-flip timing have parsers/tests and are present in the target image. The report groups output resolution, density, and target FPS instead of averaging unlike configurations, exposes budget utilization/headroom, provides a comparison matrix and failing regression thresholds, and refuses to attribute unlabelled DRM samples across mixed configurations. A host-tested campaign applies whitelisted profiles only through volatile `/run` state, captures separate raw/JSON/text evidence, clears the override, and restores the normal project; it does not touch the boot prototype or persistent project data. | Capture comparable 1080p/720p fixed/dynamic runs and visual output on hardware. |
| Unit/integration framework | `tests/run.sh` covers C, C++, Node, Python, POSIX service fixtures, real protocol handlers, and supervisor integration. `tests/lib/microfx-test.sh` is the dependency-free service-test library; `apps/tests/lib/runtime-test.mjs` is the retained-application contract and stress library. A machine-readable 19-command protocol contract prevents drift between the real bridge, Studio, the four-command read-only diagnostic, and real-handler tests. The read-only hardware collector records boot slot, persistence, renderer, distinct radio roles, Wi-Fi, AP, portal, clock, peer, and firmware evidence; its 21-check evaluator produces labeled, deterministic pass/fail summaries without modifying the target. The ordinary Buildroot path produces a checksummed 1.5 GiB root image containing the tested service policies and the expected kernel/DTB/uEnv boot payload, independently of the boot prototype. | Run and retain near/far, interruption, reboot, and both-slot acceptance samples on hardware. |
| Layer compositor | Portable retained layer descriptors remain isolated and are tested not to enter the normal image. The bounded C/JavaScript planner preserves back-to-front ordering by allowing native planes only for a contiguous topmost layer suffix; unsupported effects or plane exhaustion safely flatten lower layers instead of reordering them. | Keep the single GLES renderer authoritative unless a future platform presents a simpler, stable composition path. |
| Independent boot | U-Boot/DCD feasibility notes and a machine-validated, microFX-owned MBR layout contract are isolated and cannot enter normal builds or installers. A pinned upstream U-Boot 2025.01 overlay configures UART1, USDHC3, non-persistent environment, decoded DDR facts, and bounded selected-slot/fallback policy; it cross-compiles to `u-boot.imx`. Its opt-in builder copies upstream source and cannot create or install an image. | Serial confirmation of DDR, USDHC/GPIO and ROM offset; design the last-known-good commit protocol; then validate on a spare card before considering any integration. |

## Authoritative commands

```sh
./tests/run.sh
./platforms/imx6dl-dg1/scripts/build.sh
```

The first command is the host verification gate. The second produces the
ordinary Buildroot root filesystem used by the existing A/B SD and SSH
development workflow. Neither command enables the experimental compositor or
independent boot prototype.

Hardware acceptance steps are maintained in
[`platforms/imx6dl-dg1/HARDWARE-VALIDATION.md`](platforms/imx6dl-dg1/HARDWARE-VALIDATION.md).
