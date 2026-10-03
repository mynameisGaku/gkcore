#!/usr/bin/env python3
"""Build the pinned Windows x64 Renderer and OS libraries for gkcore development."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

from forge_checkout import ForgeCheckoutError, validate_checkout
from verify_dxc import verify as verify_dxc


LIBRARY_PROJECTS = ("Renderer", "OS")


def _windows_path(path: Path) -> str:
    return str(path.resolve()).rstrip("\\/") + "\\"


def project_command(msbuild: str, project: Path, project_name: str, build_root: Path,
                    solution_dir: Path, configuration="Release", platform="x64"):
    output = build_root / project_name
    intermediate = build_root / "obj" / project_name
    return [
        msbuild,
        str(project),
        "/m",
        "/t:Build",
        f"/p:Configuration={configuration}",
        f"/p:Platform={platform}",
        "/p:WindowsTargetPlatformVersion=10.0",
        f"/p:SolutionDir={_windows_path(solution_dir)}",
        f"/p:OutDir={_windows_path(output)}",
        f"/p:IntDir={_windows_path(intermediate)}",
    ]


def find_msbuild() -> str:
    direct = shutil.which("MSBuild.exe") or shutil.which("msbuild")
    if direct:
        return direct
    program_files = os.environ.get("ProgramFiles(x86)")
    vswhere = Path(program_files) / "Microsoft Visual Studio/Installer/vswhere.exe" if program_files else None
    if vswhere and vswhere.is_file():
        result = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild",
             "-find", "MSBuild/**/Bin/MSBuild.exe"], text=True, capture_output=True, check=False)
        candidates = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        if result.returncode == 0 and candidates:
            return candidates[0]
    raise ForgeCheckoutError("MSBuild was not found; install Visual Studio 2022 with the C++ desktop workload and MSBuild")


def build(forge_root: Path, build_root: Path, msbuild: str, dxc_root: Path,
          configuration="Release", platform="x64"):
    validate_checkout(forge_root)
    verify_dxc(dxc_root)
    if os.name != "nt":
        raise ForgeCheckoutError("official Forge Renderer/OS projects require Windows and MSVC; cross-building them is unsupported")
    project_root = forge_root / "Examples_3/Unit_Tests/PC_VS2019/Libraries"
    solution_dir = forge_root / "Examples_3/Unit_Tests/PC_VS2019"
    build_root.mkdir(parents=True, exist_ok=True)
    for project_name in LIBRARY_PROJECTS:
        project = project_root / project_name / f"{project_name}.vcxproj"
        command = project_command(msbuild, project, project_name, build_root, solution_dir, configuration, platform)
        print(f"Building official The Forge {project_name} project ({configuration}|{platform})", flush=True)
        environment = os.environ.copy()
        environment["FSL_COMPILER_DXC"] = str((dxc_root / "bin" / "x64").resolve())
        subprocess.run(command, check=True, env=environment)
        library = build_root / project_name / f"{project_name}.lib"
        if not library.is_file():
            raise ForgeCheckoutError(f"MSBuild succeeded but did not create expected library: {library}")
    compiled_shaders = build_root / "OS" / "OS" / "CompiledShaders"
    shaders = [item for item in compiled_shaders.rglob("*") if item.is_file()] if compiled_shaders.is_dir() else []
    if not shaders:
        raise ForgeCheckoutError(
            "The OS project did not produce FSL compiled shaders; check the Windows SDK/FSL compiler build output")
    return build_root / "Renderer/Renderer.lib", build_root / "OS/OS.lib", compiled_shaders


def build_plan(msbuild: str, forge_root: Path, build_root: Path, configuration="Release", platform="x64"):
    projects_root = forge_root / "Examples_3/Unit_Tests/PC_VS2019/Libraries"
    solution_dir = forge_root / "Examples_3/Unit_Tests/PC_VS2019"
    return [
        project_command(msbuild, projects_root / name / f"{name}.vcxproj", name, build_root,
                        solution_dir, configuration, platform)
        for name in LIBRARY_PROJECTS
    ]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--forge-root", required=True, help="verified pinned The Forge source directory")
    parser.add_argument("--build-dir", required=True, help="development-only output directory")
    parser.add_argument("--dxc-root", required=True, help="verified DXC 1.8.2405 directory")
    parser.add_argument("--msbuild", help="MSBuild.exe path (auto-detected from VS2022 when omitted)")
    parser.add_argument("--configuration", default="Release", choices=("Debug", "Release"))
    parser.add_argument("--platform", default="x64", choices=("x64",))
    parser.add_argument("--dry-run", action="store_true", help="print the exact two upstream commands without running them")
    args = parser.parse_args(argv)
    try:
        forge_root = Path(args.forge_root).resolve()
        build_root = Path(args.build_dir).resolve()
        dxc_root = Path(args.dxc_root).resolve()
        validate_checkout(forge_root)
        verify_dxc(dxc_root)
        msbuild = args.msbuild or "MSBuild.exe"
        if args.dry_run:
            for command in build_plan(msbuild, forge_root, build_root, args.configuration, args.platform):
                print(subprocess.list2cmdline(command))
            return 0
        build(forge_root, build_root, args.msbuild or find_msbuild(), dxc_root, args.configuration, args.platform)
        print(f"Forge libraries and compiled FSL shaders are ready in {build_root}")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, ForgeCheckoutError) as exc:
        print(f"The Forge development build failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
