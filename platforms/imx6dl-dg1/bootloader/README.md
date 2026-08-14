# Independent boot prototype

This directory is an isolated feasibility prototype. It is deliberately not
connected to the normal Buildroot configuration or `build.sh`, and nothing in
this directory is installed by the development SD-card scripts.

## Proposed boot chain

```text
i.MX6 ROM
  -> microFX u-boot.imx at SD byte 1024
  -> boot partition (kernel, DTB, boot configuration)
  -> root A or root B
  -> persistent data partition
```

The first prototype uses the standard i.MX6 `u-boot.imx` image rather than an
SPL plus U-Boot pair. The image contains a DCD table that initializes DDR
before U-Boot starts. This keeps the first hardware test small and leaves room
to migrate to SPL later.

## Proposed MBR layout

`layout.json` is the machine-readable authority for the independent-card map.
It assigns microFX-owned filesystem labels and expresses the final data
partition as grow-to-end with a 512 MiB minimum. Validate the arithmetic for a
candidate card size without reading or writing any device:

```sh
./validate-layout.py --disk-mib 4096
```

The validator cannot create an image. It rejects a bootloader overlapping the
first partition, more than four MBR partitions, overlap or gaps, undersized
media, non-aligned regions, duplicate names or labels, and labels outside the
`microfx-` namespace.

The raw bootloader is not a partition. Four primary partitions therefore fit
in an MBR:

| Region | Start | Size | Purpose |
| --- | ---: | ---: | --- |
| ROM/U-Boot area | 0 | 8 MiB | MBR plus `u-boot.imx` at 1 KiB |
| boot | 8 MiB | 64 MiB | kernel, DTB, boot configuration |
| root A | 72 MiB | 1536 MiB | active or candidate root |
| root B | 1608 MiB | 1536 MiB | fallback root |
| data | 3144 MiB | remaining media, at least 512 MiB | projects, revisions, device state |

The later image generator must consume this contract rather than duplicating
the offsets. The existing `genimage-prototype.cfg` illustrates the minimum-size
layout only; it is not authoritative and deliberately is not executable from
the normal build. A future reviewed generator must expand `microfx-data` to the
actual end of the card and mount by label rather than `/dev/mmcblk0pN`.

## Compile-verified board overlay

The repository now contains a small board overlay for pinned upstream U-Boot
2025.01. It configures UART1, USDHC3, the decoded DCD/DDR table, a non-writing
environment, and a bounded selected-slot-then-fallback boot command. The
overlay cross-compiles to `u-boot.imx`; it has not been booted on hardware.

Build it only in a Linux environment with an ARM hard-float cross compiler:

```sh
./build-u-boot-prototype.sh \
  /path/to/u-boot-2025.01 \
  /path/to/arm-buildroot-linux-gnueabihf- \
  /tmp/microfx-u-boot-output
```

The script copies the pristine upstream source to temporary storage before
applying the overlay. Its only durable outputs are `u-boot.imx`, the resolved
configuration, and a checksum in the explicitly supplied output directory. It
uses a stable default `SOURCE_DATE_EPOCH` (which release tooling may override)
and does not download source, modify the normal Buildroot output, create a disk
image, or write an SD card.

## Board facts still required before a hardware test

- Confirmation of the SD-card detect and power GPIO behavior.
- A reviewed last-known-good/commit protocol beyond the current bounded
  selected-slot-then-fallback prototype.
- Confirmation that the board ROM accepts the mapped 1 KiB image offset and
  that the selected U-Boot environment location does not overlap the raw area.
- A serial-console test before writing a complete card.
- Validation of the DDR table on more than one unit.

`imx6dl-dg1-ddr.cfg` records register/value facts decoded from the currently
booting device image. It contains no executable code. Treat it as unverified
board configuration until the independent image passes a memory test.

## Safety boundary

The current Buildroot A/B/SSH development workflow remains authoritative. Do
not add this prototype to the normal defconfig, `post-image.sh`, or install
scripts until a separate SD card and serial console are available for a
deliberate boot test. `tests/prototype-isolation-test.sh` enforces that boundary.
