#!/usr/bin/env python3
import unittest

from package_allowlist import PackageError, RUNTIME_DLLS, _FIXED, validate


PUBLIC_HEADERS = {"include/gkcore.h", "include/gkcore/Handle.h", "include/gkcore/Shader.hlsl"}
SHADERS = {
    "bin/CompiledShaders/DIRECT3D12/gkcore_color.vert",
    "bin/CompiledShaders/DIRECT3D12/gkcore_color.frag",
    "bin/CompiledShaders/DIRECT3D12/default.rootsig",
    "bin/CompiledShaders/DIRECT3D12/compute.rootsig",
    "bin/CompiledShaders/DIRECT3D12/gkcore_sprite.vert",
    "bin/CompiledShaders/DIRECT3D12/gkcore_sprite.frag",
    "bin/CompiledShaders/DIRECT3D12/gkcore_post.vert",
    "bin/CompiledShaders/DIRECT3D12/gkcore_bloom_extract.frag",
    "bin/CompiledShaders/DIRECT3D12/gkcore_bloom_blur.frag",
    "bin/CompiledShaders/DIRECT3D12/gkcore_post_composite.frag",
}
BASE = _FIXED | PUBLIC_HEADERS | RUNTIME_DLLS | SHADERS | {"lib/gkcore.lib", "lib/cmake/gkcore/gkcoreTargets-release.cmake"}


class RuntimeAllowlistTests(unittest.TestCase):
    def test_accepts_minimal_runtime(self):
        self.assertEqual(validate(BASE), BASE)

    def test_rejects_development_and_upstream_content(self):
        for extra in ("tests/core_tests.cpp", "tools/pack.py", "src/ForgeBackend.cpp", "Common_3/Renderer.h"):
            with self.subTest(extra=extra), self.assertRaises(PackageError):
                validate(BASE | {extra})

    def test_rejects_core_only_install(self):
        with self.assertRaises(PackageError):
            validate({"include/gkcore.h", "lib/libgkcoreCore.a"})

    def test_rejects_missing_license_and_unknown_runtime_file(self):
        with self.assertRaises(PackageError):
            validate(BASE - {"share/licenses/gkcore/THIRD_PARTY_NOTICES.md"})
        with self.assertRaises(PackageError):
            validate(BASE | {"share/gkcore/shaders/default.bin"})

    def test_rejects_missing_compiled_shader(self):
        with self.assertRaisesRegex(PackageError, "gkcore_color.frag"):
            validate(BASE - {"bin/CompiledShaders/DIRECT3D12/gkcore_color.frag"})

    def test_rejects_prefix_escape(self):
        with self.assertRaises(PackageError):
            validate(BASE | {"../outside.txt"})


if __name__ == "__main__":
    unittest.main()
