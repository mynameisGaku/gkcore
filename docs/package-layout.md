# 配布物と開発物

ゲーム開発者へ渡す Runtime SDK は、アプリの build/run に必要なファイルに限定します。テスト、The Forge source/headers、FSL/DXC compiler、ビルド用スクリプト、サンプル asset は開発 checkout に置き、Runtime package allowlist で混入を拒否します。

## Runtime SDK の許可ファイル

現行の `tests/package/package_allowlist.py` が要求するファイル一覧です。

```text
include/gkcore.h
include/gkcore/Handle.h
include/gkcore/Shader.hlsl
bin/gkcore.dll
bin/D3D12Core.dll
bin/dxcompiler.dll
bin/dxil.dll
bin/amd_ags_x64.dll
bin/WinPixEventRuntime.dll
bin/CompiledShaders/DIRECT3D12/gkcore_color.vert
bin/CompiledShaders/DIRECT3D12/gkcore_color.frag
bin/CompiledShaders/DIRECT3D12/gkcore_sprite.vert
bin/CompiledShaders/DIRECT3D12/gkcore_sprite.frag
bin/CompiledShaders/DIRECT3D12/gkcore_post.vert
bin/CompiledShaders/DIRECT3D12/gkcore_bloom_extract.frag
bin/CompiledShaders/DIRECT3D12/gkcore_bloom_blur.frag
bin/CompiledShaders/DIRECT3D12/gkcore_post_composite.frag
bin/CompiledShaders/DIRECT3D12/default.rootsig
bin/CompiledShaders/DIRECT3D12/compute.rootsig
lib/gkcore.lib
lib/cmake/gkcore/gkcoreConfig.cmake
lib/cmake/gkcore/gkcoreConfigVersion.cmake
lib/cmake/gkcore/gkcoreTargets.cmake
lib/cmake/gkcore/gkcoreTargets-<構成>.cmake
share/licenses/gkcore/THIRD_PARTY_NOTICES.md
share/licenses/gkcore/The-Forge-LICENSE.txt
share/licenses/gkcore/D3D12MemoryAllocator-LICENSE.txt
share/licenses/gkcore/D3D12MemoryAllocator-NOTICES.txt
share/licenses/gkcore/Direct3d12Agility-LICENSE.txt
share/licenses/gkcore/Direct3d12Agility-LICENSE-CODE.txt
share/licenses/gkcore/AMD-AGS-LICENSE.txt
share/licenses/gkcore/WinPix-ThirdPartyNotices.txt
share/licenses/gkcore/NVAPI-SDK-License.pdf
share/licenses/gkcore/LICENSE-LLVM.txt
share/licenses/gkcore/LICENSE-MIT.txt
share/licenses/gkcore/LICENSE-MS.txt
share/licenses/gkcore/stb-image-LICENSE.txt
share/licenses/gkcore/cgltf-LICENSE.txt
```

Allowlist はこの一覧以外の Runtime file を拒否します。対象 DLL の由来と個別ライセンス文書は CMake install rules と The Forge source tree からコピーします。The Forge と DXC の固定版は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) に記録されています。gkcore 自身の配布ライセンスは未決定で、依存の配布条件を含む最終 audit も未完了です。したがって、この一覧は検査契約であり、再配布許可が確定したという意味ではありません。

## 開発用 checkout

```text
include/                   # 公開 API
src/                       # 実装と内部コード
src/backends/              # Windows / The Forge renderer
tests/                     # 契約・統合・package 検査
examples/                  # 学習用コード
tools/                     # The Forge checkout/build と検査スクリプト
cmake/                     # build/install 設定
docs/                      # API・運用・仕様
build/forge/                # The Forge の Renderer.lib / OS.lib build output
```

The Forge の source checkout は `GKCORE_FORGE_ROOT` で指定し、CMake が暗黙に取得することはありません。`tools/build_forge.py` は固定 source から `Renderer.vcxproj` と `OS.vcxproj` を Release|x64 で build します。OS project の FSL targets が依存側の shader を生成します。DXC 1.8.2405 package は checksum を検証して `.devtools/` に置き、shader compiler と FSL scripts は開発環境だけにします。`tools/build_gkcore_shaders.py` は gkcore の内蔵 color / sprite shaders をコンパイルし、Runtime package には shader binaries と root signatures のみを入れます。DXC の runtime DLL と license file は現在の Runtime allowlist に明記されています。

## 配布検証

Runtime build は Windows/MSVC x64、verified The Forge checkout、Renderer/OS libraries、および各依存 payload が必要です。package test は install manifest を allowlist と照合します。公開前には Runtime SDK のみを使う downstream sample をクリーンな Windows 10/11 環境で build/run し、全 DLL と第三者通知の再配布条件を audit してください。
