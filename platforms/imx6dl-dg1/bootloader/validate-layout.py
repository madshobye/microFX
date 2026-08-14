#!/usr/bin/env python3
"""Validate and display the isolated independent-boot partition contract.

This tool deliberately does not create an image, partition a disk, invoke
genimage, or consume firmware artifacts. It only validates JSON arithmetic.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def validate(layout: dict, disk_bytes: int) -> list[dict]:
    sector = int(layout["sectorBytes"])
    alignment = int(layout["alignmentBytes"])
    minimum_disk = int(layout["minimumDiskBytes"])
    require(layout.get("schema") == 1, "unsupported layout schema")
    require(sector == 512, "i.MX6 SD prototype requires 512-byte sectors")
    require(alignment >= sector and alignment % sector == 0, "invalid alignment")
    require(disk_bytes >= minimum_disk, "disk is smaller than the minimum layout")

    raw = layout["rawBoot"]
    raw_start = int(raw["startBytes"])
    raw_size = int(raw["sizeBytes"])
    image_offset = int(raw["imageOffsetBytes"])
    image_maximum = int(raw["maximumImageBytes"])
    require(raw_start == 0, "raw boot region must start at byte zero")
    require(image_offset == 1024, "i.MX6 boot image offset must be 1024 bytes")
    require(image_offset % sector == 0, "boot image offset is not sector aligned")
    require(image_offset + image_maximum <= raw_size,
            "maximum bootloader image overlaps the first partition")

    source = layout["partitions"]
    require(len(source) == 4, "MBR layout must contain exactly four primary partitions")
    require(len({item["name"] for item in source}) == len(source), "duplicate partition name")
    require(len({item["label"] for item in source}) == len(source), "duplicate filesystem label")

    resolved = []
    previous_end = raw_start + raw_size
    for index, item in enumerate(source):
        start = int(item["startBytes"])
        require(start % alignment == 0, f"{item['name']} start is not aligned")
        require(start == previous_end,
                f"{item['name']} must begin where the previous owned region ends")
        require(item.get("filesystem") == "ext4", f"{item['name']} has unsupported filesystem")
        require(str(item.get("label", "")).startswith("microfx-"),
                f"{item['name']} lacks an owned stable label")
        if item.get("growToEnd"):
            require(index == len(source) - 1, "only the final partition may grow")
            size = disk_bytes - start
            require(size >= int(item["minimumSizeBytes"]),
                    f"{item['name']} is below its minimum size")
        else:
            size = int(item["sizeBytes"])
        require(size > 0 and size % sector == 0, f"{item['name']} has invalid size")
        require(start + size <= disk_bytes, f"{item['name']} exceeds the disk")
        resolved.append({**item, "sizeBytes": size, "endBytes": start + size})
        previous_end = start + size
    require(previous_end == disk_bytes, "layout does not own the complete disk")
    return resolved


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("layout", nargs="?", type=Path,
                        default=Path(__file__).with_name("layout.json"))
    parser.add_argument("--disk-mib", type=int, default=4096)
    args = parser.parse_args()
    layout = json.loads(args.layout.read_text(encoding="utf-8"))
    disk_bytes = args.disk_mib * 1024 * 1024
    partitions = validate(layout, disk_bytes)
    print(f"disk={disk_bytes} bytes sectors={disk_bytes // layout['sectorBytes']}")
    raw = layout["rawBoot"]
    print(f"raw-boot start={raw['startBytes']} size={raw['sizeBytes']} "
          f"image-offset={raw['imageOffsetBytes']} max-image={raw['maximumImageBytes']}")
    for item in partitions:
        print(f"{item['name']} label={item['label']} start={item['startBytes']} "
              f"size={item['sizeBytes']} end={item['endBytes']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
