# 配布物と開発物

ゲーム開発者へ渡す Runtime SDK は、アプリの build/run に必要なファイルに限定します。テスト、The Forge source/headers、FSL/DXC compiler、ビルド用スクリプト、サンプル asset は開発 checkout に置き、Runtime package allowlist で混入を拒否します。

## Runtime SDK の許可ファイル

現行の `tests/package/package_allowlist.py` がRelease構成で要求するファイル一覧です。Debug構成では、Debug Runtimeの起動に必要な`bin/d3d12SDKLayers.dll`だけを追加で要求します。

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
bin/gpu.data
bin/gpu.cfg
bin/CompiledShaders/DIRECT3D12/gkcore_color.vert
bin/CompiledShaders/DIRECT3D12/gkcore_color.frag
bin/CompiledShaders/DIRECT3D12/gkcore_sprite.vert
bin/CompiledShaders/DIRECT3D12/gkcore_sprite.frag
bin/CompiledShaders/DIRECT3D12/gkcore_model.vert
bin/CompiledShaders/DIRECT3D12/gkcore_model.frag
bin/CompiledShaders/DIRECT3D12/gkcore_post.vert
bin/CompiledShaders/DIRECT3D12/gkcore_bloom_extract.frag
bin/CompiledShaders/DIRECT3D12/gkcore_bloom_blur.frag
bin/CompiledShaders/DIRECT3D12/gkcore_post_composite.frag
bin/CompiledShaders/DIRECT3D12/gkcore_fxaa.frag
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
share/licenses/gkcore/ufbx-LICENSE.txt
share/licenses/gkcore/mikktspace-LICENSE.txt
```

Debug構成では固定したAgility SDKから`d3d12SDKLayers.dll`をDebug Runtime出力とDebug installへだけstageします。Release構成には含めません。この記載はDebug検証に必要な配置を示すもので、DLLの再配布条件が確定したという意味ではありません。Allowlistは各構成で定めたファイル以外のRuntime fileを拒否します。対象DLLの由来と個別ライセンス文書はCMake install rulesとThe Forge source treeからコピーし、固定版は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)に記録されています。gkcore自身の配布ライセンスは未決定で、依存の配布条件を含む最終auditも未完了です。したがって、この一覧は検査契約であり、再配布許可が確定したという意味ではありません。

`gpu.data` はGPUの識別情報と初期プリセット、`gpu.cfg` はGPU選択と設定規則をThe Forgeへ渡します。Runtime起動時にThe ForgeがGPU設定を読み込むため、両方を`bin`へ配置します。固定The Forge checkoutの `Common_3/OS/Windows/pc_gpu.data` と `Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg` を使い、独自設定は加えません。

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
build/forge/                # Release The Forge Renderer.lib / OS.lib
build/forge-debug/          # Debug The Forge Renderer.lib / OS.lib
build/runtime-windows/      # Release Runtime build root
build/runtime-windows-debug/ # Debug専用Runtime build root
```

The Forge の source checkout は `GKCORE_FORGE_ROOT` で指定し、CMake が暗黙に取得することはありません。`tools/build_forge.py` は固定 source から `Renderer.vcxproj` と `OS.vcxproj` を指定した構成で build します。OS project の FSL targets が依存側の shader を生成します。DXC 1.8.2405 package は checksum を検証して `.devtools/` に置き、shader compiler と FSL scripts は開発環境だけにします。FBX 読み込み用 ufbx v0.23.1 のソースも開発用 checkout に置き、Runtime package には配布条件に必要な `ufbx-LICENSE.txt` のみを含めます。GLBの接線生成には固定MikkTSpaceを静的に組み込み、source/headerは開発checkoutに、`mikktspace-LICENSE.txt` はRuntime SDKに置きます。`tools/build_gkcore_shaders.py` は gkcore の内蔵 color、sprite、model lighting、post、Bloom、FXAA shaders をコンパイルし、Runtime package にはコンパイル済み shader と root signatures のみを入れます。DXC の runtime DLL と license file は現在の Runtime allowlist に明記されています。

## Windowsの構成別build

既定のRelease構成は`build/runtime-windows`と`build/forge`を使います。Debug構成は`build/runtime-windows-debug`と`build/forge-debug`を使い、CMakeの`CMAKE_CONFIGURATION_TYPES`もそれぞれ`Release`だけ、`Debug`だけに限定します。Visual StudioのDebug RuntimeとThe Forge Debug library/headerを同じ構成でbuildし、CRT設定を揃えます。既定の`Debug;Release;...`を含む同じmulti-config rootは使わず、対応するThe Forge build rootを`GKCORE_FORGE_BUILD_DIR`に指定してください。`PRE_SETUP.bat`は構成ごとにこれらを自動選択します。

Debugでは`_DEBUG`からThe Forgeの`FORGE_DEBUG`、さらに`ENABLE_GRAPHICS_VALIDATION`が有効になります。ただし、DebugのマクロだけではD3D12検証層の取得を保証しません。固定Agility SDKの`d3d12SDKLayers.dll`がDebug出力先にないと、InfoQueueを取得できず最初のPresentを検証するテストが失敗します。DLLはDebug構成だけにstageし、Release packageには含めません。GPU-based validationはこの検証では有効化していません。

## 配布検証

Runtime build は Windows/MSVC x64、verified The Forge checkout、Renderer/OS libraries、および各依存 payload が必要です。package test は install manifest を allowlist と照合します。公開前には Runtime SDK のみを使う downstream sample をクリーンな Windows 10/11 環境で build/run し、全 DLL と第三者通知の再配布条件を audit してください。
