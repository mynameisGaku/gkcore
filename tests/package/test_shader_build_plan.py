"""Check the compiler command and exact shader outputs used for Runtime."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from build_gkcore_shaders import (ROOT, copy_fsl_inputs, copy_compatible_fsl,
                                  reflection_command, runtime_artifacts, shader_command)
from forge_checkout import ForgeCheckoutError


class ShaderBuildPlanTests(unittest.TestCase):
    def test_uses_pinned_fsl_toolchain_and_only_expected_runtime_assets(self):
        forge = Path("C:/dev/The-Forge")
        dxc = Path("C:/dev/dxc")
        out = Path("C:/dev/build/shaders")
        command = shader_command(Path("C:/python/python.exe"), forge, dxc, out)
        normalized = " ".join(str(part).replace("\\", "/") for part in command)
        self.assertIn("Common_3/Tools/ForgeShadingLanguage/fsl.py", normalized)
        self.assertIn("shaders/shaders.list", normalized)
        self.assertIn("DIRECT3D12", normalized)
        self.assertIn("Graphics/FSL", normalized)
        self.assertIn("--compile", normalized)
        self.assertEqual({path.name for path in runtime_artifacts(out)},
                         {"gkcore_color.vert", "gkcore_color.frag", "gkcore_sprite.vert", "gkcore_sprite.frag",
                          "gkcore_model.vert", "gkcore_model.frag", "gkcore_model_skinning.comp",
                          "gkcore_post.vert", "gkcore_bloom_extract.frag", "gkcore_bloom_blur.frag",
                          "gkcore_post_composite.frag", "gkcore_fxaa.frag", "default.rootsig", "compute.rootsig"})

    def test_compiled_runtime_post_shader_is_checked_with_dxc_reflection(self):
        command = reflection_command(Path("C:/python/python.exe"), Path("C:/dev/dxc"), Path("C:/dev/build/shaders"))
        normalized = [part.replace("\\", "/") for part in command]
        self.assertEqual(normalized[1], str(ROOT / "tests" / "shader_contract_tests.py").replace("\\", "/"))
        self.assertIn("CompiledShaders/DIRECT3D12/gkcore_post_composite.frag", normalized[3])
        self.assertIn("C:/dev/dxc/bin/x64/dxc.exe", normalized[5])
        self.assertEqual(normalized[-2:], ["--require-reflection", "--post-composite"])

    def test_shader_command_uses_the_temporary_compatible_fsl_copy(self):
        # 一時copyのFSL scriptを指定する。
        fsl_script = Path("C:/temp/The-Forge/Common_3/Tools/ForgeShadingLanguage/fsl.py")
        command = shader_command(Path("C:/python/python.exe"), Path("C:/dev/The-Forge"),
                                 Path("C:/dev/dxc"), Path("C:/dev/build/shaders"),
                                 fsl_script=fsl_script)
        self.assertEqual(command[1], str(fsl_script))

    def test_fsl_input_copy_removes_only_the_encoding_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            destination = root / "normalized"
            source.mkdir()
            authored = source / "shader.fsl"
            authored.write_bytes(b"\xef\xbb\xbf#include \"shader.h\"\r\n")

            copy_fsl_inputs(source, destination)

            self.assertTrue(authored.read_bytes().startswith(b"\xef\xbb\xbf"))
            self.assertEqual((destination / authored.name).read_bytes(), b"#include \"shader.h\"\r\n")

    def test_fsl_compatibility_copy_only_changes_resource_letter_comparisons(self):
        with tempfile.TemporaryDirectory() as directory:
            # 一時ディレクトリをテスト用checkoutのrootにする。
            root = Path(directory)
            forge = root / "pinned-forge"
            fsl = forge / "Common_3" / "Tools" / "ForgeShadingLanguage"
            generator = fsl / "generators" / "d3d.py"
            generator.parent.mkdir(parents=True)
            # 固定generatorの比較行と変更対象外の比較行。
            source = ("resource_type_letter is 's':\r\n"
                      "resource_type_letter is 'b':\r\n"
                      "resource_type_letter is 't':\r\n"
                      "resource_type_letter is 'u':\r\n"
                      "other is 'x':\r\n").encode("ascii")
            generator.write_bytes(source)
            (fsl / "fsl.py").write_bytes(b"script\r\n")
            # コピー対象となるFSLの補助ファイル。
            support_files = {
                fsl / "utils.py": b"preprocessor paths\r\n",
                fsl / "compilers.py": b"compiler dispatch\r\n",
                fsl / "generators" / "__init__.py": b"generator imports\r\n",
                fsl / "includes" / "d3d.h": b"d3d declarations\r\n",
            }
            for path, contents in support_files.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(contents)
            # 古いbytecodeが修正済みsourceを隠さないことを確かめる。
            stale_cache = generator.parent / "__pycache__" / "d3d.cpython-311.pyc"
            stale_cache.parent.mkdir()
            stale_cache.write_bytes(b"stale bytecode")
            dependency = forge / "Common_3" / "Utilities" / "ThirdParty" / "OpenSource" / "mcpp" / "bin" / "mcpp.exe"
            dependency.parent.mkdir(parents=True)
            dependency.write_bytes(b"pinned helper")

            # 互換版FSLとWindows前処理依存物の配置を検査する。
            compatible_script = copy_compatible_fsl(forge, root / "temporary")

            copied_generator = compatible_script.parent / "generators" / "d3d.py"
            self.assertEqual(compatible_script.read_bytes(), b"script\r\n")
            self.assertEqual(generator.read_bytes(), source)
            self.assertEqual(copied_generator.read_bytes(),
                             source.replace(b" is '", b" == '", 4))
            self.assertFalse((copied_generator.parent / "__pycache__").exists())
            for path, contents in support_files.items():
                copied_path = compatible_script.parent / path.relative_to(fsl)
                self.assertEqual(copied_path.read_bytes(), contents)
            copied_dependency = compatible_script.parents[2] / "Utilities" / "ThirdParty" / "OpenSource" / "mcpp" / "bin" / "mcpp.exe"
            self.assertEqual(copied_dependency.read_bytes(), b"pinned helper")

    def test_rejects_unexpected_pinned_fsl_identity_comparison_count(self):
        with tempfile.TemporaryDirectory() as directory:
            # 比較式が固定版から変わった状況を模擬する。
            root = Path(directory)
            forge = root / "pinned-forge"
            fsl = forge / "Common_3" / "Tools" / "ForgeShadingLanguage"
            generator = fsl / "generators" / "d3d.py"
            generator.parent.mkdir(parents=True)
            generator.write_bytes(b"resource_type_letter is 's':\n")
            (fsl / "fsl.py").write_bytes(b"script\n")
            dependency = forge / "Common_3" / "Utilities" / "ThirdParty" / "OpenSource" / "mcpp" / "bin" / "mcpp.exe"
            dependency.parent.mkdir(parents=True)
            dependency.write_bytes(b"helper")

            # vendor更新時に想定外の置換をしないことを確かめる。
            with self.assertRaisesRegex(ForgeCheckoutError, "exactly one identity comparison"):
                copy_compatible_fsl(forge, root / "temporary")

    def test_rejects_unrecognized_resource_letter_identity_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            # 未対応の文字種比較を含むgeneratorを模擬する。
            root = Path(directory)
            forge = root / "pinned-forge"
            fsl = forge / "Common_3" / "Tools" / "ForgeShadingLanguage"
            generator = fsl / "generators" / "d3d.py"
            generator.parent.mkdir(parents=True)
            generator.write_bytes(b"".join(
                b"resource_type_letter is '" + letter + b"':\n"
                for letter in (b"s", b"b", b"t", b"u", b"q")
            ))
            (fsl / "fsl.py").write_bytes(b"script\n")
            dependency = forge / "Common_3" / "Utilities" / "ThirdParty" / "OpenSource" / "mcpp" / "bin" / "mcpp.exe"
            dependency.parent.mkdir(parents=True)
            dependency.write_bytes(b"helper")

            # 未知の比較を残したままbuildへ進めない。
            with self.assertRaisesRegex(ForgeCheckoutError, "unsupported resource-letter"):
                copy_compatible_fsl(forge, root / "temporary")


if __name__ == "__main__":
    unittest.main()
