#!/usr/bin/env python3
"""Prove that bridge, Studio, diagnostics, and tests share one command contract."""

from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "services/peer-bridge/protocol.json"
BRIDGE = ROOT / "services/peer-bridge/src/project_protocol.cpp"
BRIDGE_TEST = ROOT / "services/peer-bridge/tests/project_protocol_test.cpp"
EDITOR = ROOT / "web/editor"
CHECK = EDITOR / "interaction-check.js"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def command_branches(source: str) -> set[str]:
    return set(re.findall(r'std::strcmp\(command,\s*"([^"]+)"\)', source))


def editor_requests() -> set[str]:
    commands: set[str] = set()
    for path in EDITOR.rglob("*.js"):
        if "vendor" in path.parts:
            continue
        commands.update(re.findall(r'protocol\.request\(\s*"([^"]+)"',
                                   path.read_text(encoding="utf-8")))
    return commands


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == 1, "unsupported protocol manifest schema")
    version = manifest.get("protocolVersion")
    require(isinstance(version, int) and version > 0, "invalid protocol version")
    commands = manifest.get("commands")
    require(isinstance(commands, list) and commands, "protocol command list is empty")

    names = [entry.get("name") for entry in commands]
    require(all(isinstance(name, str) and name for name in names), "invalid command name")
    require(len(names) == len(set(names)), "duplicate protocol command")
    allowed_access = {"read", "write", "control"}
    for entry in commands:
        require(entry.get("access") in allowed_access,
                f"{entry['name']} has invalid access class")
        require(isinstance(entry.get("response"), str) and entry["response"],
                f"{entry['name']} lacks a response type")
        require(isinstance(entry.get("safeCheck"), bool),
                f"{entry['name']} lacks safeCheck policy")
        require(not entry["safeCheck"] or entry["access"] == "read",
                f"mutating command {entry['name']} cannot be a safe check")

    manifest_names = set(names)
    bridge_source = BRIDGE.read_text(encoding="utf-8")
    bridge_names = command_branches(bridge_source)
    require(bridge_names == manifest_names,
            f"bridge/manifest drift: missing={sorted(manifest_names - bridge_names)} "
            f"undocumented={sorted(bridge_names - manifest_names)}")
    require(f'"protocolVersion", {version}' in bridge_source,
            "system.ping protocol version differs from manifest")

    used_by_editor = editor_requests()
    require(used_by_editor <= manifest_names,
            f"Studio uses undocumented commands: {sorted(used_by_editor - manifest_names)}")

    check_source = CHECK.read_text(encoding="utf-8")
    check_names = set(re.findall(r'protocol\.request\(\s*"([^"]+)"', check_source))
    safe_names = {entry["name"] for entry in commands if entry["safeCheck"]}
    require(check_names == safe_names,
            f"interaction check/manifest drift: expected={sorted(safe_names)} "
            f"actual={sorted(check_names)}")

    test_source = BRIDGE_TEST.read_text(encoding="utf-8")
    exercised = set(re.findall(r'\\?"type\\?":\\?"([^"\\]+)', test_source))
    require(manifest_names <= exercised,
            f"real handler test lacks commands: {sorted(manifest_names - exercised)}")

    print(f"protocol contract tests passed ({len(commands)} commands, "
          f"{len(safe_names)} read-only checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
