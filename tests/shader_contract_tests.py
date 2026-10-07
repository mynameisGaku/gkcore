#!/usr/bin/env python3
"""Validate The Forge FSL artifact and built-in shader contracts.

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


def validate_model_pixel_shader_reflection(dump: str, source: str = "<DXC reflection>") -> None:
    """Check the model lighting pixel shader's interpolants, texture and lighting ABI."""
    input_section = _section(dump, "Input signature:", "Output signature:")
    output_section = _section(dump, "Output signature:", "Patch Constant signature:", "Resource Bindings:")
    resource_section = _section(dump, "Resource Bindings:", "ViewId state:", "Buffer Definitions:")
    missing: list[str] = []
    for semantic in (r"\bSV_Position\s+0\b", r"\bCOLOR\s+0\b", r"\bTEXCOORD\s+0\b",
                     r"\bTEXCOORD\s+1\b", r"\bTEXCOORD\s+2\b", r"\bTEXCOORD\s+3\b", r"\bTEXCOORD\s+4\b", r"\bTEXCOORD\s+5\b", r"\bTEXCOORD\s+6\b", r"\bTEXCOORD\s+7\b", r"\bTEXCOORD\s+8\b"):
        if not re.search(semantic, input_section, re.IGNORECASE):
            missing.append(semantic.replace(r"\b", "").replace(r"\s+", " "))
    if not re.search(r"\bSV_Target\s+0\b", output_section, re.IGNORECASE):
        missing.append("SV_Target0")
    if not re.search(r"\bgImageTexture\s+texture\b[^\n]*\bt0\b", resource_section, re.IGNORECASE):
        missing.append("gImageTexture at t0")
    if not re.search(r"\bgMetallicRoughnessTexture\s+texture\b[^\n]*\bt1\b", resource_section, re.IGNORECASE):
        missing.append("gMetallicRoughnessTexture at t1")
    if not re.search(r"\bgNormalTexture\s+texture\b[^\n]*\bt2\b", resource_section, re.IGNORECASE):
        missing.append("gNormalTexture at t2")
    if not re.search(r"\bgImageSampler\s+sampler\b[^\n]*\bs3\b", resource_section, re.IGNORECASE):
        missing.append("gImageSampler in the ModelTextureResources persistent set")
    if not re.search(r"\bgMetallicRoughnessSampler\s+sampler\b[^\n]*\bs4\b", resource_section, re.IGNORECASE):
        missing.append("gMetallicRoughnessSampler at s4")
    if not re.search(r"\bgNormalSampler\s+sampler\b[^\n]*\bs5\b", resource_section, re.IGNORECASE):
        missing.append("gNormalSampler at s5")
    if not re.search(r"\bgModelLighting\s+cbuffer\b[^\n]*\bcb0,space1\b", resource_section, re.IGNORECASE):
        missing.append("LightingConstants at b0, space1")
    buffer_section = _section(dump, "Buffer Definitions:", "Resource Bindings:")
    if not re.search(r"Size:\s*32\b", buffer_section):
        missing.append("32-byte LightingConstants block")
    # 切り抜きの有効値と境界値は、追加した2成分の入力を必須とする。
    if not re.search(r"\bTEXCOORD\s+4\s+xy\s+\d+\s+\S+\s+float\s+xy\b", input_section, re.IGNORECASE):
        missing.append("alpha mask/cutoff at TEXCOORD4.xy")
    if not re.search(r"\bTEXCOORD\s+5\s+xy\s+\d+\s+\S+\s+float\s+xy\b", input_section, re.IGNORECASE):
        missing.append("metallic-roughness UV at TEXCOORD5.xy")
    # DXCのregister内配置がxy/zwのどちらでも、宣言と使用成分が一致することを照合する。
    for semantic, mask, label in ((6, "xyzw", "world tangent"), (7, "(?:xy|zw)", "normal UV"), (8, "(?:xy|zw)", "normal parameters")):
        if not re.search(rf"\bTEXCOORD\s+{semantic}\s+({mask})\s+\d+\s+\S+\s+float\s+\1\b", input_section, re.IGNORECASE):
            missing.append(f"{label} at TEXCOORD{semantic}.{mask}")
    if missing:
        raise ShaderContractError(f"{source}: model pixel shader reflection is missing required gkcore ABI entries: " + ", ".join(missing))


