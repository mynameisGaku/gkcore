# 第三者コンポーネントと配布条件

この文書は、Runtime の許可リストにある第三者ファイルと、固定した The Forge checkout / DXC package のライセンス文書を照合した記録です。確認できた許諾、条件付きの許諾、条件を特定できていない項目を分けて記します。

## gkcore

gkcore 自身の配布ライセンスは未決定です。このリポジトリは利用者に著作権許諾や再配布権を付与するライセンス文書を含みません。

## The Forge

- 固定リポジトリ: [ConfettiFX/The-Forge](https://github.com/ConfettiFX/The-Forge)
- 固定 commit: [`cd5046893faba2dc7869243873bf01f02a6f0df9`](https://github.com/ConfettiFX/The-Forge/commit/cd5046893faba2dc7869243873bf01f02a6f0df9)、`cmake/forge-files-lock.json` で検証
- [LICENSE](https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/LICENSE): Apache License 2.0

固定したソースは開発用 checkout に置き、gkcore のリポジトリへ複製しません。The Forge 本体の LICENSE と各内包依存の notice は別物です。下記の各コンポーネント条件も適用されます。

## The Forge から同梱するコンポーネント

| コンポーネント | Runtime でのファイル | 根拠と確認した条件 |
|---|---|---|
| D3D12 Memory Allocator | The Forge がリンクするコード | [`LICENSE.txt`](https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/Common_3/Graphics/ThirdParty/OpenSource/D3D12MemoryAllocator/LICENSE.txt) は AMD copyright 2019-2022 の MIT License。別ファイル [`NOTICES.txt`](https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/Common_3/Graphics/ThirdParty/OpenSource/D3D12MemoryAllocator/NOTICES.txt) には AMD code、Microsoft DirectX Graphics Samples、Vulkan Memory Allocator の MIT notice がある。配布時には両方を含める。 |
| DirectX 12 Agility SDK | `D3D12Core.dll` | 固定ソースの [`distributable files.txt`](https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/Common_3/Graphics/ThirdParty/OpenSource/Direct3d12Agility/distributable%20files.txt) に記載。`LICENSE.txt` は `build/native/bin/` の DLL、`LICENSE-CODE.txt` は `build/native/include/` の header に適用。Microsoft 条項は、アプリで主要機能を加えること、配布先/利用者に Microsoft を保護する条件への同意を求めること、一定の請求に対する補償を含む。 |
| AMD AGS | `amd_ags_x64.dll` | 固定ソースの [`LICENSE.txt`](https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/Common_3/Graphics/ThirdParty/OpenSource/ags/ags_lib/LICENSE.txt) は AMD copyright 2023 の MIT License。 |
| NVIDIA NVAPI | gkcore の Runtime file allowlist には PDF のみ。The Forge renderer は NVAPI を初期化し、driver 情報を参照する。 | 固定ソースの [公開 SDK 契約 PDF](https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/Common_3/Graphics/ThirdParty/OpenSource/nvapi/docs/NVAPI_SDKs_Samples_and_Tools_License_Agreement%28Public%29.pdf) の 1.1(ii) は、API library と header の binary 再配布を NVIDIA 対応 GPU 向けの製品に組み込む場合に認める一方、配布先と利用者に契約上の義務を課す法的拘束力のある契約を求める。Runtime に PDF はあるが、gkcore の利用者契約へこれらの条件をどう反映するかは未解決。 |
| WinPix Event Runtime | `WinPixEventRuntime.dll` | The Forge が固定する package `1.0.200127001` の利用許諾は下記のとおり未確定。後年の Microsoft 版で確認できた条件と、この固定版との関係を分けて記録する。 |

Agility、NVAPI、DXC の条項では配布先に課す契約条件が定められています。現行 SDK にその条件を組み込む利用者契約はありません。WinPix の固定版に適用される条件も確定していません。

### WinPix Event Runtime の版別資料

- The Forge の固定 nuspec は package `1.0.200127001` と Microsoft WebPI の `licenseUrl` を記しています。リンク先は現在、一般の Web 開発案内へ転送され、EULA を表示しません。The Forge に入っている [`ThirdPartyNotices.txt`](https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/Common_3/Graphics/ThirdParty/OpenSource/winpixeventruntime/ThirdPartyNotices.txt) も「informational purposes only」とし、利用許諾文書とは区別しています。
- Microsoft の [DirectML ThirdPartyNotices](https://github.com/microsoft/DirectML/blob/8700779fe7a09ea7a007cf3d7ab4293c78e41017/DxDispatch/ThirdPartyNotices.txt#L307-L385) には `PIX EVENT RUNTIME FOR WINDOWS` の Microsoft Software License Terms 全文があります。object code の配布は認められますが、アプリに主要機能を加えること、配布先と利用者に同等以上に保護する条件への同意を求めること、copyright notice の表示、Microsoft への補償が条件です。同じ Microsoft リポジトリの [Guide](https://github.com/microsoft/DirectML/blob/8700779fe7a09ea7a007cf3d7ab4293c78e41017/DxDispatch/doc/Guide.md) は `1.0.230302001`、[サンプルの package manifest](https://github.com/microsoft/DirectML/blob/8700779fe7a09ea7a007cf3d7ab4293c78e41017/Samples/yolov4/packages.config) は `1.0.210209001` を示しています。この EULA は固定版より後の版での Microsoft 公開根拠ですが、`1.0.200127001` に対する直接の根拠ではありません。
- Microsoft の [PixEvents README](https://github.com/microsoft/PixEvents/blob/b0caa735f8510f4ff60c29ef1c88101620defc5e/README.md) は NuGet package が 2024 年 3 月時点で MIT license と説明します。Microsoft の [vcpkg manifest](https://github.com/microsoft/vcpkg/blob/3b156cbdecf72d1032e8e1c80d2de97a0c40a285/ports/winpixevent/vcpkg.json) は新しい `1.0.240308001` を MIT と記し、[同版を取得する portfile](https://github.com/microsoft/vcpkg/blob/3b156cbdecf72d1032e8e1c80d2de97a0c40a285/ports/winpixevent/portfile.cmake) は package 内の `license.txt` をコピーしています。これは旧版の許諾を証明せず、古い EULA を MIT に置き換える根拠にはなりません。
- 公開 repository には `PIX EVENT RUNTIME FOR WINDOWS` と題する EULA の複製もありますが、Microsoft の一次資料ではなく package `1.0.200127001` との版対応も記されていません。この複製を固定版の許諾根拠とは扱いません。従って、固定版の正確な Microsoft 条項はまだ特定できていません。

## DirectX Shader Compiler (DXC)

- 固定版: `1.8.2405`、archive URL と SHA-256 は [`cmake/dxc-lock.json`](../cmake/dxc-lock.json) に記録。
- 公式 package: [Microsoft/DirectXShaderCompiler v1.8.2405](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2405)
- Windows package の `README.md` は `LICENSE-LLVM.txt` を DXC package 内のその他のファイル、`LICENSE-MS.txt` を `dxil.dll`、`LICENSE-MIT.txt` を `d3d12shader.h` に適用すると記す。Runtime allowlist には `dxcompiler.dll` と `dxil.dll` を含め、compiler `dxc.exe` と header は含めない。
- Microsoft の `LICENSE-MS.txt` は Agility と同様に、主要機能を加えること、配布先/利用者に保護条項への同意を求めること、一定の請求に対する補償を配布条件とする。`dxil.dll` の再配布についてもこの条件を考慮する必要がある。

## ufbx

- 固定版: ufbx `0.23.1`、commit [`26a482ae66871d7de36eb722aa060bce95bce274`](https://github.com/ufbx/ufbx/commit/26a482ae66871d7de36eb722aa060bce95bce274)。開発用の `third_party/ufbx/` に upstream の `ufbx.h` と `ufbx.c` を保存し、`third_party/ufbx/README.md` に SHA-256 を記録しています。
- 上流の [`LICENSE`](https://github.com/ufbx/ufbx/blob/26a482ae66871d7de36eb722aa060bce95bce274/LICENSE) は MIT License または Unlicense のいずれかを選べる二重ライセンスです。gkcore は MIT alternative の下で ufbx を使用します。Runtime SDK には `ufbx-LICENSE.txt` として原文を含め、parser source/header は含めません。
- ufbx は gkcore の FBX import 実装へ静的に組み込まれます。MIT 条項に従い、著作権表示と許諾文を Runtime package に含めます。

## MikkTSpace

- 固定commit: [`3e895b49d05ea07e4c2133156cfa94369e19e409`](https://github.com/mmikk/MikkTSpace/tree/3e895b49d05ea07e4c2133156cfa94369e19e409)。`third_party/mikktspace/` のsource/headerは上流の原文を変更せず保存し、READMEにSHA-256を記録しています。
- 許諾と条件は上流の [mikktspace.h](https://github.com/mmikk/MikkTSpace/blob/3e895b49d05ea07e4c2133156cfa94369e19e409/mikktspace.h) 冒頭にあります。原著者を偽らないこと、改変したsourceはその旨を明示すること、source配布からnoticeを削除・変更しないことが条件です。
- 接線生成処理へ静的に組み込み、Runtime SDKには原文の著作権表示と許諾を `mikktspace-LICENSE.txt` として保存します。source/headerは開発checkoutへ置き、Runtime SDKには入れません。

## Forge の Renderer / OS 静的ライブラリ

固定 The Forge の `Renderer.vcxproj` と `OS.vcxproj` は静的ライブラリを build し、gkcore はその両方をリンクします。`OS.vcxproj` には ImGui、Lua 5.3.5、cpu_features、hidapi、bstrlib、LZ4、Zstandard の source が含まれます。これらの一部は MIT、BSD、Apache-2.0 のライセンス文書を持ちますが、実際に gkcore.dll へ取り込まれた object は Windows/MSVC の link map を確認するまで特定できません。

hidapi の pinned checkout には `windows/hid.c` と `hidapi.h` がある一方、source が参照する `LICENSE.txt`、`LICENSE-gpl3.txt`、`LICENSE-bsd.txt`、`LICENSE-orig.txt` はありません。どの条件で当該 source が提供されているかをこの checkout だけから決めることはできません。従って、Renderer / OS のリンク済み binary まで含めた完全な第三者 license 棚卸しは未完了です。

## Runtime manifest と確認状況

Runtime allowlist は依存の DLL とライセンス/notice ファイルを列挙するもので、配布許諾を自動的に証明するものではありません。MIT と Apache-2.0 の本文が確認できること、Microsoft/NVIDIA 条件に配布先契約が必要なこと、固定版 WinPix の条項が未確認であることを区別します。静的ライブラリが実際に含む第三者 object も未確認です。gkcore 自身のライセンスも未決定です。

WinPix `1.0.200127001` が参照する WebPI EULA と配布条件の、版を特定できる Microsoft 資料は見つかっていません。後年 MIT と明記された `1.0.240308001` へ更新する場合も、The Forge との互換性と checksum を別途確認する必要があります。Agility/DXC/NVAPI の再配布時に必要な利用者契約の扱いも未解決です。依存一覧、該当する原文、Runtime に入れるファイルは [配布物と開発物](package-layout.md) も参照してください。
