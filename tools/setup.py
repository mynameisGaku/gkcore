#!/usr/bin/env python3
"""Prepare and build the complete Windows development tree for gkcore."""
from __future__ import annotations

import os
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEVTOOLS = ROOT / ".devtools"
FORGE = DEVTOOLS / "The-Forge"
DXC = DEVTOOLS / "dxc-1.8.2405"
FORGE_BUILD = ROOT / "build" / "forge"
SHADER_BUILD = ROOT / "build" / "runtime-windows" / "gkcore_shaders"
BUILD = ROOT / "build" / "runtime-windows"


class SetupError(RuntimeError):
    pass


def require_windows_toolchain() -> Path:
    if sys.version_info < (3, 9):
        raise SetupError("Python 3.9 or newer is required for the development setup scripts.")
    if os.name != "nt":
        raise SetupError("The distributable Windows Runtime requires Windows 10/11 and an x64 host.")
    program_files = os.environ.get("ProgramFiles(x86)")
    vswhere = Path(program_files) / "Microsoft Visual Studio/Installer/vswhere.exe" if program_files else None
    if not vswhere or not vswhere.is_file():
        raise SetupError("Visual Studio 2022 with the C++ desktop workload is required (vswhere.exe was not found).")
    result = subprocess.run(
        [str(vswhere), "-all", "-products", "*", "-version", "[17.0,18.0)",
         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"], text=True, capture_output=True, check=False)
    installations = [Path(line.strip()) for line in result.stdout.splitlines() if line.strip()] if result.returncode == 0 else []
    if not installations:
        raise SetupError("Install Visual Studio 2022 with the 'Desktop development with C++' workload.")
    selected = next((path for path in installations
                     if list((path / "VC" / "Tools" / "MSVC").glob("14.29.*/bin/Hostx64/x64/cl.exe"))), None)
    if not selected:
        raise SetupError(
            "This pinned The Forge revision requires the VS 2019 v142 14.29 (MSVC 1929) toolset. "
            "In Visual Studio Installer, add 'C++ v14.29 (16.11) build tools (v142)'.")
    sdk_root = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits/10"
    sdk_versions = [version for version in (sdk_root / "Include").glob("10.*")
                    if (version / "um" / "Windows.h").is_file()
                    and (sdk_root / "Lib" / version.name / "um" / "x64").is_dir()]
    if not sdk_versions:
        raise SetupError("A Windows 10 SDK with x64 headers/libraries is required. Add the current Windows 10 SDK in Visual Studio Installer.")
    cmake = shutil.which("cmake")
    if not cmake:
        raise SetupError("CMake 3.21 or newer is required and must be available on PATH (the VS 2022 generator was added in 3.21).")
    version_result = subprocess.run([cmake, "--version"], text=True, capture_output=True, check=False)
    match = re.search(r"cmake version (\d+)\.(\d+)", version_result.stdout)
    if not match or tuple(map(int, match.groups())) < (3, 21):
        raise SetupError("CMake 3.21 or newer is required; update CMake and ensure the new version is on PATH.")
    msbuild = selected / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
    if not msbuild.is_file():
        raise SetupError(f"MSBuild.exe is missing from the selected Visual Studio installation: {msbuild}")
    return msbuild


def run(command: list[str], *, env=None) -> None:
    print("+ " + subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def ctest_command(build_dir: Path, gpu_check: bool = False) -> list[str]:
    """Run every registered test; opt-in GPU smoke joins the suite rather than filtering it."""
    return ["ctest", "--test-dir", str(build_dir), "-C", "Release", "--output-on-failure"]


def ensure_dependencies() -> None:
    if not FORGE.exists():
        run([sys.executable, str(ROOT / "tools/fetch_forge.py"), "--destination", str(FORGE)])
    else:
        run([sys.executable, str(ROOT / "tools/forge_checkout.py"), "--root", str(FORGE)])
    if not DXC.exists():
        run([sys.executable, str(ROOT / "tools/fetch_dxc.py"), "--destination", str(DXC)])
    else:
        run([sys.executable, str(ROOT / "tools/verify_dxc.py"), "--root", str(DXC)])


def prepare(gpu_check: bool = False) -> None:
    msbuild = require_windows_toolchain()
    ensure_dependencies()
    run([sys.executable, str(ROOT / "tools/build_forge.py"), "--forge-root", str(FORGE),
         "--dxc-root", str(DXC), "--build-dir", str(FORGE_BUILD), "--msbuild", str(msbuild)])
    run([sys.executable, str(ROOT / "tools/build_gkcore_shaders.py"), "--forge-root", str(FORGE),
         "--dxc-root", str(DXC), "--output-dir", str(SHADER_BUILD)])
    configure = ["cmake", "-S", str(ROOT), "-B", str(BUILD), "-G", "Visual Studio 17 2022",
                 "-A", "x64", "-T", "v142", "-DGKCORE_BUILD_TESTS=ON", "-DGKCORE_BUILD_RUNTIME=ON",
                 f"-DGKCORE_FORGE_ROOT={FORGE}", f"-DGKCORE_DXC_ROOT={DXC}",
                 f"-DGKCORE_FORGE_BUILD_DIR={FORGE_BUILD}", f"-DGKCORE_SHADER_BUILD_DIR={SHADER_BUILD}",
                 f"-DGKCORE_RUN_BACKEND_SMOKE={'ON' if gpu_check else 'OFF'}"]
    run(configure)
    run(["cmake", "--build", str(BUILD), "--config", "Release", "--parallel"])
    run(ctest_command(BUILD, gpu_check))
    if gpu_check:
        print("BUILD READY: library, tests, sample, and the requested Direct3D 12 GPU smoke check passed.", flush=True)
    else:
        print("BUILD READY: library, tests, and sample compiled; Direct3D 12 rendering was not smoke-tested. "
              "Run PRE_SETUP.bat --gpu-check on a DX12-capable GPU when you want that check.", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gpu-check", action="store_true", help="run the optional Direct3D 12 host-GPU smoke test")
    args = parser.parse_args()
    try:
        prepare(gpu_check=args.gpu_check)
        return 0
    except (OSError, subprocess.CalledProcessError, SetupError) as exc:
        print(f"gkcore setup stopped: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