def validate_post_composite_shader_reflection(dump: str, source: str = "<DXC reflection>") -> None:
    """合成shaderが隣接する2つのtexture slotを公開することを確認する。"""
    resource_section = _section(dump, "Resource Bindings:", "ViewId state:", "Buffer Definitions:")
    texture_rows = [line for line in resource_section.splitlines()
                    if re.search(r"\btexture\b", line, re.IGNORECASE)]
    missing: list[str] = []
    if len(texture_rows) != 1 or not re.search(
            r"\btexture\s+f32\s+2d\s+T\d+\s+t0(?:,space0)?\s+2\b",
            texture_rows[0] if texture_rows else "", re.IGNORECASE):
        missing.append("two-element Texture2D binding at t0-t1")
    if not re.search(r"\bsampler\s+NA\s+NA\s+S\d+\s+s2(?:,space0)?\s+1\b",
                     resource_section, re.IGNORECASE):
        missing.append("one sampler binding at s2")
    if not re.search(r"\bcbuffer\s+NA\s+NA\s+CB\d+\s+cb3(?:,space0)?\s+1\b",
                     resource_section, re.IGNORECASE):
        missing.append("one constant-buffer binding at b3")
    if missing:
        raise ShaderContractError(
            f"{source}: post-composite shader reflection is missing required gkcore ABI entries: " + ", ".join(missing))


