"""Optional DemonEngine build defaults."""
from pathlib import Path

ENGINE_ROOT = Path(r"D:\DemonEngine_SDK_v1.2\DemonEngine")
BUILD_DIRECTORY = ENGINE_ROOT / "build-msvc"
GENERATOR = "Ninja Multi-Config"
ARCHITECTURE = "x64"
DEFAULT_CONFIGURATION = "Debug"
