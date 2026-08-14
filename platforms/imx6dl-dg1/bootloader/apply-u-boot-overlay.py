#!/usr/bin/env python3
"""Apply the isolated microFX board overlay to a pristine U-Boot 2025.01 tree."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


PINNED_VERSION = ("2025", "01")
TARGET_BLOCK = '''config TARGET_MICROFX_IMX6DL_DG1
\tbool "microFX i.MX6DL DG1 boot prototype"
\tdepends on MX6QDL
\tselect BOARD_EARLY_INIT_F
\tselect DM
\tselect DM_GPIO
\tselect DM_MMC
\tselect OF_CONTROL
\timply CMD_DM

'''
SOURCE_LINE = 'source "board/microfx/imx6dl_dg1/Kconfig"\n'


def uboot_version(source: Path) -> tuple[str, str]:
    values: dict[str, str] = {}
    for line in (source / "Makefile").read_text().splitlines():
        if "=" not in line:
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        if key in {"VERSION", "PATCHLEVEL"}:
            values[key] = value
    return values.get("VERSION", ""), values.get("PATCHLEVEL", "")


def insert_once(text: str, marker: str, addition: str) -> str:
    if addition.strip() in text:
        return text
    if marker not in text:
        raise RuntimeError(f"pinned U-Boot insertion marker missing: {marker!r}")
    return text.replace(marker, addition + marker, 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="pristine or previously overlaid U-Boot tree")
    args = parser.parse_args()
    source = args.source.resolve()
    here = Path(__file__).resolve().parent
    overlay = here / "u-boot"
    if uboot_version(source) != PINNED_VERSION:
        raise SystemExit("expected pristine upstream U-Boot 2025.01")

    for path in sorted(overlay.rglob("*")):
        if path.is_dir():
            continue
        target = source / path.relative_to(overlay)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)

    # The DCD facts have one authority in this repository.
    shutil.copyfile(
        here / "imx6dl-dg1-ddr.cfg",
        source / "board/microfx/imx6dl_dg1/imximage.cfg",
    )

    kconfig = source / "arch/arm/mach-imx/mx6/Kconfig"
    text = kconfig.read_text()
    text = insert_once(text, "config TARGET_MX6Q_ENGICAM\n", TARGET_BLOCK)
    text = insert_once(text, 'source "board/boundary/nitrogen6x/Kconfig"\n', SOURCE_LINE)
    kconfig.write_text(text)
    print(f"microFX U-Boot overlay applied to {source}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
