#!/usr/bin/env python3
import re
import unittest

from package_allowlist import PackageError, RUNTIME_DLLS, _FIXED, validate


PUBLIC_HEADERS = {"include/gkcore.h", "include/gkcore/Handle.h", "include/gkcore/Shader.hlsl", "include/gkcore/ModelAnimation.h", "include/gkcore/EHumanoidBone.h"}
SHADERS = {
    "bin/CompiledShaders/DIRECT3D12/gkcore_color.vert",
    "bin/CompiledShaders/DIRECT3D12/gkcore_color.frag",
    "bin/CompiledShaders/DIRECT3D12/default.rootsig",
    "bin/CompiledShaders/DIRECT3D12/compute.rootsig",
    "bin/CompiledShaders/DIRECT3D12/gkcore_sprite.vert",
    "bin/CompiledShaders/DIRECT3D12/gkcore_sprite.frag",
    "bin/CompiledShaders/DIRECT3D12/gkcore_model.vert",
    "bin/CompiledShaders/DIRECT3D12/gkcore_model.frag",
    "bin/CompiledShaders/DIRECT3D12/gkcore_post.vert",
    "bin/CompiledShaders/DIRECT3D12/gkcore_bloom_extract.frag",
    "bin/CompiledShaders/DIRECT3D12/gkcore_bloom_blur.frag",
    "bin/CompiledShaders/DIRECT3D12/gkcore_post_composite.frag",
    "bin/CompiledShaders/DIRECT3D12/gkcore_fxaa.frag",
}
# 片方の欠落も検出するため、配布物に必要な2ファイルを明示する。
GPU_CONFIGURATION = {"bin/gpu.data", "bin/gpu.cfg"}
BASE = _FIXED | PUBLIC_HEADERS | RUNTIME_DLLS | GPU_CONFIGURATION | SHADERS | {"lib/gkcore.lib", "lib/cmake/gkcore/gkcoreTargets-release.cmake"}
DEBUG_LAYER = "bin/d3d12SDKLayers.dll"


class RuntimeAllowlistTests(unittest.TestCase):

    def test_requires_mikktspace_license(self):
        # 静的に組み込む接線生成コードの原文noticeを配布物へ必須にする。
        with self.assertRaises(PackageError):
            validate(BASE - {"share/licenses/gkcore/mikktspace-LICENSE.txt"})

    def test_accepts_minimal_runtime(self):
        self.assertEqual(validate(BASE), BASE)

    def test_rejects_development_and_upstream_content(self):
        for extra in (
            "tests/core_tests.cpp",
            "tools/pack.py",
            "src/ForgeBackend.cpp",
            "Common_3/Renderer.h",
            "third_party/ufbx/ufbx.h",
            "third_party/ufbx/ufbx.c",
        ):
            with self.subTest(extra=extra), self.assertRaises(PackageError):
                validate(BASE | {extra})

    def test_rejects_core_only_install(self):
        with self.assertRaises(PackageError):
            validate({"include/gkcore.h", "lib/libgkcoreCore.a"})

    def test_rejects_missing_license_and_unknown_runtime_file(self):
        with self.assertRaises(PackageError):
            validate(BASE - {"share/licenses/gkcore/THIRD_PARTY_NOTICES.md"})
        with self.assertRaises(PackageError):
            validate(BASE - {"share/licenses/gkcore/ufbx-LICENSE.txt"})
        with self.assertRaises(PackageError):
            validate(BASE | {"share/gkcore/shaders/default.bin"})

    def test_rejects_missing_compiled_shader(self):
        with self.assertRaisesRegex(PackageError, "gkcore_color.frag"):
            validate(BASE - {"bin/CompiledShaders/DIRECT3D12/gkcore_color.frag"})
        with self.assertRaisesRegex(PackageError, "gkcore_fxaa.frag"):
            validate(BASE - {"bin/CompiledShaders/DIRECT3D12/gkcore_fxaa.frag"})
        with self.assertRaisesRegex(PackageError, "gkcore_model.vert"):
            validate(BASE - {"bin/CompiledShaders/DIRECT3D12/gkcore_model.vert"})
        with self.assertRaisesRegex(PackageError, "gkcore_model.frag"):
            validate(BASE - {"bin/CompiledShaders/DIRECT3D12/gkcore_model.frag"})

    def test_rejects_missing_gpu_configuration_data(self):
        for path in GPU_CONFIGURATION:
            pattern = rf"missing required runtime files:.*{re.escape(path.rsplit('/', 1)[-1])}"
            with self.subTest(path=path), self.assertRaisesRegex(PackageError, pattern):
                validate(BASE - {path})

    def test_rejects_prefix_escape(self):
        with self.assertRaises(PackageError):
            validate(BASE | {"../outside.txt"})

    def test_debug_requires_sdk_debug_layer(self):
        with self.assertRaisesRegex(PackageError, "d3d12SDKLayers.dll"):
            validate(BASE, configuration="Debug")

    def test_debug_accepts_sdk_debug_layer(self):
        package = BASE | {DEBUG_LAYER}
        self.assertEqual(validate(package, configuration="Debug"), package)

    def test_release_rejects_sdk_debug_layer(self):
        with self.assertRaisesRegex(PackageError, "d3d12SDKLayers.dll"):
            validate(BASE | {DEBUG_LAYER}, configuration="Release")

    def test_release_like_configurations_remain_supported(self):
        for configuration in ("Release", "RelWithDebInfo", "MinSizeRel"):
            with self.subTest(configuration=configuration):
                self.assertEqual(validate(BASE, configuration=configuration), BASE)

    def test_rejects_unknown_configuration(self):
        with self.assertRaisesRegex(PackageError, "configuration"):
            validate(BASE, configuration="Fast")


if __name__ == "__main__":
    unittest.main()
