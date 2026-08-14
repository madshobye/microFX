#!/usr/bin/env python3
"""Evaluate a read-only hardware-smoke report without contacting the device."""

from __future__ import annotations

import pathlib
import re
import sys


def parse_report(path: pathlib.Path) -> dict[str, dict[str, str]]:
    sections: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for raw_line in path.read_text(errors="replace").splitlines():
        line = raw_line.strip()
        if line.startswith("=====") and line.endswith("====="):
            name = line.removeprefix("=====").removesuffix("=====").strip()
            current = sections.setdefault(name, {})
        elif current is not None and "=" in line:
            key, value = line.split("=", 1)
            current[key.strip()] = value.strip()
    return sections


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {pathlib.Path(sys.argv[0]).name} REPORT", file=sys.stderr)
        return 2
    report = pathlib.Path(sys.argv[1])
    if not report.is_file():
        print(f"missing smoke report: {report}", file=sys.stderr)
        return 2

    section = parse_report(report)
    client_interface = section.get("wifi_client", {}).get("client_interface", "")
    provision_interface = section.get("setup_ap", {}).get("provision_interface", "")
    checks = [
        ("clock synchronized", section.get("identity", {}).get("clock_sane") == "yes"),
        ("known A/B root slot", section.get("identity", {}).get("root_slot") in {"2", "3"}),
        ("current image schema", section.get("identity", {}).get("image_schema") == "1"),
        ("current image platform", section.get("identity", {}).get("image_platform") == "imx6dl-dg1"),
        ("authoritative boot model", section.get("identity", {}).get("image_boot_model") == "existing-ab"),
        ("persistent partition mounted", section.get("persistence", {}).get("data_device", "missing") != "missing"),
        ("project store present", section.get("persistence", {}).get("/data/apps/projects") == "present"),
        ("configuration store present", section.get("persistence", {}).get("/data/config") == "present"),
        ("state store present", section.get("persistence", {}).get("/data/state") == "present"),
        ("renderer running", section.get("renderer", {}).get("renderer") == "running"),
        (
            "client and setup radios distinct",
            bool(client_interface)
            and bool(provision_interface)
            and client_interface != provision_interface,
        ),
        ("client Wi-Fi associated", section.get("wifi_client", {}).get("wpa_state") == "COMPLETED"),
        ("setup hostapd running", section.get("setup_ap", {}).get("hostapd") == "running"),
        ("setup dnsmasq running", section.get("setup_ap", {}).get("dnsmasq") == "running"),
        ("setup HTTP running", section.get("setup_ap", {}).get("httpd") == "running"),
        ("setup beacon enabled", section.get("setup_ap", {}).get("state") == "ENABLED"),
        ("portal HTTP healthy", section.get("setup_ap", {}).get("portal_http") == "ok"),
        ("peer bridge running", section.get("services", {}).get("peer_bridge") == "running"),
        ("time synchronization usable", section.get("services", {}).get("time_sync") == "valid"),
        ("AR6003 board data installed", section.get("firmware_diagnostics", {}).get("board_data_file") == "present"),
        ("AR6003 board data target explicit", section.get("firmware_diagnostics", {}).get("board_data_target") == "bdata.SD31.bin"),
        ("AR6003 board data size valid", section.get("firmware_diagnostics", {}).get("board_data_bytes") == "1792"),
        (
            "AR6003 board data checksum recorded",
            re.fullmatch(
                r"[0-9a-f]{64}",
                section.get("firmware_diagnostics", {}).get("board_data_sha256", ""),
            )
            is not None,
        ),
        ("AR6003 board data loaded without fallback", section.get("firmware_diagnostics", {}).get("board_data_fallback_warnings") == "0"),
    ]

    failed = 0
    for label, passed in checks:
        print(f"{'PASS' if passed else 'FAIL'} {label}")
        failed += not passed
    print(f"RESULT {'pass' if failed == 0 else 'fail'} checks={len(checks)} failed={failed}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
