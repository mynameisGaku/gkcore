import struct
import sys
import tempfile
from pathlib import Path
from unittest import TestCase, main
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import compile_pixel_shader as compiler
from compile_pixel_shader import fsl_artifact, shader_command


def dxil_container(stage=0, *, bad_bitcode=False, part_magic=b"DXIL"):
    """Build a minimal structurally valid DXBC container with one DXIL program."""
    program_version = (stage << 16) | (6 << 4)
    bitcode = b"BAD!" if bad_bitcode else b"BC\xc0\xde"
    # DXIL program header is 8 bytes, followed by a 16-byte bitcode header.
    bitcode_header = struct.pack("<4sIII", b"DXIL", 1, 16, len(bitcode))
    program = struct.pack("<II", program_version, (8 + len(bitcode_header) + len(bitcode)) // 4)
    program += bitcode_header + bitcode
    part = struct.pack("<4sI", part_magic, len(program)) + program
    offset = 36
    total_size = offset + len(part)
    header = struct.pack("<4s16sIII", b"DXBC", bytes(16), 1, total_size, 1)
    return header + struct.pack("<I", offset) + part


class PixelShaderCompilerTests(TestCase):
    def test_wraps_pixel_dxil_in_pinned_fsl_resource_loader_layout(self):
        bytecode = dxil_container()
        artifact = fsl_artifact(bytecode)
        self.assertEqual(artifact[:4], b"@FSL")
        self.assertEqual(struct.unpack_from("<8I", artifact, 4), (1, 0, 0, 0, 0, 0, 0, 0))
        self.assertEqual(struct.unpack_from("<QQQ", artifact, 36), (0, 60, len(bytecode)))
        self.assertEqual(artifact[60:], bytecode)

    def test_rejects_truncated_and_inconsistent_dxbc_containers(self):
        for payload in (b"bad", dxil_container()[:31]):
            with self.subTest(payload_size=len(payload)), self.assertRaises(ValueError):
                fsl_artifact(payload)
        malformed = bytearray(dxil_container())
        struct.pack_into("<I", malformed, 24, len(malformed) + 1)
        with self.assertRaisesRegex(ValueError, "declared size"):
            fsl_artifact(bytes(malformed))

    def test_rejects_truncated_offset_tables_bad_offsets_and_chunk_overruns(self):
        truncated_table = bytearray(dxil_container()[:36])
        struct.pack_into("<I", truncated_table, 24, len(truncated_table))
        struct.pack_into("<I", truncated_table, 28, 2)
        with self.assertRaisesRegex(ValueError, "part-offset table"):
            fsl_artifact(bytes(truncated_table))

        invalid_offset = bytearray(dxil_container())
        struct.pack_into("<I", invalid_offset, 32, len(invalid_offset) - 4)
        with self.assertRaisesRegex(ValueError, "offset is outside"):
            fsl_artifact(bytes(invalid_offset))

        oversized_chunk = bytearray(dxil_container())
        struct.pack_into("<I", oversized_chunk, 40, len(oversized_chunk))
        with self.assertRaisesRegex(ValueError, "payload exceeds"):
            fsl_artifact(bytes(oversized_chunk))

    def test_rejects_overlapping_container_parts_and_invalid_bitcode_ranges(self):
        two_parts = dxil_container()
        part = two_parts[36:]
        total = 40 + len(part)
        header = bytearray(two_parts[:32])
        struct.pack_into("<I", header, 24, total)
        struct.pack_into("<I", header, 28, 2)
        duplicated = bytes(header) + struct.pack("<II", 40, 40) + part
        with self.assertRaisesRegex(ValueError, "overlap"):
            fsl_artifact(duplicated)

        invalid_bitcode = bytearray(dxil_container())
        # DXIL chunk starts at 36; payload program starts at 44; bitcode offset is at 60.
        struct.pack_into("<I", invalid_bitcode, 60, 0xfffffff0)
        with self.assertRaisesRegex(ValueError, "bitcode range"):
            fsl_artifact(bytes(invalid_bitcode))

    def test_rejects_missing_dxil_invalid_bitcode_and_non_pixel_stage(self):
        cases = (
            (dxil_container(part_magic=b"SFI0"), "no DXIL program"),
            (dxil_container(bad_bitcode=True), "LLVM bitcode"),
            (dxil_container(stage=1), "not a pixel shader"),
        )
        for payload, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                fsl_artifact(payload)

    def test_rejects_container_and_fsl_size_limits(self):
        with self.assertRaisesRegex(ValueError, "64 MiB"):
            fsl_artifact(dxil_container() + bytes(64 * 1024 * 1024))

    def test_compiler_uses_fixed_pixel_entry_model_and_hlsl_include_path(self):
        command = shader_command(Path("C:/dxc/dxc.exe"), Path("C:/sample.hlsl"),
                                 Path("C:/out.dxil"), Path("C:/include"))
        joined = " ".join(str(part).replace("\\", "/") for part in command)
        self.assertIn("-T ps_6_0", joined)
        self.assertIn("-E main", joined)
        self.assertIn("C:/include", joined)
        self.assertIn("-Fo", joined)

    def test_failed_compile_preserves_existing_output_and_keeps_dxc_location_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dxc = root / "bin" / "x64" / "dxc.exe"
            dxc.parent.mkdir(parents=True)
            dxc.touch()
            source = root / "bad shader.hlsl"
            source.write_text("broken", encoding="utf-8")
            output = root / "existing.frag"
            output.write_bytes(b"previous-good-artifact")

            def failed_compile(command, **kwargs):
                self.assertFalse(kwargs.get("shell", True))
                return compiler.subprocess.CompletedProcess(
                    command, 1, "", f"{source}(7,3): error: unknown identifier\n")

            with patch.object(compiler, "verify_dxc", return_value=None) as verify:
                with self.assertRaisesRegex(compiler.ShaderCompileError, r"bad shader\.hlsl\(7,3\)"):
                    compiler.compile_shader(source, output, root, runner=failed_compile)
                verify.assert_called_once_with(root.resolve())
            self.assertEqual(output.read_bytes(), b"previous-good-artifact")
            self.assertEqual({p.name for p in root.iterdir()}, {"bin", source.name, output.name})

    def test_successful_compile_atomically_replaces_output_with_wrapped_shader(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dxc = root / "bin" / "x64" / "dxc.exe"
            dxc.parent.mkdir(parents=True)
            dxc.touch()
            source = root / "pixel.hlsl"
            source.write_text("float4 main() : SV_Target { return 1; }", encoding="utf-8")
            output = root / "pixel.frag"
            output.write_bytes(b"old")

            def successful_compile(command, **kwargs):
                self.assertFalse(kwargs.get("shell", True))
                dxil_index = command.index("-Fo") + 1
                Path(command[dxil_index]).write_bytes(dxil_container())
                return compiler.subprocess.CompletedProcess(command, 0, "", "")

            with patch.object(compiler, "verify_dxc", return_value=None):
                compiler.compile_shader(source, output, root, runner=successful_compile)
            self.assertEqual(output.read_bytes(), fsl_artifact(dxil_container()))
            self.assertEqual({p.name for p in root.iterdir()}, {"bin", source.name, output.name})

    def test_invalid_compiler_output_does_not_replace_existing_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dxc = root / "bin" / "x64" / "dxc.exe"
            dxc.parent.mkdir(parents=True)
            dxc.touch()
            source = root / "vertex.hlsl"
            source.write_text("float4 main() : SV_Target { return 1; }", encoding="utf-8")
            output = root / "pixel.frag"
            output.write_bytes(b"known-good")

            def vertex_compile(command, **kwargs):
                Path(command[command.index("-Fo") + 1]).write_bytes(dxil_container(stage=1))
                return compiler.subprocess.CompletedProcess(command, 0, "", "")

            with patch.object(compiler, "verify_dxc", return_value=None):
                with self.assertRaisesRegex(ValueError, "not a pixel shader"):
                    compiler.compile_shader(source, output, root, runner=vertex_compile)
            self.assertEqual(output.read_bytes(), b"known-good")

    def test_utf8_dxc_diagnostics_survive_a_non_utf8_windows_locale(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dxc = root / "bin" / "x64" / "dxc.exe"
            dxc.parent.mkdir(parents=True)
            dxc.touch()
            source = root / "shader.hlsl"
            source.write_text("bad", encoding="utf-8")
            output = root / "shader.frag"

            def locale_sensitive_runner(command, **kwargs):
                self.assertFalse(kwargs.get("shell", True))
                kwargs.setdefault("encoding", "ascii")
                child = [sys.executable, "-c",
                         "import sys; sys.stderr.buffer.write('日本語: shader.hlsl(9,2): error'.encode('utf-8')); sys.exit(1)"]
                return compiler.subprocess.run(child, **kwargs)

            with patch.object(compiler, "verify_dxc", return_value=None):
                with self.assertRaisesRegex(compiler.ShaderCompileError, "日本語: shader.hlsl\\(9,2\\): error"):
                    compiler.compile_shader(source, output, root, runner=locale_sensitive_runner)
            self.assertFalse(output.exists())

    def test_refuses_to_replace_the_hlsl_source_with_compiled_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "shader.hlsl"
            source.write_text("preserve this source", encoding="utf-8")
            with patch.object(compiler, "verify_dxc", return_value=None):
                with self.assertRaisesRegex(compiler.ShaderCompileError, "source and output paths must differ"):
                    compiler.compile_shader(source, source, root)
            self.assertEqual(source.read_text(encoding="utf-8"), "preserve this source")


if __name__ == "__main__":
    main()
