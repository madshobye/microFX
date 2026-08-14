# microFX development handover

This is the operational entry point for a new maintainer or a new Codex task.
Read `codex.md` for architecture and engineering rules, then use this document
for the concrete host, VM, SD-card, SSH, update, and recovery workflow.

## Safety boundary

There are six update levels. Use the narrowest one that fits the change:

1. `canvas-upload.sh` builds the current checkout and atomically replaces the
   application release under `/data`. This is the normal graphics/JavaScript
   workflow and does not modify a root slot.
2. `project-upload.sh` transactionally updates the current demo project while
   preserving a recoverable copy of its previous code and assets.
3. `studio-upload.sh` installs the cross-built peer bridge and static Studio,
   backing up the replaced active-root files under `/data`.
4. `install-active-root-ssh.sh` updates a strict whitelist of tested userspace
   service/configuration files on the active root. It creates a backup under
   `/data/state/root-update-backups`, validates network/SSH/graphics afterward,
   does not reboot, and never changes Wi-Fi or SSH startup itself.
5. `install-network-hardening-ssh.sh` updates only reviewed network/SSH
   recovery services on the active root. It backs up replacements, never
   includes credentials, and does not restart the live connection.
6. `install-full-sd.sh` writes the complete image to both Linux root slots.
   This is destructive and is the normal way to make both slots match.

Never use `dd` against an unresolved disk name. Never rewrite the raw boot area
or partition table during ordinary development. The custom U-Boot directory is
an isolated feasibility prototype, not the active boot path.

## Host prerequisites

The supported host workflow is macOS with Homebrew:

```sh
brew install lima e2fsprogs dtc
```

From the repository root, initialize the pinned Buildroot VM once:

```sh
cd platforms/imx6dl-dg1
./scripts/setup-build-vm.sh
./scripts/test-vm.sh
```

The setup script creates a mount-free Ubuntu Lima VM, installs Buildroot host
dependencies, downloads Buildroot 2025.02.16, verifies its pinned SHA-256, and
writes the selected VM name to ignored `private/build-vm`. Override the name
with `VM_NAME=name ./scripts/setup-build-vm.sh`. Builds and smoke tests copy the
current checkout into the VM over SCP; they never depend on a stale Lima mount.

Useful VM diagnostics:

```sh
cat private/build-vm
limactl list
limactl start "$(cat private/build-vm)"
limactl shell "$(cat private/build-vm)"
```

If Lima reports the VM as `Broken`, stop and repair or recreate that VM before
building. Do not assume old files in its Buildroot output represent this
checkout.

## Local private inputs

All machine credentials live in ignored `platforms/imx6dl-dg1/private/`:

```text
build-vm                       selected Lima instance name
canvas_debug_ed25519           SSH private key (host only)
canvas_debug_ed25519.pub       public key embedded in development images
canvas-debug.conf              development/debug runtime policy
wpa_supplicant.conf            optional fallback client network configuration
```

Generate a new SSH pair when setting up another developer:

```sh
ssh-keygen -t ed25519 -N '' -f private/canvas_debug_ed25519
```

The full build stages only the public key, debug policy, and optional Wi-Fi
configuration into the VM. The SSH private key and `build-vm` file must never
leave the host or enter Git. Production images should omit debug policy and
developer credentials.

## Boot and partition map

The current development card uses the existing MBR and bootloader layout:

```text
raw area before p1   existing board U-Boot (not written by normal scripts)
p1                   board/boot configuration
p2                   root slot A, ext4, approximately 1536 MiB
p3                   root slot B, ext4, approximately 1536 MiB
p4                   persistent data, mounted at /data
```

The selected root contains its own kernel, DTB, `uEnv.txt`, and root filesystem:

```text
/boot/microfx-imx6dl-dg1.img
/boot/microfx-imx6dl-dg1.dtb
/boot/uEnv.txt
```

At runtime `/dev/mmcblk0p2` means slot A and `/dev/mmcblk0p3` means slot B.
Application projects, revisions, SSH host keys, saved network configuration,
clock seed, recovery counters, and active-release links live on p4 under
`/data`, so they survive a root-slot change.

The generated `.rootfs` is not a whole-card image. A blank replacement SD card
will not boot from `install-full-sd.sh` alone because it lacks the proven raw
bootloader area and four-partition layout. Preserve a known-good prepared card
until the independent boot prototype has completed hardware validation.

## Build, verify, and install

Run the portable gate first, then build the target image:

```sh
./tests/run.sh
cd platforms/imx6dl-dg1
./scripts/build.sh
```

Outputs are ignored and written under `artifacts/`:

```text
microfx-imx6dl-dg1.rootfs       1536 MiB ext4 root image
microfx-imx6dl-dg1.rootfs.gz    compressed image
microfx-imx6dl-dg1.rootfs.md5   transfer check
SHA256SUMS                      authoritative local artifact checks
```

To update both slots on an already prepared 4 GB card:

```sh
diskutil list
sudo ./scripts/install-full-sd.sh /dev/diskN
```

The installer refuses non-external media, the wrong capacity/layout, an
unexpected image size, or failed checksums. It unmounts, writes p2 and p3,
syncs, and ejects. It deliberately leaves p1, p4, and the raw bootloader area
untouched.

## SSH development workflow

