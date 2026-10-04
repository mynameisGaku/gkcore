#!/usr/bin/env python3
"""Validate The Forge FSL artifact and the built-in color shader contract.

A D3D12 FSL build uses FSL's @FSL derivative container; each selected stage
payload is a DXIL/DXBC container. Run this script without arguments for fast
format/diagnostic tests, or pass --artifact and --dxc to validate a compiled
artifact and its DXC reflection dump.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from dataclasses import dataclass
from typing import Iterable


FSL_HEADER = struct.Struct("<4s8I")
FSL_DERIVATIVE = struct.Struct("<QQQ")
DXIL_PREFIX = struct.Struct("<4s16sIII")


class ShaderContractError(ValueError):
    pass


@dataclass(frozen=True)
class Derivative:
    hash: int
    offset: int
    size: int
    bytecode: bytes


@dataclass(frozen=True)
class FslArtifact:
    metadata: tuple[int, ...]
    derivatives: tuple[Derivative, ...]


def parse_dxil_container(data: bytes, source: str = "<shader>") -> None:
    """Validate the DXBC container wrapper used by Direct3D 12 DXIL bytecode."""
    if len(data) < DXIL_PREFIX.size + 4:
        raise ShaderContractError(f"{source}: truncated DXIL container header ({len(data)} bytes)")
    if data[:4] != b"DXBC":
        raise ShaderContractError(f"{source}: expected DXIL/DXBC container magic 'DXBC'")

    _, _, version, declared_size, part_count = DXIL_PREFIX.unpack_from(data)
    if version != 1:
        raise ShaderContractError(f"{source}: unsupported DXBC container version {version}")
    if declared_size != len(data):
        raise ShaderContractError(
            f"{source}: container declares {declared_size} bytes but artifact has {len(data)}"
        )
    if part_count == 0:
        raise ShaderContractError(f"{source}: DXIL container has no parts")

    offsets_end = DXIL_PREFIX.size + 4 * part_count
    if offsets_end > len(data):
        raise ShaderContractError(f"{source}: truncated DXIL part-offset table")

    has_dxil = False
    ranges: list[tuple[int, int]] = []
    for index in range(part_count):
        offset = struct.unpack_from("<I", data, DXIL_PREFIX.size + index * 4)[0]
        if offset < offsets_end or offset + 8 > len(data):
            raise ShaderContractError(f"{source}: DXIL part {index} offset {offset} is outside the container")
        fourcc, size = struct.unpack_from("<4sI", data, offset)
        end = offset + 8 + size
        if end > len(data):
            raise ShaderContractError(f"{source}: DXIL part {index} payload exceeds the container")
        ranges.append((offset, end))
        has_dxil |= fourcc == b"DXIL"

    ranges.sort()
    if any(left[1] > right[0] for left, right in zip(ranges, ranges[1:])):
        raise ShaderContractError(f"{source}: DXIL container parts overlap")
    if not has_dxil:
        raise ShaderContractError(f"{source}: DXBC container is missing its DXIL program part")


def parse_fsl_artifact(data: bytes, source: str = "<artifact>") -> FslArtifact:
    """Parse ResourceLoader's pinned FSLHeader/FSLDerivative layout safely."""
    if len(data) < FSL_HEADER.size:
        raise ShaderContractError(f"{source}: truncated @FSL header ({len(data)} bytes)")
    fields = FSL_HEADER.unpack_from(data)
    magic, derivative_count, *metadata = fields
    if magic != b"@FSL":
        raise ShaderContractError(f"{source}: expected The Forge FSL artifact magic '@FSL'")
    if not 1 <= derivative_count <= 256:
        raise ShaderContractError(f"{source}: invalid FSL derivative count {derivative_count}")

    table_end = FSL_HEADER.size + derivative_count * FSL_DERIVATIVE.size
    if table_end > len(data):
        raise ShaderContractError(f"{source}: truncated FSL derivative table")

    derivatives: list[Derivative] = []
    ranges: list[tuple[int, int]] = []
    for index in range(derivative_count):
        hash_value, offset, size = FSL_DERIVATIVE.unpack_from(
            data, FSL_HEADER.size + index * FSL_DERIVATIVE.size
        )
        if size == 0:
            raise ShaderContractError(f"{source}: FSL derivative {index} has no bytecode")
        if offset < table_end or offset > len(data) or size > len(data) - offset:
            raise ShaderContractError(
                f"{source}: FSL derivative {index} range ({offset}, {size}) is outside the artifact"
            )
        bytecode = data[offset : offset + size]
        try:
            parse_dxil_container(bytecode, f"{source}: derivative {index}")
        except ShaderContractError as error:
            raise ShaderContractError(str(error)) from error
        derivatives.append(Derivative(hash_value, offset, size, bytecode))
        ranges.append((offset, offset + size))

    ranges.sort()
    if any(left[1] > right[0] for left, right in zip(ranges, ranges[1:])):
        raise ShaderContractError(f"{source}: FSL derivative bytecode ranges overlap")
    return FslArtifact(tuple(metadata), tuple(derivatives))


