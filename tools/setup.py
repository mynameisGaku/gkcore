#!/usr/bin/env python3
"""Prepare and build the complete Windows development tree for gkcore."""
from __future__ import annotations

import os
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import shutil
import subprocess
import sys

from build_forge import WINDOWS_SDK_VERSION, verify_windows_sdk
from forge_checkout import ForgeCheckoutError

ROOT = Path(__file__).resolve().parents[1]
DEVTOOLS = ROOT / ".devtools"
FORGE = DEVTOOLS / "The-Forge"
DXC = DEVTOOLS / "dxc-1.8.2405"
FORGE_BUILD = ROOT / "build" / "forge"
SHADER_BUILD = ROOT / "build" / "runtime-windows" / "gkcore_shaders"
BUILD = ROOT / "build" / "runtime-windows"


class SetupError(RuntimeError):
    pass


@dataclass(frozen=True)
class WindowsToolchain:
    """選択したVisual Studio instanceと、そのinstanceで使うビルド設定。"""

    installation_path: Path  # v142とMSBuildを同じ場所から使う。
    msbuild_path: Path  # Forgeのビルドに使うMSBuild。
    generator: str  # CMakeが使うVisual Studio generator。
    minimum_cmake: tuple[int, int]  # instanceごとに必要なCMakeの最低バージョン。
    windows_sdk_version: str = WINDOWS_SDK_VERSION  # 固定Forgeとgkcoreで共通に使うSDK。


def require_windows_toolchain() -> WindowsToolchain:
    if sys.version_info < (3, 9):
        raise SetupError("Python 3.9 or newer is required for the development setup scripts.")
    if os.name != "nt":
        raise SetupError("The distributable Windows Runtime requires Windows 10/11 and an x64 host.")
    program_files = os.environ.get("ProgramFiles(x86)")
    vswhere = Path(program_files) / "Microsoft Visual Studio/Installer/vswhere.exe" if program_files else None
    if not vswhere or not vswhere.is_file():
        raise SetupError("Visual Studio 2022 or 2026 with the C++ desktop workload is required (vswhere.exe was not found).")

    supported_versions = (
        ("[18.0,19.0)", "Visual Studio 18 2026", (4, 2)),
        ("[17.0,18.0)", "Visual Studio 17 2022", (3, 21)),
    )
    installations_found = False
    v142_found = False
    candidates = []
    for version_range, generator, minimum_cmake in supported_versions:
        result = subprocess.run(
            [str(vswhere), "-all", "-products", "*", "-version", version_range,
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"], text=True, capture_output=True, check=False)
        installations = [Path(line.strip()) for line in result.stdout.splitlines() if line.strip()] if result.returncode == 0 else []
        installations_found = installations_found or bool(installations)
        for installation in installations:
            compiler = list((installation / "VC" / "Tools" / "MSVC").glob("14.29.*/bin/Hostx64/x64/cl.exe"))
            if not compiler:
                continue
            v142_found = True
            msbuild = installation / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
            if msbuild.is_file():
                candidates.append(WindowsToolchain(installation, msbuild, generator, minimum_cmake,
                                                   WINDOWS_SDK_VERSION))

    if not installations_found:
        raise SetupError("Install Visual Studio 2022 or 2026 with the 'Desktop development with C++' workload.")
    if not v142_found:
        raise SetupError(
            "The pinned The Forge revision requires the v142 14.29 (MSVC 1929) toolset in a Visual Studio 2022 or 2026 instance. "
            "In Visual Studio Installer, add 'C++ v14.29 (16.11) build tools (v142)' to a supported instance.")
    if not candidates:
        raise SetupError("MSBuild.exe is missing from every Visual Studio 2022 or 2026 instance that has v142 14.29 installed.")

    sdk_root = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10"
    try:
        verify_windows_sdk(sdk_root)
    except ForgeCheckoutError as exc:
        raise SetupError(str(exc)) from exc
    cmake = shutil.which("cmake")
    if not cmake:
        raise SetupError("CMake 3.21 or newer is required for Visual Studio 2022; CMake 4.2 or newer is required for Visual Studio 2026.")
    version_result = subprocess.run([cmake, "--version"], text=True, capture_output=True, check=False)
    match = re.search(r"cmake version (\d+)\.(\d+)", version_result.stdout)
    if version_result.returncode != 0 or not match:
        raise SetupError("The installed CMake version could not be determined.")
    cmake_version = tuple(map(int, match.groups()))
    compatible = [candidate for candidate in candidates if cmake_version >= candidate.minimum_cmake]
    if not compatible:
        requirements = []
        for candidate in candidates:
            requirement = f"CMake {candidate.minimum_cmake[0]}.{candidate.minimum_cmake[1]} or newer for {candidate.generator}"
            if requirement not in requirements:
                requirements.append(requirement)
        detected = f"{cmake_version[0]}.{cmake_version[1]}"
        raise SetupError(f"CMake {detected} is too old for the installed Visual Studio instance; {'; '.join(requirements)}.")
    return compatible[0]


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
    toolchain = require_windows_toolchain()
    ensure_dependencies()
    run([sys.executable, str(ROOT / "tools/build_forge.py"), "--forge-root", str(FORGE),
         "--dxc-root", str(DXC), "--build-dir", str(FORGE_BUILD), "--msbuild", str(toolchain.msbuild_path)])
    run([sys.executable, str(ROOT / "tools/build_gkcore_shaders.py"), "--forge-root", str(FORGE),
         "--dxc-root", str(DXC), "--output-dir", str(SHADER_BUILD)])
    configure = ["cmake", "-S", str(ROOT), "-B", str(BUILD), "-G", toolchain.generator,
                 "-A", "x64", "-T", "v142", f"-DCMAKE_GENERATOR_INSTANCE={toolchain.installation_path}",
                 f"-DCMAKE_SYSTEM_VERSION={toolchain.windows_sdk_version}",
                 f"-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION_MAXIMUM={toolchain.windows_sdk_version}",
                 "-DGKCORE_BUILD_TESTS=ON", "-DGKCORE_BUILD_RUNTIME=ON",
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
