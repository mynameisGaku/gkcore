#!/usr/bin/env python3
"""Compile one ordinary HLSL pixel shader to gkcore's pinned FSL format."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from verify_dxc import verify as verify_dxc

MAX_ARTIFACT_BYTES = 64 * 1024 * 1024
FSL_HEADER = struct.Struct("<4s8I")
FSL_DERIVATIVE = struct.Struct("<QQQ")
DXBC_HEADER_SIZE = 32
MAX_DXBC_PARTS = 256


class ShaderCompileError(RuntimeError):
    """Compilation failed; its text contains the original DXC diagnostics."""


def _read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def validate_dxil_pixel_container(data: bytes) -> None:
    """Validate DXBC container bounds and require exactly one DXIL pixel program."""
    if len(data) < DXBC_HEADER_SIZE + 4:
        raise ValueError("truncated DXBC container header")
    if data[:4] != b"DXBC":
        raise ValueError("expected DXIL/DXBC container magic 'DXBC'")
    if _read_u32(data, 20) != 1:
        raise ValueError("unsupported DXBC container version")
    if _read_u32(data, 24) != len(data):
        raise ValueError("DXBC declared size does not match container size")

    part_count = _read_u32(data, 28)
    if part_count == 0 or part_count > MAX_DXBC_PARTS:
        raise ValueError("invalid DXBC part count")
    offsets_end = DXBC_HEADER_SIZE + part_count * 4
    if offsets_end > len(data):
        raise ValueError("truncated DXBC part-offset table")

    parts: list[tuple[int, int, bytes]] = []
    for index in range(part_count):
        offset = _read_u32(data, DXBC_HEADER_SIZE + index * 4)
        if offset < offsets_end or offset > len(data) or len(data) - offset < 8:
            raise ValueError(f"DXBC part {index} offset is outside the container")
        payload_size = _read_u32(data, offset + 4)
        if payload_size > len(data) - offset - 8:
            raise ValueError(f"DXBC part {index} payload exceeds the container")
        end = offset + 8 + payload_size
        parts.append((offset, end, data[offset : offset + 4]))

    ordered = sorted(parts)
    if any(left[1] > right[0] for left, right in zip(ordered, ordered[1:])):
        raise ValueError("DXBC container parts overlap")
    dxil_parts = [(offset, end) for offset, end, magic in parts if magic == b"DXIL"]
    if not dxil_parts:
        raise ValueError("DXBC container has no DXIL program part")
    if len(dxil_parts) != 1:
        raise ValueError("DXBC container contains multiple DXIL programs")

    offset, end = dxil_parts[0]
    payload_size = _read_u32(data, offset + 4)
    if payload_size < 28 or payload_size % 4:
        raise ValueError("DXIL program header or bitcode is truncated")
    program = data[offset + 8 : end]
    program_version = _read_u32(program, 0)
    if program_version >> 16 != 0:
        raise ValueError("DXIL program is not a pixel shader")
    if _read_u32(program, 4) != payload_size // 4:
        raise ValueError("DXIL SizeInUint32 does not match program size")

    bitcode_header = program[8:]
    if bitcode_header[:4] != b"DXIL":
        raise ValueError("invalid DXIL bitcode header magic")
    bitcode_offset = _read_u32(bitcode_header, 8)
    bitcode_size = _read_u32(bitcode_header, 12)
    if bitcode_offset < 16 or bitcode_offset > payload_size - 8:
        raise ValueError("DXIL bitcode range is outside the program")
    if bitcode_size < 4 or bitcode_size > payload_size - 8 - bitcode_offset:
        raise ValueError("DXIL bitcode range is outside the program")
    bitcode_start = 8 + bitcode_offset
    if program[bitcode_start : bitcode_start + 4] != b"BC\xc0\xde":
        raise ValueError("DXIL bitcode is missing LLVM bitcode magic")


def fsl_artifact(bytecode: bytes) -> bytes:
    """Wrap validated DXIL in the pinned The Forge FSLHeader/FSLDerivative layout."""
    offset = FSL_HEADER.size + FSL_DERIVATIVE.size
    artifact_size = offset + len(bytecode)
    if artifact_size > MAX_ARTIFACT_BYTES:
        raise ValueError("FSL shader artifact exceeds the 64 MiB size limit")
    validate_dxil_pixel_container(bytecode)
    # FSL metadata: single-view, ICB compatible, 4 thread-group dimensions, output mask.
    header = FSL_HEADER.pack(b"@FSL", 1, 0, 0, 0, 0, 0, 0, 0)
    derivative = FSL_DERIVATIVE.pack(0, offset, len(bytecode))
    return header + derivative + bytecode


def shader_command(dxc: Path, source: Path, output: Path,
                   include_dir: Path | list[Path]) -> list[str]:
    """Return a shell-free DXC command for the public pixel-shader ABI."""
    include_dirs = [include_dir] if isinstance(include_dir, Path) else include_dir
    command = [str(dxc), "-T", "ps_6_0", "-E", "main"]
    for path in include_dirs:
        command.extend(("-I", str(path)))
    command.extend(("-Fo", str(output), str(source)))
    return command


def _format_diagnostics(result: subprocess.CompletedProcess[str]) -> str:
    output = "\n".join(part for part in (result.stdout, result.stderr) if part)
    return output.strip() or f"DXC exited with status {result.returncode} without diagnostics"


def compile_shader(source: Path, output: Path, dxc_root: Path, *,
                   include_dirs: list[Path] | None = None, runner=subprocess.run) -> None:
    """Verify pinned DXC, compile to temporaries, then atomically replace output."""
    source = source.resolve()
    output = output.resolve()
    dxc_root = dxc_root.resolve()
    if source == output:
        raise ShaderCompileError("source and output paths must differ")
    verify_dxc(dxc_root)
    dxc = dxc_root / "bin" / "x64" / "dxc.exe"
    if not dxc.is_file():
        raise ShaderCompileError(f"verified DXC executable is missing: {dxc}")
    if not source.is_file():
        raise ShaderCompileError(f"HLSL source file is missing: {source}")

    public_include = Path(__file__).resolve().parents[1] / "include"
    include_dirs = [public_include, *(Path(path).resolve() for path in (include_dirs or []))]
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gkcore-pixel-shader-", dir=output.parent) as temporary:
        temporary_dir = Path(temporary)
        dxil_path = temporary_dir / "compiled.dxil"
        staged_path = temporary_dir / output.name
        command = shader_command(dxc, source, dxil_path, include_dirs)
        try:
            result = runner(command, capture_output=True, text=True, encoding="utf-8",
                            errors="replace", check=False, shell=False)
        except OSError as error:
            raise ShaderCompileError(f"could not execute DXC at {dxc}: {error}") from error
        if result.returncode:
            raise ShaderCompileError(_format_diagnostics(result))
        try:
            bytecode = dxil_path.read_bytes()
        except OSError as error:
            raise ShaderCompileError(f"DXC succeeded but did not produce {dxil_path}: {error}") from error
        artifact = fsl_artifact(bytecode)
        staged_path.write_bytes(artifact)
        os.replace(staged_path, output)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="ordinary HLSL source with entry point main")
    parser.add_argument("--output", required=True, type=Path, help="destination .frag FSL runtime artifact")
    parser.add_argument("--dxc-root", required=True, type=Path, help="verified pinned DXC 1.8.2405 package root")
    parser.add_argument("--include-dir", action="append", type=Path,
                        help="additional HLSL include directory; may be repeated")
    args = parser.parse_args(argv)
    try:
        compile_shader(args.input, args.output, args.dxc_root, include_dirs=args.include_dir)
        return 0
    except (OSError, ValueError, ShaderCompileError) as error:
        print(f"pixel shader compile failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
