"""Check the compiler command and exact shader outputs used for Runtime."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from build_gkcore_shaders import shader_command, runtime_artifacts


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
                          "gkcore_post.vert", "gkcore_bloom_extract.frag", "gkcore_bloom_blur.frag",
                          "gkcore_post_composite.frag", "gkcore_fxaa.frag", "default.rootsig", "compute.rootsig"})


if __name__ == "__main__":
    unittest.main()