def validate_model_vertex_shader_reflection(dump: str, source: str = "<DXC reflection>") -> None:
    """Check the model lighting vertex shader's mesh input and varyings."""
    input_section = _section(dump, "Input signature:", "Output signature:")
    output_section = _section(dump, "Output signature:", "Patch Constant signature:", "Resource Bindings:")
    missing: list[str] = []
    for semantic in (r"\bPOSITION\s+0\b", r"\bCOLOR\s+0\b", r"\bTEXCOORD\s+0\b",
                     r"\bNORMAL\s+0\b", r"\bTEXCOORD\s+1\b", r"\bTEXCOORD\s+2\b", r"\bTEXCOORD\s+3\b", r"\bTEXCOORD\s+4\b", r"\bTANGENT\s+0\b", r"\bTEXCOORD\s+5\b", r"\bTEXCOORD\s+6\b"):
        if not re.search(semantic, input_section, re.IGNORECASE):
            missing.append(semantic.replace(r"\b", "").replace(r"\s+", " "))
    for semantic in (r"\bSV_Position\s+0\b", r"\bCOLOR\s+0\b", r"\bTEXCOORD\s+0\b",
                     r"\bTEXCOORD\s+1\b", r"\bTEXCOORD\s+2\b", r"\bTEXCOORD\s+3\b", r"\bTEXCOORD\s+4\b", r"\bTEXCOORD\s+5\b", r"\bTEXCOORD\s+6\b", r"\bTEXCOORD\s+7\b", r"\bTEXCOORD\s+8\b"):
        if not re.search(semantic, output_section, re.IGNORECASE):
            missing.append("output " + semantic.replace(r"\b", "").replace(r"\s+", " "))
    # アルファ抜き情報の2成分を確認する。
    if not re.search(r"\bTEXCOORD\s+3\s+xy\s+\d+\s+\S+\s+float\s+xy\b", input_section, re.IGNORECASE):
        missing.append("alpha mask/cutoff input at TEXCOORD3.xy")
    if not re.search(r"\bTEXCOORD\s+4\s+xy\s+\d+\s+\S+\s+float\s+xy\b", output_section, re.IGNORECASE):
        missing.append("alpha mask/cutoff output at TEXCOORD4.xy")
    if not re.search(r"\bTEXCOORD\s+4\s+xy\s+\d+\s+\S+\s+float\s+xy\b", input_section, re.IGNORECASE):
        missing.append("metallic-roughness UV input at TEXCOORD4.xy")
    if not re.search(r"\bTEXCOORD\s+5\s+xy\s+\d+\s+\S+\s+float\s+xy\b", output_section, re.IGNORECASE):
        missing.append("metallic-roughness UV output at TEXCOORD5.xy")
    # 120byte頂点に対応する接線、法線UV、材質値の成分数。
    for signature, semantic, index, mask, label in ((input_section, "TANGENT", 0, "xyzw", "world tangent input"), (input_section, "TEXCOORD", 5, "xy", "normal UV input"), (input_section, "TEXCOORD", 6, "xy", "normal parameters input"), (output_section, "TEXCOORD", 6, "xyzw", "world tangent output"), (output_section, "TEXCOORD", 7, "(?:xy|zw)", "normal UV output"), (output_section, "TEXCOORD", 8, "(?:xy|zw)", "normal parameters output")):
        if not re.search(rf"\b{semantic}\s+{index}\s+({mask})\s+\d+\s+\S+\s+float\s+\1\b", signature, re.IGNORECASE):
            missing.append(f"{label} at {semantic}{index}.{mask}")
    if missing:
        raise ShaderContractError(f"{source}: model vertex shader reflection is missing required gkcore ABI entries: " + ", ".join(missing))


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

    def test_requires_compiled_model_shader_artifacts(self):
        root = pathlib.Path(__file__).resolve().parent / "assets" / "shaders"
        for suffix in ("vert", "frag"):
            with self.subTest(suffix=suffix):
                self.assertTrue((root / f"gkcore_model.{suffix}").is_file())

    def test_accepts_model_lighting_pixel_shader_reflection(self):
        good = """\
; Input signature:
; SV_Position 0 xyzw 0 POS float xyzw
; COLOR 0 xyzw 1 NONE float xyzw
; TEXCOORD 0 xyzw 2 NONE float xyzw
; TEXCOORD 1 xyzw 3 NONE float xyzw
; TEXCOORD 2 xyzw 4 NONE float xyzw
; TEXCOORD 3 xyzw 5 NONE float xyzw
; TEXCOORD 4 xy 6 NONE float xy
; TEXCOORD 5 xy 7 NONE float xy
; TEXCOORD 6 xyzw 8 NONE float xyzw
; TEXCOORD 7 xy 9 NONE float xy
; TEXCOORD 8 xy 10 NONE float xy
; Output signature:
; SV_Target 0 xyzw 0 TARGET float xyzw
; Buffer Definitions:
; gModelLighting Size: 32
; Resource Bindings:
; gImageTexture texture f32 2d 0 T0 t0 1
; gMetallicRoughnessTexture texture f32 2d 1 T1 t1 1
; gNormalTexture texture f32 2d 2 T2 t2 1
; gImageSampler sampler NA NA 0 S0 s3 1
; gMetallicRoughnessSampler sampler NA NA 1 S1 s4 1
; gNormalSampler sampler NA NA 2 S2 s5 1
; gModelLighting cbuffer NA NA NA CB0 cb0,space1 1
"""
        validate_model_pixel_shader_reflection(good, "gkcore_model.frag")
        # 従来の6入力だけのshaderを、新しい頂点配置へ誤って使わせない。
        with self.assertRaisesRegex(ShaderContractError, "alpha mask/cutoff"):
            validate_model_pixel_shader_reflection(good.replace("; TEXCOORD 4 xy 6 NONE float xy\n", ""), "old-model.frag")

        # register後半へ詰められた2成分も同じ意味と使用成分を保つ。
        validate_model_pixel_shader_reflection(good.replace("; TEXCOORD 7 xy 9 NONE float xy", "; TEXCOORD 7 zw 9 NONE float zw").replace("; TEXCOORD 8 xy 10 NONE float xy", "; TEXCOORD 8 zw 10 NONE float zw"), "packed-model.frag")
        # 接線と法線画像bindingを欠くバイナリは受け付けない。
        with self.assertRaisesRegex(ShaderContractError, "world tangent"):
            validate_model_pixel_shader_reflection(good.replace("; TEXCOORD 6 xyzw 8 NONE float xyzw\n", ""), "no-tangent.frag")
        with self.assertRaisesRegex(ShaderContractError, "gNormalTexture"):
            validate_model_pixel_shader_reflection(good.replace("; gNormalTexture texture f32 2d 2 T2 t2 1\n", ""), "no-normal.frag")

        # 役割別samplerを共用bindingに退行させない。
        with self.assertRaisesRegex(ShaderContractError, "gMetallicRoughnessSampler"):
            validate_model_pixel_shader_reflection(good.replace("; gMetallicRoughnessSampler sampler NA NA 1 S1 s4 1\n", ""), "shared-sampler.frag")
        with self.assertRaisesRegex(ShaderContractError, "gNormalSampler"):
            validate_model_pixel_shader_reflection(good.replace("; gNormalSampler sampler NA NA 2 S2 s5 1\n", ""), "shared-sampler.frag")

    def test_rejects_model_reflection_without_lighting_abi(self):
        with self.assertRaisesRegex(ShaderContractError, "LightingConstants at b0, space1"):
            validate_model_pixel_shader_reflection(REFLECTION_GOOD, "gkcore_model.frag")

    def test_accepts_post_composite_texture_array_reflection(self):
        reflection = """\
; Resource Bindings:
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;                                   cbuffer      NA          NA     CB0            cb3     1
;                                   sampler      NA          NA      S0             s2     1
;                                   texture     f32          2d      T0             t0     2
; ViewId state:
"""
        validate_post_composite_shader_reflection(reflection, "gkcore_post_composite.frag")

    def test_rejects_overlapping_single_texture_bindings_for_post_composite(self):
        reflection = """\
; Resource Bindings:
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
;                                   cbuffer      NA          NA     CB0            cb0     1
;                                   sampler      NA          NA      S0             s0     1
;                                   texture     f32          2d      T0             t0     1
;                                   texture     f32          2d      T0            t0     1
; ViewId state:
"""
        with self.assertRaisesRegex(ShaderContractError, "two-element Texture2D binding at t0-t1"):
            validate_post_composite_shader_reflection(reflection, "gkcore_post_composite.frag")

    def test_rejects_stale_post_composite_register_offsets(self):
        reflection = """\
; Resource Bindings:
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
;                                   cbuffer      NA          NA     CB0            cb0     1
;                                   sampler      NA          NA      S0             s0     1
;                                   texture     f32          2d      T0             t0     2
; ViewId state:
"""
        with self.assertRaises(ShaderContractError):
            validate_post_composite_shader_reflection(reflection, "gkcore_post_composite.frag")

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