The last observed development address was `192.168.3.109`, but DHCP can change
it. Confirm the current address in the router before treating that value as
authoritative. `CANVAS_HOST` can provide the default for scripts that support
it.

```sh
cd platforms/imx6dl-dg1
./scripts/canvas-ssh.sh 192.168.3.109
./scripts/canvas-upload.sh 192.168.3.109
./scripts/project-upload.sh 192.168.3.109 demo-scene
./scripts/bundled-projects-upload.sh 192.168.3.109
./scripts/studio-upload.sh 192.168.3.109
./scripts/canvas-screenshot.sh 192.168.3.109
./scripts/hardware-smoke.sh 192.168.3.109
```

Useful read-only device checks:

```sh
./scripts/canvas-ssh.sh 192.168.3.109 'cat /proc/cmdline'
./scripts/canvas-ssh.sh 192.168.3.109 'cat /run/microfx-status'
./scripts/canvas-ssh.sh 192.168.3.109 'mount | grep " /data "'
./scripts/canvas-ssh.sh 192.168.3.109 'ip route show default'
./scripts/canvas-ssh.sh 192.168.3.109 'readlink /data/apps/current'
./scripts/canvas-ssh.sh 192.168.3.109 'cat /data/config/device-identity.conf; cat /data/config/peer-id'
./scripts/canvas-ssh.sh 192.168.3.109 'tail -100 /tmp/canvas.log'
```

The application uploader compiles from the current checkout, stages a release
below `/data/apps/incoming`, verifies SHA-256 on the device, then atomically
activates it. If upload validation fails, the incoming release is not selected.
If the uploaded JavaScript fails activation, HDMI shows the firmware error app
with the QuickJS message/line while the supervisor waits for the next Save &
Run request. The generated setup identity persists in
`/data/config/device-identity.conf`; a portal-edited peer ID in
`/data/config/peer-id` is intentionally not regenerated.

Run the runtime upload before a project that uses a newly added API. The project
uploader updates `main.js`, metadata, and the complete asset tree without
disturbing revision history; when the project is active it stops the renderer,
checks that the replacement stays alive, and restores the saved files on
failure. The Studio uploader is separate because it replaces active-root web
and service files, not the renderer release.

## Recovery expectations

Development images start key-only Dropbear early. With `CANVAS_SSH=1`, normal
client Wi-Fi gets four clean association rounds at its original boot position.
The RAM-only recovery guardian waits 120 seconds. After repeated failure it
stops competing Wi-Fi processes and runs the frozen stored-network recovery
sequence on `wlan1`; it never creates an access point. Failed bounded recovery
returns ownership to normal Wi-Fi. Two consecutive boots that do not reach the
three-minute stable marker suppress graphics on the next boot while normal
networking and SSH recover.

If the application breaks but SSH works, upload a known-good app or inspect
`/tmp/canvas.log`. If userspace service files are known to be stale but Wi-Fi
and SSH are healthy, use `install-active-root-ssh.sh`; retain the printed backup
path. If SSH cannot be recovered, write the checksummed full root image to both
slots with `install-full-sd.sh`. Do not maintain one-off `debugfs` patch payloads
outside Git: they become stale and are less reliable than the complete image.

## Current development baseline

`V1-STATUS.md` is the evidence map and explicitly separates host-verified work
from hardware gates. At the 2026-08-14 handover, the last observed device used
slot B (`/dev/mmcblk0p3`), had `/data` mounted, client Wi-Fi and SSH running,
and used the current demo release. That snapshot is diagnostic history, not a
guarantee after a reboot or DHCP change. Slot selection and live health must be
read again from the device.

## Script inventory

| Script | Purpose | Writes device state |
| --- | --- | --- |
| `setup-build-vm.sh` | Reproducibly create/provision the pinned Lima VM | No |
| `test-vm.sh` | Cross-build the engine/app from the current checkout | No |
| `build.sh` | Produce and checksum the complete root image | No |
| `canvas-ssh.sh` | Key-only SSH wrapper | Only the supplied command |
| `canvas-upload.sh` | Atomic application release build/upload | `/data/apps` |
| `project-upload.sh` | Backed-up demo project/API and asset update | `/data/apps/projects` |
| `bundled-projects-upload.sh` | Update every bundled example, preserving user projects | `/data/apps/projects` |
| `studio-upload.sh` | Cross-built Studio and peer bridge update | Active root + backup on `/data` |
| `canvas-screenshot.sh` | Retrieve a framebuffer screenshot | Target `/tmp`, host artifacts |
| `canvas-profile.sh` | Toggle/read RAM-only renderer profiling | `/run` only |
| `canvas-benchmark.sh` | Run volatile benchmark profiles | `/run`, host artifacts |
| `hardware-smoke.sh` | Collect read-only hardware evidence | Host artifacts |
| `hardware-acceptance.sh` | Evaluate collected evidence | Host artifacts |
| `install-active-root-ssh.sh` | Whitelisted, backed-up userspace hotfix | Active root + backup on `/data` |
| `install-network-hardening-ssh.sh` | Whitelisted network/recovery hotfix | Active root + backup on `/data` |
| `install-full-sd.sh` | Write the complete image to p2 and p3 | Destructive root-slot write |
| `inspect-development-sd.sh` | Inspect an inserted prepared card | No |

The DRM plane probe, GPU clock, and independent bootloader tooling are explicit
experiments. They are not prerequisites for ordinary application development.
