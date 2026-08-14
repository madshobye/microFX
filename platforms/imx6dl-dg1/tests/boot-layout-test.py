#!/usr/bin/env python3

import copy
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "bootloader" / "validate-layout.py"
SPEC = importlib.util.spec_from_file_location("microfx_boot_layout", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)

layout = json.loads((ROOT / "bootloader" / "layout.json").read_text())
disk = 4096 * 1024 * 1024
resolved = MODULE.validate(layout, disk)
assert [item["name"] for item in resolved] == ["boot", "root-a", "root-b", "data"]
assert resolved[-1]["endBytes"] == disk
assert resolved[-1]["sizeBytes"] == disk - resolved[-1]["startBytes"]
assert len({item["label"] for item in resolved}) == 4

def rejected(mutator, message):
    candidate = copy.deepcopy(layout)
    mutator(candidate)
    try:
        MODULE.validate(candidate, disk)
    except ValueError:
        return
    raise AssertionError(message)


rejected(lambda value: value["rawBoot"].update(maximumImageBytes=9 * 1024 * 1024),
         "bootloader overlap was accepted")
rejected(lambda value: value["partitions"][1].update(startBytes=64 * 1024 * 1024),
         "partition overlap was accepted")
rejected(lambda value: value["partitions"][1].update(startBytes=80 * 1024 * 1024),
         "unowned partition gap was accepted")
rejected(lambda value: value["partitions"].append(copy.deepcopy(value["partitions"][-1])),
         "fifth MBR partition was accepted")
rejected(lambda value: value["partitions"][2].update(label="vendor-root"),
         "non-owned label was accepted")

try:
    MODULE.validate(layout, int(layout["minimumDiskBytes"]) - 512)
except ValueError:
    pass
else:
    raise AssertionError("undersized disk was accepted")

print("independent boot layout tests passed")
