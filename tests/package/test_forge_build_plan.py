#!/usr/bin/env python3
"""Exercise the deterministic MSBuild invocations without requiring Windows."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from build_forge import WINDOWS_SDK_REQUIRED_FILES, WINDOWS_SDK_VERSION, build_plan, verify_windows_sdk
from forge_checkout import ForgeCheckoutError


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
            self.assertIn(f"/p:WindowsTargetPlatformVersion={WINDOWS_SDK_VERSION}", command)
            self.assertTrue(any(argument.startswith("/p:OutDir=") and "gkcore-build" in argument for argument in command))
            self.assertTrue(any(argument.startswith("/p:IntDir=") and "gkcore-build" in argument for argument in command))

    def test_sdk_pin_matches_the_conflict_workaround_and_checks_required_files(self):
        self.assertEqual(WINDOWS_SDK_VERSION, "10.0.22621.0")
        with tempfile.TemporaryDirectory() as directory:
            sdk_root = Path(directory)
            for relative_path in WINDOWS_SDK_REQUIRED_FILES:
                path = sdk_root / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            self.assertEqual(verify_windows_sdk(sdk_root), WINDOWS_SDK_VERSION)
            (sdk_root / "Lib" / WINDOWS_SDK_VERSION / "um" / "x64" / "dxguid.lib").unlink()
            with self.assertRaisesRegex(ForgeCheckoutError, "dxguid\.lib"):
                verify_windows_sdk(sdk_root)

    def test_official_fsl_project_build_requires_a_separate_verified_dxc(self):
        import inspect
        from build_forge import build
        self.assertIn("dxc_root", inspect.signature(build).parameters)

    def test_compiler_environment_adds_utf8_without_discarding_existing_cl_options(self):
        from build_forge import compiler_environment
        environment = compiler_environment({"CL": "/W3 /DKEEP=1"}, Path("/tools/dxc"))
        self.assertEqual(environment["CL"], "/W3 /DKEEP=1 /utf-8")
        self.assertEqual(environment["FSL_COMPILER_DXC"], str(Path("/tools/dxc/bin/x64").resolve()))


if __name__ == "__main__":
    unittest.main()
