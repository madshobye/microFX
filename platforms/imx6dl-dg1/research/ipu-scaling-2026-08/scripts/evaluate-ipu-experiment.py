#!/usr/bin/env python3
"""Evaluate combined KMS, IPU IC, and active scaling-demo evidence."""

from __future__ import annotations

import argparse
import json
import shlex
from pathlib import Path


REQUIRED_EVIDENCE = {
    "primaryScalingTest": "no",
    "overlayScalingTest": "no",
    "xrgbScaleNegotiation": "yes",
    "v4l2DmaBufQueues": "yes",
    "ipuIcCompositionTest": "yes",
    "scaling": "yes",
    "dmaBufImport": "yes",
    "ipuImageConverter": "yes",
    "atomicCommit": "yes",
    "pageFlipSync": "yes",
    "nativeOverlayAbove": "yes",
    "cpuCopyPrimary": "no",
    "globalAlpha": "no",
}


def fields(line: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for token in shlex.split(line)[1:]:
        if "=" in token:
            key, value = token.split("=", 1)
            result[key] = value
    return result


def number(values: dict[str, str], key: str) -> float | None:
    try:
        return float(values[key])
    except (KeyError, ValueError):
        return None


def parse_experiment(text: str, target_fps: float = 30.0) -> dict:
    evidence: dict[str, str] = {}
    capabilities: dict[str, dict[str, str]] = {}
    client_capabilities: dict[str, dict[str, str]] = {}
    planes: list[dict[str, str]] = []
    preferred_modes: list[dict[str, str]] = []
    profile: dict[str, str] = {}

    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("MICROFX_EVIDENCE "):
            values = fields(line)
            evidence.update(values)
        elif line.startswith("CLIENT_CAP "):
            values = fields(line)
            if "name" in values:
                client_capabilities[values["name"]] = values
        elif line.startswith("CAP "):
            values = fields(line)
            if "name" in values:
                capabilities[values["name"]] = values
        elif line.startswith("PLANE "):
            planes.append(fields(line))
        elif line.startswith("MODE "):
            values = fields(line)
            if values.get("preferred") == "yes":
                preferred_modes.append(values)
        elif line.startswith("PROFILE "):
            profile = fields(line)

    blockers: list[str] = []
    if client_capabilities.get("atomic", {}).get("supported") != "yes":
        blockers.append("atomic KMS client capability was not proven")
    prime = capabilities.get("prime", {})
    try:
        prime_value = int(prime.get("value", "0"), 0)
    except ValueError:
        prime_value = 0
    if prime.get("supported") != "yes" or prime_value & 3 != 3:
        blockers.append("DRM PRIME import and export were not both proven")
    if not any(plane.get("type") == "primary" for plane in planes):
        blockers.append("no primary plane was recorded")
    if not any(plane.get("type") == "overlay" for plane in planes):
        blockers.append("no overlay plane was recorded")
    if not preferred_modes:
        blockers.append("no connected-display preferred mode was recorded")

    for name, expected in REQUIRED_EVIDENCE.items():
        actual = evidence.get(name)
        if actual != expected:
            blockers.append(f"{name} expected {expected}, recorded {actual or 'missing'}")

    fps = number(profile, "fps")
    frames = number(profile, "frames")
    conversion_average = number(profile, "ipuConvertAvgMs")
    conversion_maximum = number(profile, "ipuConvertMaxMs")
    cpu_percent = number(profile, "processCpuPercent")
    if fps is None or fps < target_fps - 0.25:
        blockers.append(f"sustained fps did not meet {target_fps - 0.25:.2f}")
    if frames is None or frames < target_fps * 5:
        blockers.append("active profile was shorter than five seconds")
    if conversion_average is None or conversion_average >= 1000.0 / target_fps:
        blockers.append("average IPU conversion exceeded the frame budget")

    viable = not blockers
    return {
        "schema": 1,
        "targetFps": target_fps,
        "decision": "ipu-ic-prototype-viable" if viable else "keep-gles-baseline",
        "viable": viable,
        "preferredModes": preferred_modes,
        "planeCount": len(planes),
        "evidence": evidence,
        "profile": {
            "frames": frames,
            "fps": fps,
            "processCpuPercent": cpu_percent,
            "ipuConvertAvgMs": conversion_average,
            "ipuConvertMaxMs": conversion_maximum,
        },
        "blockers": blockers,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--target-fps", type=float, default=30.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = parse_experiment(args.capture.read_text(errors="replace"), args.target_fps)
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered)
    else:
        print(rendered, end="")
    return 0 if report["viable"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
