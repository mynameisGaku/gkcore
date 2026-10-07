#!/usr/bin/env python3
"""内蔵モデルシェーダーの検査用ファイルを、照合情報を残して生成する。"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


# 開発用のビルド機構を共有するプロジェクトの場所。
PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))
from build_gkcore_shaders import copy_compatible_fsl, copy_fsl_inputs, shader_command
from forge_checkout import validate_checkout
from verify_dxc import verify as verify_dxc


def compile_fixtures(forge_root, dxc_root, output_dir, build_dir):
    """固定コンパイラーで検査情報を保持し、モデル用の2ファイルだけを保存する。"""
    validate_checkout(forge_root)
    verify_dxc(dxc_root)
    build_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)
    # FSLが使うコンパイラーと文字コードを固定する。
    environment = os.environ.copy()
    environment["FSL_COMPILER_DXC"] = str(dxc_root / "bin" / "x64")
    environment["PYTHONUTF8"] = "1"
    with tempfile.TemporaryDirectory(prefix="gkcore-model-reflection-") as temporary:
        # 固定依存物を変更せず、通常のビルドと同じ互換処理を共有する。
        temporary_root = Path(temporary)
        inputs = temporary_root / "shaders"
        copy_fsl_inputs(PROJECT_ROOT / "shaders", inputs)
        fsl_script = copy_compatible_fsl(forge_root, temporary_root / "fsl-compat")
        command = shader_command(Path(sys.executable), forge_root, dxc_root, build_dir, inputs / "shaders.list", fsl_script)
        # 通常の配布用出力では削られるbinding名とbuffer情報を、検査用に残す。
        command.append("--debug")
        subprocess.run(command, check=True, cwd=PROJECT_ROOT, env=environment)
    for name in ("gkcore_model.vert", "gkcore_model.frag"):
        # shader契約テストが参照する検査専用の成果物。
        source = build_dir / "Binaries" / "DIRECT3D12" / name
        if not source.is_file():
            raise RuntimeError(f"model shader fixture was not generated: {source}")
        shutil.copy2(source, output_dir / name)
        print(f"Saved reflection fixture: {output_dir / name}")


def main():
    """検査用のshader出力先と開発依存物を指定して生成する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--forge-root", required=True, type=Path)
    parser.add_argument("--dxc-root", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=PROJECT_ROOT / "build" / "model-shader-reflection")
    args = parser.parse_args()
    compile_fixtures(args.forge_root.resolve(), args.dxc_root.resolve(), args.output_dir.resolve(), args.build_dir.resolve())


if __name__ == "__main__":
    main()
