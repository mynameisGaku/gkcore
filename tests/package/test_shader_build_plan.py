"""Check the compiler command and exact shader outputs used for Runtime."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from build_gkcore_shaders import ROOT, copy_fsl_inputs, reflection_command, runtime_artifacts, shader_command


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
                          "gkcore_model.vert", "gkcore_model.frag",
                          "gkcore_post.vert", "gkcore_bloom_extract.frag", "gkcore_bloom_blur.frag",
                          "gkcore_post_composite.frag", "gkcore_fxaa.frag", "default.rootsig", "compute.rootsig"})

    def test_compiled_runtime_post_shader_is_checked_with_dxc_reflection(self):
        command = reflection_command(Path("C:/python/python.exe"), Path("C:/dev/dxc"), Path("C:/dev/build/shaders"))
        normalized = [part.replace("\\", "/") for part in command]
        self.assertEqual(normalized[1], str(ROOT / "tests" / "shader_contract_tests.py").replace("\\", "/"))
        self.assertIn("CompiledShaders/DIRECT3D12/gkcore_post_composite.frag", normalized[3])
        self.assertIn("C:/dev/dxc/bin/x64/dxc.exe", normalized[5])
        self.assertEqual(normalized[-2:], ["--require-reflection", "--post-composite"])

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


if __name__ == "__main__":
    unittest.main()
