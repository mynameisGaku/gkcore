"""Fail-closed allowlist for the small gkcore Runtime SDK."""
from pathlib import PurePosixPath
import re


class PackageError(ValueError):
    pass


RUNTIME_DLLS = {
    "bin/gkcore.dll",
    "bin/D3D12Core.dll",
    "bin/dxcompiler.dll",
    "bin/dxil.dll",
    "bin/amd_ags_x64.dll",
    "bin/WinPixEventRuntime.dll",
}
RUNTIME_SHADERS = {
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
    "bin/CompiledShaders/DIRECT3D12/gkcore_fxaa.frag",
}
_FIXED = {
    "include/gkcore.h",
    "include/gkcore/Handle.h",
    "include/gkcore/Shader.hlsl",
    "lib/cmake/gkcore/gkcoreConfig.cmake",
    "lib/cmake/gkcore/gkcoreConfigVersion.cmake",
    "lib/cmake/gkcore/gkcoreTargets.cmake",
    "share/licenses/gkcore/THIRD_PARTY_NOTICES.md",
    "share/licenses/gkcore/The-Forge-LICENSE.txt",
    "share/licenses/gkcore/D3D12MemoryAllocator-LICENSE.txt",
    "share/licenses/gkcore/D3D12MemoryAllocator-NOTICES.txt",
    "share/licenses/gkcore/Direct3d12Agility-LICENSE.txt",
    "share/licenses/gkcore/Direct3d12Agility-LICENSE-CODE.txt",
    "share/licenses/gkcore/AMD-AGS-LICENSE.txt",
    "share/licenses/gkcore/WinPix-ThirdPartyNotices.txt",
    "share/licenses/gkcore/NVAPI-SDK-License.pdf",
    "share/licenses/gkcore/LICENSE-LLVM.txt",
    "share/licenses/gkcore/LICENSE-MIT.txt",
    "share/licenses/gkcore/LICENSE-MS.txt",
    "share/licenses/gkcore/stb-image-LICENSE.txt",
    "share/licenses/gkcore/cgltf-LICENSE.txt",
    "share/licenses/gkcore/ufbx-LICENSE.txt",
}


def validate(files):
    """Validate the installed file manifest and return its normalized paths."""
    normalized = set()
    for name in files:
        path = PurePosixPath(str(name).replace("\\", "/"))
        if path.is_absolute() or ".." in path.parts:
            raise PackageError("package manifest contains a path outside its prefix")
        normalized.add(path.as_posix())
    if any(path == "." for path in normalized):
        raise PackageError("package manifest contains a path outside its prefix")
    required = _FIXED | RUNTIME_DLLS | RUNTIME_SHADERS | {"lib/gkcore.lib"}
    missing = required - normalized
    if missing:
        raise PackageError("missing required runtime files: " + ", ".join(sorted(missing)))

    allowed = set(required)
    # CMake emits one configuration-specific import location file for multi-config generators.
    allowed.update(path for path in normalized if re.fullmatch(r"lib/cmake/gkcore/gkcoreTargets-[A-Za-z0-9_]+\.cmake", path))
    extra = normalized - allowed
    if extra:
        raise PackageError("non-Runtime files in package: " + ", ".join(sorted(extra)))
    return normalized
