#!/usr/bin/env python3
"""Compile gkcore's fixed Direct3D 12 shader set with pinned Forge FSL and DXC."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

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
    "gkcore_model_skinning.comp",
    "gkcore_post.vert",
    "gkcore_bloom_extract.frag",
    "gkcore_bloom_blur.frag",
    "gkcore_post_composite.frag",
    "gkcore_fxaa.frag",
    "default.rootsig",
    "compute.rootsig",
)


def shader_command(python: Path, forge_root: Path, dxc_root: Path, output_root: Path,
                   shader_list: Path | None = None, fsl_script: Path | None = None) -> list[str]:
    fsl = fsl_script or forge_root / "Common_3" / "Tools" / "ForgeShadingLanguage" / "fsl.py"
    shader_list = shader_list or ROOT / "shaders" / "shaders.list"
    return [
        str(python), str(fsl), str(shader_list),
        "-d", str(output_root / "Generated"),
        "-b", str(output_root / "Binaries"),
        "-i", str(output_root / "Intermediate"),
        "-l", "DIRECT3D12",
        "-I", str(forge_root / "Common_3" / "Graphics" / "FSL"),
        "--compile",
    ]


def copy_compatible_fsl(forge_root: Path, temporary_root: Path) -> Path:
    """固定FSL一式を一時複製し、Python文字比較の互換修正を加える。"""
    # 固定Forge内のFSL本体とD3D generator。
    source_root = forge_root / "Common_3" / "Tools" / "ForgeShadingLanguage"
    source_generator = source_root / "generators" / "d3d.py"
    # Windows FSL前処理が相対位置から読み込む実行ファイル。
    source_mcpp = forge_root / "Common_3" / "Utilities" / "ThirdParty" / "OpenSource" / "mcpp" / "bin" / "mcpp.exe"
    if not (source_root / "fsl.py").is_file() or not source_generator.is_file() or not source_mcpp.is_file():
        raise ForgeCheckoutError("The pinned FSL tool or its Windows preprocessor dependency is missing")

    # FSLがForgeの相対配置を維持できる一時root。
    compatible_root = temporary_root / "The-Forge"
    compatible_fsl_root = compatible_root / "Common_3" / "Tools" / "ForgeShadingLanguage"
    shutil.copytree(source_root, compatible_fsl_root, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    compatible_generator = compatible_fsl_root / "generators" / "d3d.py"
    # 元ファイルを残したまま互換修正するgeneratorの内容。
    generator_source = compatible_generator.read_bytes()
    # リソース種別4文字のidentity比較だけを値比較へ直す。
    for resource_letter in (b"s", b"b", b"t", b"u"):
        old_comparison = b"resource_type_letter is '" + resource_letter + b"':"
        new_comparison = b"resource_type_letter == '" + resource_letter + b"':"
        if generator_source.count(old_comparison) != 1:
            raise ForgeCheckoutError(
                f"The pinned FSL generator must contain exactly one identity comparison for {resource_letter.decode('ascii')}"
            )
        generator_source = generator_source.replace(old_comparison, new_comparison)
    if b"resource_type_letter is '" in generator_source:
        raise ForgeCheckoutError("The pinned FSL generator contains an unsupported resource-letter identity comparison")
    compatible_generator.write_bytes(generator_source)

    # FSLのWindows前処理が期待する相対位置へ依存物を置く。
    compatible_mcpp = compatible_root / "Common_3" / "Utilities" / "ThirdParty" / "OpenSource" / "mcpp" / "bin" / "mcpp.exe"
    compatible_mcpp.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_mcpp, compatible_mcpp)
    return compatible_fsl_root / "fsl.py"


def copy_fsl_inputs(source_root: Path, destination_root: Path) -> None:
    """BOM付きの元shaderを保ち、FSL入力用コピーだけBOMを除く。"""
    shutil.copytree(source_root, destination_root)
    bom = b"\xef\xbb\xbf"
    for path in destination_root.rglob("*"):
        if not path.is_file():
            continue
        data = path.read_bytes()
        if data.startswith(bom):
            path.write_bytes(data[len(bom):])


def runtime_artifacts(output_root: Path) -> list[Path]:
    return [output_root / "CompiledShaders" / "DIRECT3D12" / name for name in EXPECTED_ARTIFACTS]


def reflection_command(python: Path, dxc_root: Path, output_root: Path) -> list[str]:
    """DXCでpost-composite shaderのtexture配列bindingを検査する。"""
    artifact = output_root / "CompiledShaders" / "DIRECT3D12" / "gkcore_post_composite.frag"
    return [
        str(python), str(ROOT / "tests" / "shader_contract_tests.py"),
        "--artifact", str(artifact),
        "--dxc", str(dxc_root / "bin" / "x64" / "dxc.exe"),
        "--require-reflection", "--post-composite",
    ]


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
    # FSLの文字コードを固定し、環境localeによる失敗を避ける。
    environment["PYTHONUTF8"] = "1"
    print("Compiling gkcore's Direct3D 12 FSL shader set", flush=True)
    with tempfile.TemporaryDirectory(prefix="gkcore-fsl-input-") as temporary:
        normalized_source_root = Path(temporary) / "shaders"
        copy_fsl_inputs(ROOT / "shaders", normalized_source_root)
        fsl_script = copy_compatible_fsl(forge_root, Path(temporary) / "fsl-compat")
        command = shader_command(python, forge_root, dxc_root, output_root,
                                 normalized_source_root / "shaders.list", fsl_script)
        subprocess.run(command, check=True, cwd=ROOT, env=environment)

    source_root = binary_output / "DIRECT3D12"
    destination_root = output_root / "CompiledShaders" / "DIRECT3D12"
    destination_root.mkdir(parents=True, exist_ok=True)
    for name in EXPECTED_ARTIFACTS:
        source = source_root / name
        if not source.is_file() or source.stat().st_size == 0:
            raise ForgeCheckoutError(f"FSL succeeded but required runtime shader artifact is missing: {source}")
        shutil.copy2(source, destination_root / name)
    subprocess.run(reflection_command(python, dxc_root, output_root), check=True, cwd=ROOT)
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
