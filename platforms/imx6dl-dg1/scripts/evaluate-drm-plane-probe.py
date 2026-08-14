#!/usr/bin/env python3
"""Turn a read-only DRM probe capture into a conservative capability decision."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


RGB_FORMATS = {"XR24", "AR24", "XB24", "AB24", "RG16", "XR30", "AR30"}
REQUIRED_EXPERIMENTS = ("scaling", "dmaBufImport", "atomicCommit", "pageFlipSync")


def parse_yes_no(value: str) -> bool | None:
    value = value.strip().lower()
    if value in {"yes", "true", "pass", "1"}:
        return True
    if value in {"no", "false", "fail", "0"}:
        return False
    return None


def parse_probe(text: str) -> dict:
    in_imx_planes = False
    planes: list[dict] = []
    current: dict | None = None
    current_property: str | None = None
    evidence: dict[str, bool | None] = {name: None for name in REQUIRED_EXPERIMENTS}

    for raw in text.splitlines():
        line = raw.rstrip()
        if line == "===== modetest imx-drm planes =====":
            in_imx_planes = True
            continue
        if line.startswith("=====") and in_imx_planes:
            in_imx_planes = False
            current = None
            current_property = None

        match = re.match(r"^MICROFX_EVIDENCE\s+([A-Za-z][A-Za-z0-9]*)=(\S+)\s*$", line)
        if match and match.group(1) in evidence:
            evidence[match.group(1)] = parse_yes_no(match.group(2))
            continue

        if not in_imx_planes:
            continue
        match = re.match(r"^\s*(\d+)\s+\d+\s+\d+\s+[-0-9,]+\s+\S+", line)
        if match:
            current = {
                "id": int(match.group(1)),
                "type": "unknown",
                "formats": [],
                "properties": {},
            }
            planes.append(current)
            current_property = None
            continue
        if current is None:
            continue
        match = re.match(r"^\s*formats:\s*(.*)$", line)
        if match:
            current["formats"] = [token.upper() for token in match.group(1).split()]
            continue
        match = re.match(r"^\s*\d+\s+([A-Za-z0-9_-]+):\s*$", line)
        if match:
            current_property = match.group(1)
            current["properties"][current_property] = {"enums": {}}
            continue
        if current_property is None:
            continue
        match = re.match(r"^\s*enums:\s*(.*)$", line)
        if match:
            for name, value in re.findall(r"([A-Za-z]+)=(-?\d+)", match.group(1)):
                current["properties"][current_property]["enums"][name] = int(value)
            continue
        match = re.match(r"^\s*value:\s*(-?\d+)\s*$", line)
        if match:
            value = int(match.group(1))
            prop = current["properties"][current_property]
            prop["value"] = value
            if current_property.lower() == "type":
                current["type"] = next(
                    (name.lower() for name, number in prop["enums"].items() if number == value),
                    "unknown",
                )

    overlays = [plane for plane in planes if plane["type"] == "overlay"]
    rgb_overlays = [plane for plane in overlays if RGB_FORMATS.intersection(plane["formats"])]
    property_names = {
        name.lower()
        for plane in overlays
        for name in plane["properties"]
    }
    alpha = bool(property_names.intersection({"alpha", "global_alpha", "global-alpha"}))
    z_position = bool(property_names.intersection({"zpos", "z_position", "z-position"}))
    structural_candidate = bool(rgb_overlays and z_position)
    experiment_ready = structural_candidate and all(evidence[name] is True for name in REQUIRED_EXPERIMENTS)

    blockers: list[str] = []
    if not overlays:
        blockers.append("no imx-drm overlay plane was identified")
    elif not rgb_overlays:
        blockers.append("no overlay plane advertises a supported RGB format")
    if overlays and not z_position:
        blockers.append("overlay z-position control was not identified")
    for name in REQUIRED_EXPERIMENTS:
        if evidence[name] is not True:
            blockers.append(f"{name} has not passed an active display experiment")

    return {
        "schema": 1,
        "driver": "imx-drm",
        "planes": planes,
        "summary": {
            "planeCount": len(planes),
            "overlayPlaneCount": len(overlays),
            "rgbOverlayPlaneCount": len(rgb_overlays),
            "globalAlphaProperty": alpha,
            "zPositionProperty": z_position,
            "structuralCandidate": structural_candidate,
            "experimentReady": experiment_ready,
        },
        "activeEvidence": evidence,
        "decision": "native-plane-experiment" if experiment_ready else "keep-gles-baseline",
        "blockers": blockers,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = parse_probe(args.capture.read_text(errors="replace"))
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered)
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

