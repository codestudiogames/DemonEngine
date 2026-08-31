#!/usr/bin/env python3
"""Standalone DemonEngine MSVC detection/diagnostic."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from pathlib import Path

GREEN = "\033[92m"
RED = "\033[91m"
CYAN = "\033[96m"
RESET = "\033[0m"


def ok(text: str) -> None:
    print(f"{GREEN}[OK] {text}{RESET}")


def fail(text: str) -> None:
    print(f"{RED}[ERROR] {text}{RESET}")


def find_vswhere() -> Path | None:
    pfx86 = os.environ.get("ProgramFiles(x86)")
    if pfx86:
        p = Path(pfx86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        if p.is_file():
            return p
    found = shutil.which("vswhere.exe")
    return Path(found) if found else None


def main() -> int:
    print(f"{CYAN}=== DemonEngine MSVC Check ==={RESET}")

    vswhere = find_vswhere()
    if vswhere is None:
        fail("vswhere.exe not found.")
        return 1

    result = subprocess.run(
        [
            str(vswhere),
            "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-format", "json",
        ],
        text=True,
        capture_output=True,
        check=False,
    )

    if result.returncode != 0:
        fail("vswhere query failed.")
        return 1

    try:
        installs = json.loads(result.stdout)
    except json.JSONDecodeError:
        fail("Could not parse Visual Studio installation information.")
        return 1

    if not installs:
        fail("No MSVC x64 installation found.")
        return 1

    for item in installs:
        path = Path(item.get("installationPath", ""))
        version = item.get("installationVersion", "unknown")
        line = item.get("catalog", {}).get("productLineVersion", "unknown")
        if line == "17":
            family = "Visual Studio 17.x / 2022"
        elif line == "18":
            family = "Visual Studio 18.x / 2026"
        else:
            family = f"Visual Studio {line}"
        ok(f"{family} | {version}")
        ok(f"Installation: {path}")

        vsdevcmd = path / "Common7" / "Tools" / "VsDevCmd.bat"
        if not vsdevcmd.is_file():
            fail(f"VsDevCmd.bat missing: {vsdevcmd}")
            return 1
        ok(f"VsDevCmd.bat: {vsdevcmd}")

    ok("MSVC diagnostic passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
