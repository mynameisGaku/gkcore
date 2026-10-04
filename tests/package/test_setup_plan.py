"""Setup must run the full CPU/package suite as well as opt-in GPU tests."""
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import setup


class SetupPlanTests(unittest.TestCase):
    def _detect_toolchain(self, instances, cmake_version):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            program_files = root / "Program Files (x86)"
            vswhere = program_files / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
            vswhere.parent.mkdir(parents=True)
            vswhere.touch()
            sdk = program_files / "Windows Kits" / "10"
            sdk_header = sdk / "Include" / "10.0.22621.0" / "um" / "Windows.h"
            sdk_header.parent.mkdir(parents=True)
            sdk_header.touch()
            sdk_library = sdk / "Lib" / "10.0.22621.0" / "um" / "x64"
            sdk_library.mkdir(parents=True)
            paths_by_range = {}
            for version_range, name, has_v142 in instances:
                instance = root / name
                paths_by_range.setdefault(version_range, []).append(str(instance))
                msbuild = instance / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
                msbuild.parent.mkdir(parents=True)
                msbuild.touch()
                if has_v142:
                    compiler = instance / "VC" / "Tools" / "MSVC" / "14.29.30133" / "bin" / "Hostx64" / "x64" / "cl.exe"
                    compiler.parent.mkdir(parents=True)
                    compiler.touch()

            def run(command, **kwargs):
                if command[0] == str(vswhere):
                    version_range = command[command.index("-version") + 1]
                    output = "\n".join(paths_by_range.get(version_range, []))
                    return SimpleNamespace(returncode=0, stdout=output, stderr="")
                return SimpleNamespace(returncode=0, stdout=f"cmake version {cmake_version}\n", stderr="")

            fake_os = SimpleNamespace(name="nt", environ={"ProgramFiles(x86)": str(program_files)})
            with patch.object(setup, "os", fake_os), \
                 patch.object(setup.shutil, "which", return_value="cmake"), \
                 patch.object(setup.subprocess, "run", side_effect=run):
                return setup.require_windows_toolchain()

    def test_vs2026_instance_with_v142_is_selected_and_paired_with_its_msbuild(self):
        toolchain = self._detect_toolchain([("[18.0,19.0)", "VS2026", True)], "4.2.0")

        self.assertEqual(toolchain.installation_path.name, "VS2026")
        self.assertEqual(toolchain.msbuild_path, toolchain.installation_path / "MSBuild" / "Current" / "Bin" / "MSBuild.exe")
        self.assertEqual(toolchain.generator, "Visual Studio 18 2026")

    def test_vs2022_instance_with_v142_is_supported_by_cmake_321(self):
        toolchain = self._detect_toolchain([("[17.0,18.0)", "VS2022", True)], "3.21.0")

        self.assertEqual(toolchain.installation_path.name, "VS2022")
        self.assertEqual(toolchain.generator, "Visual Studio 17 2022")

    def test_multiple_instances_use_a_v142_instance_and_its_own_msbuild(self):
        toolchain = self._detect_toolchain([
            ("[18.0,19.0)", "VS2026-without-v142", False),
            ("[17.0,18.0)", "VS2022-with-v142", True),
        ], "3.21.0")

        self.assertEqual(toolchain.installation_path.name, "VS2022-with-v142")
        self.assertEqual(toolchain.msbuild_path.parents[3], toolchain.installation_path)
        self.assertEqual(toolchain.generator, "Visual Studio 17 2022")

    def test_cmake_321_uses_vs2022_when_both_generations_have_v142(self):
        toolchain = self._detect_toolchain([
            ("[18.0,19.0)", "VS2026", True),
            ("[17.0,18.0)", "VS2022", True),
        ], "3.21.0")

        self.assertEqual(toolchain.installation_path.name, "VS2022")
        self.assertEqual(toolchain.msbuild_path.parents[3], toolchain.installation_path)
        self.assertEqual(toolchain.generator, "Visual Studio 17 2022")

    def test_vs2026_requires_cmake_42(self):
        with self.assertRaisesRegex(setup.SetupError, "4\.2"):
            self._detect_toolchain([("[18.0,19.0)", "VS2026", True)], "4.1.9")

    def test_vs2022_requires_cmake_321(self):
        with self.assertRaisesRegex(setup.SetupError, "3\.21"):
            self._detect_toolchain([("[17.0,18.0)", "VS2022", True)], "3.20.6")

    def test_vs2026_without_v142_reports_the_missing_toolset(self):
        with self.assertRaisesRegex(setup.SetupError, "v142.*14\.29"):
            self._detect_toolchain([("[18.0,19.0)", "VS2026", False)], "4.3.1")

    def test_prepare_keeps_full_suite_when_gpu_smoke_is_enabled(self):
        commands = []
        toolchain = setup.WindowsToolchain(
            Path("C:/VS2026"), Path("C:/VS2026/MSBuild/Current/Bin/MSBuild.exe"),
            "Visual Studio 18 2026", (4, 2))
        with patch.object(setup, "require_windows_toolchain", return_value=toolchain), \
             patch.object(setup, "ensure_dependencies"), \
             patch.object(setup, "run", side_effect=lambda command, **kwargs: commands.append(command)), \
             patch("builtins.print"):
            setup.prepare(gpu_check=True)

        configure = next(command for command in commands if command[:2] == ["cmake", "-S"])
        self.assertEqual(configure[configure.index("-G") + 1], toolchain.generator)
        self.assertIn(f"-DCMAKE_GENERATOR_INSTANCE={toolchain.installation_path}", configure)
        self.assertIn("-DGKCORE_RUN_BACKEND_SMOKE=ON", configure)
        forge_build = next(command for command in commands if str(command[1]).endswith("build_forge.py"))
        self.assertEqual(forge_build[forge_build.index("--msbuild") + 1], str(toolchain.msbuild_path))
        command = next(command for command in commands if command[0] == "ctest")
        self.assertIn("--output-on-failure", command)
        self.assertNotIn("-L", command)
        self.assertNotIn("windows|gpu", command)


if __name__ == "__main__":
    unittest.main()