def validate_pixel_shader_reflection(dump: str, source: str = "<DXC reflection>") -> None:
    """Check the built-in color pixel shader signature (position and color)."""
    missing: list[str] = []
    input_section = _section(dump, "Input signature:", "Output signature:")
    output_section = _section(dump, "Output signature:", "Patch Constant signature:", "Resource Bindings:")
    resource_section = _section(dump, "Resource Bindings:", "ViewId state:", "Buffer Definitions:")

    for semantic in (r"\bSV_Position\s+0\b", r"\bCOLOR\s+0\b"):
        if not re.search(semantic, input_section, re.IGNORECASE):
            missing.append(semantic.replace(r"\b", "").replace(r"\s+", " "))
    if not re.search(r"\bSV_Target\s+0\b", output_section, re.IGNORECASE):
        missing.append("SV_Target0")
    # The built-in color shader has no resource descriptors. Custom shader ABI
    # validation is performed by the native DXC reflection path.
    if re.search(r"\b(?:t\d+|s\d+|u\d+|cb\d+)\b", resource_section, re.IGNORECASE):
        missing.append("resource-free built-in color pixel shader")
    if missing:
        raise ShaderContractError(f"{source}: shader reflection is missing required gkcore ABI entries: " + ", ".join(missing))


def _section(dump: str, begin: str, *ends: str) -> str:
    start = dump.find(begin)
    if start < 0:
        return ""
    start += len(begin)
    candidates = [dump.find(end, start) for end in ends]
    candidates = [index for index in candidates if index >= 0]
    return dump[start : min(candidates) if candidates else len(dump)]


def validate_runtime_manifest(paths: Iterable[str]) -> None:
    """Keep FSL, DXC, and shader-build tools out of the Runtime SDK."""
    compiler_markers = ("/fsl.py", "/compilers.py", "/forgeshadinglanguage/", "/dxc.exe")
    included = sorted(
        path for path in paths
        if any(marker in "/" + path.replace("\\", "/").lower() for marker in compiler_markers)
    )
    if included:
        raise ShaderContractError("Runtime SDK contains development shader tools: " + ", ".join(included))


def _sample_dxil() -> bytes:
    part_offset = DXIL_PREFIX.size + 4
    payload = b"DXIL" + struct.pack("<I", 4) + b"test"
    total_size = part_offset + len(payload)
    header = DXIL_PREFIX.pack(b"DXBC", bytes(16), 1, total_size, 1)
    return header + struct.pack("<I", part_offset) + payload


def _sample_fsl() -> bytes:
    dxil = _sample_dxil()
    table_end = FSL_HEADER.size + FSL_DERIVATIVE.size
    header = FSL_HEADER.pack(b"@FSL", 1, 0, 0, 1, 1, 1, 0, 0)
    derivative = FSL_DERIVATIVE.pack(0, table_end, len(dxil))
    return header + derivative + dxil


REFLECTION_GOOD = """\
; Input signature:
; Name Index Mask Register SysValue Format Used
; SV_Position 0 xyzw 0 POS float xyzw
; COLOR 0 xyzw 1 NONE float xyzw
; Output signature:
; Name Index Mask Register SysValue Format Used
; SV_Target 0 xyzw 0 TARGET float xyzw
; Resource Bindings:
; Name Type Format Dim ID HLSL Bind Count
"""