def inspect_artifact(path: pathlib.Path, dxc: str | None, require_reflection: bool,
                     model_pixel: bool = False, model_vertex: bool = False,
                     post_composite: bool = False) -> None:
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
        if result.returncode and "Unknown argument: '-all'" in result.stdout:
            result = subprocess.run([dxc, "-dumpbin", str(inner)], text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        if result.returncode:
            raise ShaderContractError(f"{path}: DXC reflection failed (exit {result.returncode}):\n{result.stdout}")
        if model_pixel:
            validate_model_pixel_shader_reflection(result.stdout, str(path))
            print(f"PASS: {path}: DXC reflection satisfies the model lighting pixel shader contract")
        elif model_vertex:
            validate_model_vertex_shader_reflection(result.stdout, str(path))
            print(f"PASS: {path}: DXC reflection satisfies the model lighting vertex shader contract")
        elif post_composite:
            validate_post_composite_shader_reflection(result.stdout, str(path))
            print(f"PASS: {path}: DXC reflection exposes both post-composite texture slots")
        else:
            validate_pixel_shader_reflection(result.stdout, str(path))
            print(f"PASS: {path}: DXC reflection satisfies the built-in color pixel shader contract")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=pathlib.Path, help="compiled The Forge @FSL artifact")
    parser.add_argument("--dxc", help="DXC executable used to inspect the embedded DXIL")
    parser.add_argument("--require-reflection", action="store_true", help="fail if --dxc is omitted")
    parser.add_argument("--model-pixel", action="store_true",
                        help="check the model lighting pixel shader reflection ABI")
    parser.add_argument("--model-vertex", action="store_true",
                        help="check the model lighting vertex shader reflection ABI")
    parser.add_argument("--post-composite", action="store_true",
                        help="check the post-composite texture-array reflection ABI")
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
            inspect_artifact(args.artifact, dxc, args.require_reflection,
                             args.model_pixel, args.model_vertex, args.post_composite)
        elif args.require_reflection:
            raise ShaderContractError("--require-reflection needs --artifact <compiled shader>")
    except (OSError, ShaderContractError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
