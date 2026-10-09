"""ゲーム構築用checkoutの出力対象とCMake切り替えを固定する。"""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import prepare_game_repository as exporter
import setup


class GameRepositoryTests(unittest.TestCase):
    def test_main_cmake_removes_nested_test_capture_and_benchmark_blocks(self):
        source = """cmake_minimum_required(VERSION 3.20)
option(GKCORE_BUILD_TESTS \"tests\" ON)
option(GKCORE_RENDER_PERFORMANCE_METRICS \"metrics\" OFF)
if(GKCORE_BUILD_TESTS)
    add_executable(gkcore_tests tests/core_tests.cpp)
    if(TARGET gkcore_capture_runtime)
        target_link_libraries(gkcore_tests PRIVATE gkcore_capture_runtime)
    endif()
endif()
if(TARGET gkcore_model_benchmark)
    add_dependencies(gkcore_model_benchmark gkcore_stage_runtime_payload)
endif()
if(TARGET gkcore_model_viewer)
    add_dependencies(gkcore_model_viewer gkcore_stage_runtime_payload)
endif()
add_library(gkcore SHARED src/api/gkcore.cpp)
"""

        result = exporter.make_game_cmake(source)

        self.assertIn("add_dependencies(gkcore_model_viewer", result)
        self.assertIn("add_library(gkcore SHARED", result)
        self.assertNotRegex(result, r"(?i)tests|capture|benchmark|GKCORE_BUILD_TESTS|GKCORE_RENDER_PERFORMANCE")

    def test_product_cmake_builds_custom_shader_from_example_source(self):
        source = (Path(__file__).resolve().parents[2] / "CMakeLists.txt").read_text(encoding="utf-8-sig")

        result = exporter.make_game_cmake(source)

        self.assertIn("examples/shaders/post_effect_tint.hlsl", result)
        self.assertIn("tools/compile_pixel_shader.py", result)
        self.assertIn("${_gkcore_custom_post_shader}", result)
        self.assertNotIn("tests/assets/shaders", result)

    def test_product_shader_builder_does_not_require_validation_tests(self):
        source = (Path(__file__).resolve().parents[2] / "tools" / "build_gkcore_shaders.py").read_text(encoding="utf-8-sig")

        result = exporter.make_game_shader_builder(source)

        self.assertIn("def compile_shaders", result)
        self.assertNotIn("reflection_command", result)
        self.assertNotIn("tests/", result)

    def test_runtime_presets_keep_only_runtime_configuration_without_test_cache(self):
        source = {
            "version": 3,
            "configurePresets": [
                {"name": "dev", "cacheVariables": {"GKCORE_BUILD_TESTS": "ON"}},
                {"name": "runtime-windows", "cacheVariables": {
                    "GKCORE_BUILD_TESTS": "ON", "GKCORE_BUILD_RUNTIME": "ON"}},
            ],
            "buildPresets": [
                {"name": "dev", "configurePreset": "dev"},
                {"name": "runtime-windows", "configurePreset": "runtime-windows"},
            ],
            "testPresets": [{"name": "dev", "configurePreset": "dev"}],
        }

        result = exporter.make_game_presets(source)

        self.assertEqual([preset["name"] for preset in result["configurePresets"]], ["runtime-windows"])
        self.assertEqual([preset["name"] for preset in result["buildPresets"]], ["runtime-windows"])
        self.assertNotIn("testPresets", result)
        self.assertNotIn("GKCORE_BUILD_TESTS", result["configurePresets"][0]["cacheVariables"])

    def test_copy_plan_excludes_development_files_and_keeps_game_inputs(self):
        paths = set(exporter.allowed_repository_files())

        self.assertIn("src/api/gkcore.cpp", paths)
        self.assertIn("examples/model_viewer.cpp", paths)
        self.assertIn("examples/shaders/tint.hlsl", paths)
        self.assertIn("tools/compile_pixel_shader.py", paths)
        self.assertIn("docs/THIRD_PARTY_NOTICES.md", paths)
        self.assertIn("cmake/gpu.cfg", paths)
        self.assertIn(".gitignore", paths)
        self.assertIn(".gitattributes", paths)
        self.assertNotIn("AGENTS.md", paths)
        self.assertNotIn("START.bat", paths)
        self.assertNotIn("CLEAN.bat", paths)
        self.assertFalse(any(path.startswith("tests/") for path in paths))
        self.assertFalse(any("validation" in path.lower() or "benchmark" in path.lower() for path in paths))

    def test_product_document_filter_keeps_runtime_constraints_and_images(self):
        source = """# API

`gk::SetExposure` rejects non-finite and out-of-range values without applying them.

## GPU test results

Release and Debug GPU image tests passed. See [render validation](render-validation.md).

```bat
ctest --test-dir build/runtime-windows -R gkcore.effects
```

旧実装はcacheの上限に達して失敗しましたが、変更後は全画素一致しました。

![Effect example](images/effects-baseline.png)
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "docs" / "images").mkdir(parents=True)
            (root / "docs" / "images" / "effects-baseline.png").write_bytes(b"image")

            result = exporter._sanitize_document(source, root, Path("docs/guide.md"))

        self.assertIn("rejects non-finite and out-of-range values", result)
        self.assertIn("![Effect example](images/effects-baseline.png)", result)
        self.assertNotIn("ctest", result.lower())
        self.assertNotIn("render-validation.md", result)
        self.assertNotIn("GPU test results", result)
        self.assertNotIn("旧実装", result)

    def test_export_creates_only_allowlisted_tree_and_refuses_to_overwrite(self):
        source = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            destination = root / "game-repository"
            copied = exporter.export_repository(source, destination)

            actual = {path.relative_to(destination).as_posix()
                      for path in destination.rglob("*") if path.is_file()}
            self.assertTrue(set(copied).issubset(actual))
            self.assertFalse(any(path.startswith("tests/") for path in actual))
            self.assertNotIn("AGENTS.md", actual)
            self.assertNotIn("START.bat", actual)
            self.assertNotIn("CLEAN.bat", actual)
            generated_cmake = (destination / "CMakeLists.txt").read_text(encoding="utf-8-sig")
            self.assertNotIn("tests/assets", generated_cmake)
            self.assertNotIn("Unit_Tests", generated_cmake)
            self.assertIn("examples/shaders/post_effect_tint.hlsl", generated_cmake)
            self.assertIn("SDKを出力します", (destination / "docs" / "quickstart.md").read_text(encoding="utf-8-sig"))
            self.assertIn("Runtime SDKの構成", (destination / "docs" / "package-layout.md").read_text(encoding="utf-8-sig"))
            generated_shader_builder = (destination / "tools" / "build_gkcore_shaders.py").read_text(encoding="utf-8-sig")
            self.assertNotIn("tests/", generated_shader_builder)
            self.assertIn("examples/assets/*.glb binary", (destination / ".gitattributes").read_text(encoding="utf-8"))
            self.assertIn("/build/", (destination / ".gitignore").read_text(encoding="utf-8"))
            self.assertFalse(any("tests/assets" in line for line in (destination / ".gitattributes").read_text(encoding="utf-8").splitlines()))
            self.assertEqual(exporter.find_broken_local_links(destination), [])
            markdown = "\n".join(path.read_text(encoding="utf-8-sig")
                                  for path in destination.rglob("*.md"))
            self.assertNotRegex(markdown, r"(?i)ctest\s+--")
            for path in destination.rglob("*.md"):
                for line_number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
                    if exporter.DEVELOPMENT_EVIDENCE.search(line) and not line.lstrip().startswith("!["):
                        self.fail(f"development validation text remains at {path.relative_to(destination)}:{line_number}")
            text_extensions = {".cpp", ".h", ".hpp", ".hlsl", ".fsl", ".py", ".ps1", ".bat", ".md", ".in"}
            for path in destination.rglob("*"):
                if not path.is_file() or path.relative_to(destination).parts[0] == "third_party":
                    continue
                if path.suffix.lower() not in text_extensions and path.name not in (
                        "CMakeLists.txt", ".gitignore", ".gitattributes"):
                    continue
                contents = path.read_bytes()
                relative = path.relative_to(destination).as_posix()
                self.assertNotIn(b"\r\r\n", contents, relative)
                self.assertIn(b"\r\n", contents, relative)
                self.assertEqual(contents.count(b"\n"), contents.count(b"\r\n"), relative)

            occupied = root / "occupied"
            occupied.mkdir()
            marker = occupied / "keep.txt"
            marker.write_text("preserve", encoding="utf-8")
            with self.assertRaisesRegex(exporter.ExportError, "must be empty"):
                exporter.export_repository(source, occupied)
            self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")

    def test_setup_defaults_to_tests_only_when_checkout_contains_them(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertFalse(setup.tests_enabled(root, None))
            (root / "tests").mkdir()
            self.assertTrue(setup.tests_enabled(root, None))
            self.assertTrue(setup.tests_enabled(root, True))
            self.assertFalse(setup.tests_enabled(root, False))

    def test_explicit_tests_option_requires_tests_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(setup.SetupError, "tests directory"):
                setup.tests_enabled(Path(directory), True)

    def test_prepare_skips_ctest_and_test_options_when_checkout_has_no_tests(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("project(game)\n", encoding="utf-8")
            commands = []
            toolchain = setup.WindowsToolchain(
                Path("C:/VS2026"), Path("C:/VS2026/MSBuild/Current/Bin/MSBuild.exe"),
                "Visual Studio 18 2026", (4, 2))
            with patch.object(setup, "ROOT", root), \
                 patch.object(setup, "require_windows_toolchain", return_value=toolchain), \
                 patch.object(setup, "ensure_dependencies"), \
                 patch.object(setup, "run", side_effect=lambda command, **kwargs: commands.append(command)), \
                 patch("builtins.print"):
                setup.prepare()

        configure = next(command for command in commands if command[:2] == ["cmake", "-S"])
        self.assertFalse(any("GKCORE_BUILD_TESTS" in argument for argument in configure))
        self.assertFalse(any(command[0] == "ctest" for command in commands))


if __name__ == "__main__":
    unittest.main()