class FslArtifactTests(unittest.TestCase):
    def test_accepts_valid_fsl_with_dxil_derivative(self):
        parsed = parse_fsl_artifact(_sample_fsl(), "sample.frag")
        self.assertEqual(len(parsed.derivatives), 1)
        self.assertEqual(parsed.derivatives[0].bytecode[:4], b"DXBC")

    def test_reports_wrong_magic_with_filename(self):
        with self.assertRaisesRegex(ShaderContractError, r"sprite\.bin: expected The Forge FSL artifact magic"):
            parse_fsl_artifact(b"bad!" + bytes(40), "sprite.bin")

    def test_reports_truncated_derivative(self):
        artifact = bytearray(_sample_fsl())
        struct.pack_into("<Q", artifact, FSL_HEADER.size + 16, len(artifact) + 100)
        with self.assertRaisesRegex(ShaderContractError, r"sample\.bin: FSL derivative 0 range"):
            parse_fsl_artifact(bytes(artifact), "sample.bin")

    def test_reports_damaged_nested_dxil(self):
        artifact = bytearray(_sample_fsl())
        artifact[-len(_sample_dxil())] = ord("X")
        with self.assertRaisesRegex(ShaderContractError, r"bad\.bin: derivative 0: expected DXIL/DXBC"):
            parse_fsl_artifact(bytes(artifact), "bad.bin")


class ShaderReflectionTests(unittest.TestCase):
    def test_accepts_builtin_color_pixel_shader_reflection(self):
        validate_pixel_shader_reflection(REFLECTION_GOOD, "sprite.dxil")

    def test_lists_missing_abi_entries(self):
        with self.assertRaisesRegex(ShaderContractError, r"SV_Position 0.*COLOR 0.*SV_Target0"):
            validate_pixel_shader_reflection("; Input signature:\n; TEXCOORD 0\n; Output signature:\n")

    def test_runtime_manifest_rejects_compilers(self):
        validate_runtime_manifest(["include/gkcore.h", "bin/gkcore.dll", "lib/gkcore.lib"])
        with self.assertRaisesRegex(ShaderContractError, r"Runtime SDK contains development shader tools:"):
            validate_runtime_manifest([
                "bin/dxc.exe",
                "Common_3/Tools/ForgeShadingLanguage/fsl.py",
            ])

    def test_runtime_manifest_accepts_required_dxil_runtime_dependencies(self):
        validate_runtime_manifest([
            "bin/gkcore.dll",
            "bin/D3D12Core.dll",
            "bin/dxcompiler.dll",
            "bin/dxil.dll",
        ])


def inspect_artifact(path: pathlib.Path, dxc: str | None, require_reflection: bool) -> None:
    parsed = parse_fsl_artifact(path.read_bytes(), str(path))
    print(f"PASS: {path}: valid @FSL artifact, {len(parsed.derivatives)} DXIL derivative(s)")
    if not dxc:
        if require_reflection:
            raise ShaderContractError("--require-reflection needs --dxc <dxc.exe>")
        print("NOTE: reflection was not checked (pass --dxc to inspect the first DXIL derivative)")
        return

    with tempfile.TemporaryDirectory(prefix="gkcore-shader-reflection-") as directory:
        inner = pathlib.Path(directory) / "shader.dxil"
        inner.write_bytes(parsed.derivatives[0].bytecode)
        result = subprocess.run([dxc, "-dumpbin", "-all", str(inner)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        if result.returncode:
            raise ShaderContractError(f"{path}: DXC reflection failed (exit {result.returncode}):\n{result.stdout}")
        validate_pixel_shader_reflection(result.stdout, str(path))
        print(f"PASS: {path}: DXC reflection satisfies the built-in color pixel shader contract")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=pathlib.Path, help="compiled The Forge @FSL artifact")
    parser.add_argument("--dxc", help="DXC executable used to inspect the embedded DXIL")
    parser.add_argument("--require-reflection", action="store_true", help="fail if --dxc is omitted")
    parser.add_argument("--runtime-manifest", type=pathlib.Path,
                        help="newline-separated install manifest to check for shader build tools")
    args = parser.parse_args()

    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        return 1
    try:
        if args.runtime_manifest:
            validate_runtime_manifest(args.runtime_manifest.read_text(encoding="utf-8").splitlines())
            print(f"PASS: {args.runtime_manifest}: no FSL/DXC compiler tools in Runtime manifest")
        if args.artifact:
            dxc = args.dxc or shutil.which("dxc")
            inspect_artifact(args.artifact, dxc, args.require_reflection)
        elif args.require_reflection:
            raise ShaderContractError("--require-reflection needs --artifact <compiled shader>")
    except (OSError, ShaderContractError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
