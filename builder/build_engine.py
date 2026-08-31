#!/usr/bin/env python3
"""
DemonEngine automatic MSVC build tool.

Default project:
    D:\DemonEngine_SDK_v1.2\DemonEngine

Uses:
    x64 MSVC
    CMake
    Ninja Multi-Config

Examples:
    python build_engine.py
    python build_engine.py --release
    python build_engine.py --clean
    python build_engine.py --configure-only
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

DEFAULT_ENGINE_DIR = Path(r"D:\DemonEngine_SDK_v1.2\DemonEngine")
DEFAULT_BUILD_NAME = "build-msvc"

GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
CYAN = "\033[96m"
RESET = "\033[0m"


def ok(message: str) -> None:
    print(f"{GREEN}[OK] {message}{RESET}")


def warn(message: str) -> None:
    print(f"{YELLOW}[WARN] {message}{RESET}")


def fail(message: str) -> None:
    print(f"{RED}[ERROR] {message}{RESET}")


def info(message: str) -> None:
    print(f"{CYAN}{message}{RESET}")


@dataclass(frozen=True)
class VisualStudioInstallation:
    path: Path
    installation_version: str
    product_line_version: str
    display_name: str

    @property
    def major(self) -> int | None:
        try:
            return int(self.product_line_version.split(".", 1)[0])
        except (AttributeError, ValueError):
            return None


def run_capture(command: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        text=True,
        capture_output=True,
        check=False,
        env=env,
    )


def find_vswhere() -> Path | None:
    pfx86 = os.environ.get("ProgramFiles(x86)")
    if pfx86:
        candidate = Path(pfx86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        if candidate.is_file():
            return candidate

    found = shutil.which("vswhere.exe")
    return Path(found) if found else None


def find_visual_studio() -> VisualStudioInstallation | None:
    vswhere = find_vswhere()
    if vswhere is None:
        fail("MSVC check failed: vswhere.exe was not found.")
        fail("Install Visual Studio with the Desktop C++ / MSVC x64 tools.")
        return None

    result = run_capture([
        str(vswhere),
        "-latest",
        "-products", "*",
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-format", "json",
    ])

    if result.returncode != 0 or not result.stdout.strip():
        fail("MSVC check failed: vswhere could not find a usable Visual Studio installation.")
        return None

    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        fail(f"MSVC check failed: invalid vswhere output: {exc}")
        return None

    if not data:
        fail("MSVC check failed: no Visual Studio installation with x64 C++ tools was found.")
        return None

    item = data[0]
    catalog = item.get("catalog", {})
    return VisualStudioInstallation(
        path=Path(item.get("installationPath", "")),
        installation_version=str(item.get("installationVersion", "unknown")),
        product_line_version=str(
            catalog.get("productLineVersion")
            or item.get("productLineVersion")
            or "unknown"
        ),
        display_name=str(item.get("displayName", "Visual Studio")),
    )


def vs_family(vs: VisualStudioInstallation) -> str:
    if vs.major == 17:
        return "Visual Studio 17.x / 2022"
    if vs.major == 18:
        return "Visual Studio 18.x / 2026"
    if vs.major is not None:
        return f"Visual Studio {vs.major}.x"
    return "Visual Studio"


def load_msvc_environment(vs: VisualStudioInstallation) -> dict[str, str] | None:
    vsdevcmd = vs.path / "Common7" / "Tools" / "VsDevCmd.bat"
    if not vsdevcmd.is_file():
        fail(f"MSVC check failed: VsDevCmd.bat not found: {vsdevcmd}")
        return None

    command = f'call "{vsdevcmd}" -arch=x64 -host_arch=x64 >nul && set'
    result = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", command],
        text=True,
        capture_output=True,
        check=False,
    )

    if result.returncode != 0:
        fail("MSVC environment initialization failed.")
        if result.stderr.strip():
            print(result.stderr)
        return None

    env = os.environ.copy()
    for line in result.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            env[key] = value

    if not shutil.which("cl.exe", path=env.get("PATH", "")):
        fail("MSVC check failed: cl.exe was not found after initializing the x64 toolchain.")
        return None

    ok(f"MSVC environment initialized: {vsdevcmd}")
    return env


def validate_tools(env: dict[str, str]) -> bool:
    cl = shutil.which("cl.exe", path=env.get("PATH", ""))
    cmake = shutil.which("cmake.exe", path=env.get("PATH", ""))
    ninja = shutil.which("ninja.exe", path=env.get("PATH", ""))

    if cl:
        ok(f"MSVC compiler found: {cl}")
    else:
        fail("MSVC compiler check failed: cl.exe not found.")
        return False

    if cmake:
        ok(f"CMake found: {cmake}")
    else:
        fail("CMake not found in the MSVC environment.")
        return False

    if ninja:
        ok(f"Ninja found: {ninja}")
    else:
        fail("Ninja not found in the MSVC environment.")
        return False

    version = subprocess.run(
        ["cl.exe"],
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )
    compiler_text = (version.stdout + "\n" + version.stderr).strip()
    compiler_line = next(
        (line.strip() for line in compiler_text.splitlines() if "Microsoft" in line),
        None,
    )

    if compiler_line:
        ok(f"Compiler: {compiler_line}")
    else:
        fail("MSVC compiler responded, but its version could not be validated.")
        return False

    return True


def clean_build_dir(path: Path) -> bool:
    if not path.exists():
        return True
    try:
        shutil.rmtree(path)
        ok(f"Removed build directory: {path}")
        return True
    except OSError as exc:
        fail(f"Could not remove build directory '{path}': {exc}")
        return False


def configure(project_dir: Path, build_dir: Path, env: dict[str, str]) -> bool:
    info("=== CMake Configure ===")
    command = [
        "cmake",
        "-S", str(project_dir),
        "-B", str(build_dir),
        "-G", "Ninja Multi-Config",
        "-DCMAKE_C_COMPILER=cl.exe",
        "-DCMAKE_CXX_COMPILER=cl.exe",
    ]
    result = subprocess.run(
        command,
        cwd=str(project_dir),
        env=env,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        fail(f"CMake configure failed with exit code {result.returncode}.")
        return False
    ok("CMake configure completed.")
    return True


def build(project_dir: Path, build_dir: Path, env: dict[str, str], configuration: str) -> bool:
    info(f"=== DemonEngine {configuration} Build ===")
    result = subprocess.run(
        [
            "cmake",
            "--build", str(build_dir),
            "--config", configuration,
            "--parallel",
        ],
        cwd=str(project_dir),
        env=env,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        fail(f"{configuration} build failed with exit code {result.returncode}.")
        return False
    ok(f"{configuration} build completed successfully.")
    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Automatic DemonEngine MSVC build tool.")
    parser.add_argument("--release", action="store_true", help="Build Release instead of Debug.")
    parser.add_argument("--clean", action="store_true", help="Delete build-msvc before configuring.")
    parser.add_argument("--configure-only", action="store_true", help="Configure only; do not build.")
    parser.add_argument("--project", type=Path, default=DEFAULT_ENGINE_DIR, help="Override the engine directory.")
    parser.add_argument("--build-dir", type=Path, default=None, help="Override the build directory.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_dir = args.project
    build_dir = args.build_dir or (project_dir / DEFAULT_BUILD_NAME)
    configuration = "Release" if args.release else "Debug"

    print(f"{CYAN}=============================================={RESET}")
    print(f"{CYAN}     DemonEngine Automatic MSVC Builder{RESET}")
    print(f"{CYAN}=============================================={RESET}")

    if not project_dir.is_dir():
        fail(f"Engine directory not found: {project_dir}")
        return 1
    ok(f"Engine directory found: {project_dir}")

    vs = find_visual_studio()
    if vs is None:
        return 1
    ok(f"{vs_family(vs)} found | toolchain {vs.installation_version}")

    env = load_msvc_environment(vs)
    if env is None or not validate_tools(env):
        return 1

    if args.clean and not clean_build_dir(build_dir):
        return 1

    if not configure(project_dir, build_dir, env):
        return 1

    if args.configure_only:
        ok("Configure-only operation finished.")
        return 0

    if not build(project_dir, build_dir, env, configuration):
        return 1

    ok("DemonEngine build pipeline finished.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
