#!/usr/bin/env python3
"""Compile gkcore's fixed Direct3D 12 shader set with pinned Forge FSL and DXC."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

from verify_dxc import verify as verify_dxc
from forge_checkout import ForgeCheckoutError, validate_checkout

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_ARTIFACTS = (
    "gkcore_color.vert",
    "gkcore_color.frag",
    "gkcore_sprite.vert",
    "gkcore_sprite.frag",
    "gkcore_model.vert",
    "gkcore_model.frag",
    "gkcore_post.vert",
    "gkcore_bloom_extract.frag",
    "gkcore_bloom_blur.frag",
    "gkcore_post_composite.frag",
    "gkcore_fxaa.frag",
    "default.rootsig",
    "compute.rootsig",
)


def shader_command(python: Path, forge_root: Path, dxc_root: Path, output_root: Path) -> list[str]:
    fsl = forge_root / "Common_3" / "Tools" / "ForgeShadingLanguage" / "fsl.py"
    return [
        str(python), str(fsl), str(ROOT / "shaders" / "shaders.list"),
        "-d", str(output_root / "Generated"),
        "-b", str(output_root / "Binaries"),
        "-i", str(output_root / "Intermediate"),
        "-l", "DIRECT3D12",
        "-I", str(forge_root / "Common_3" / "Graphics" / "FSL"),
        "--compile",
    ]


def runtime_artifacts(output_root: Path) -> list[Path]:
    return [output_root / "CompiledShaders" / "DIRECT3D12" / name for name in EXPECTED_ARTIFACTS]


def compile_shaders(forge_root: Path, dxc_root: Path, output_root: Path, python: Path | None = None) -> list[Path]:
    validate_checkout(forge_root)
    verify_dxc(dxc_root)
    if os.name != "nt":
        raise ForgeCheckoutError("The pinned FSL root-signature compiler requires Windows; shader build is supported on Windows only")
    python = python or Path(sys.executable)
    binary_output = output_root / "Binaries"
    binary_output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["FSL_COMPILER_DXC"] = str((dxc_root / "bin" / "x64").resolve())
    command = shader_command(python, forge_root, dxc_root, output_root)
    print("Compiling gkcore's Direct3D 12 FSL shader set", flush=True)
    subprocess.run(command, check=True, cwd=ROOT, env=environment)

    source_root = binary_output / "DIRECT3D12"
    destination_root = output_root / "CompiledShaders" / "DIRECT3D12"
    destination_root.mkdir(parents=True, exist_ok=True)
    for name in EXPECTED_ARTIFACTS:
        source = source_root / name
        if not source.is_file() or source.stat().st_size == 0:
            raise ForgeCheckoutError(f"FSL succeeded but required runtime shader artifact is missing: {source}")
        shutil.copy2(source, destination_root / name)
    return runtime_artifacts(output_root)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--forge-root", required=True)
    parser.add_argument("--dxc-root", required=True)
    parser.add_argument("--output-dir", required=True, help="development-only shader outputs")
    parser.add_argument("--python", help="Python interpreter used by the pinned FSL tool")
    args = parser.parse_args(argv)
    try:
        artifacts = compile_shaders(Path(args.forge_root).resolve(), Path(args.dxc_root).resolve(),
                                    Path(args.output_dir).resolve(), Path(args.python) if args.python else None)
        print(f"Compiled {len(artifacts)} gkcore shader artifacts into {args.output_dir}")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, ForgeCheckoutError) as exc:
        print(f"gkcore shader build failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
