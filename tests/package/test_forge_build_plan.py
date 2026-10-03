#!/usr/bin/env python3
"""Exercise the deterministic MSBuild invocations without requiring Windows."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from build_forge import build_plan


class ForgeBuildPlanTests(unittest.TestCase):
    def test_builds_only_official_renderer_and_os_archives_in_isolated_output_dirs(self):
        root = Path("/dev/forge-root")
        output = Path("/dev/gkcore-build/forge")
        plan = build_plan("MSBuild.exe", root, output)
        self.assertEqual(len(plan), 2)
        self.assertIn("Examples_3/Unit_Tests/PC_VS2019/Libraries/Renderer/Renderer.vcxproj", plan[0][1].replace("\\", "/"))
        self.assertIn("Examples_3/Unit_Tests/PC_VS2019/Libraries/OS/OS.vcxproj", plan[1][1].replace("\\", "/"))
        for command in plan:
            self.assertIn("/m", command)
            self.assertIn("/t:Build", command)
            self.assertIn("/p:Configuration=Release", command)
            self.assertIn("/p:Platform=x64", command)
            self.assertIn("/p:WindowsTargetPlatformVersion=10.0", command)
            self.assertTrue(any(argument.startswith("/p:OutDir=") and "gkcore-build" in argument for argument in command))
            self.assertTrue(any(argument.startswith("/p:IntDir=") and "gkcore-build" in argument for argument in command))

    def test_official_fsl_project_build_requires_a_separate_verified_dxc(self):
        import inspect
        from build_forge import build
        self.assertIn("dxc_root", inspect.signature(build).parameters)


if __name__ == "__main__":
    unittest.main()
