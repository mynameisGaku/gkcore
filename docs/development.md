# 開発と TDD

gkcore は小さな描画 API の約束を、依存の少ないテストで先に固定して進めます。機能追加では次の順番を使います。

1. 利用例から期待する動きを決め、失敗するテストを書く。
2. 最小の実装でそのテストを通す。
3. 重複を整理し、全テストを再実行する。
4. The Forge や Windows 固有の統合がある場合は、契約テストと実機/統合確認を分けて記録する。

たとえば `gk::DrawRect` と `gk::DrawTriangle3D` を呼んだ順番が、内部のフレーム命令にも保たれること、存在しないリソース handle を描こうとしたら説明可能なエラーになることは、GPU を使わずにテストできます。画面に正しく見えるかどうかは、別途 backend を動かす統合確認が必要です。

## 開発時に分けて置く物

テスト、モックバックエンド、The Forge の依存物、シェーダーコンパイラ、サンプル、ビルド・パッケージ用スクリプトは開発専用です。ゲーム開発者向けランタイムの配布物には含めません。厳密なマニフェスト例は [package-layout.md](package-layout.md) に記載します。

Runtime の公開 API、実装、利用者向けサンプルでは C++ 標準ライブラリ/STL を使わない方針です。テスト、開発ツール、配布物や shader の検査スクリプトでは標準ライブラリを利用できます。Runtime のウィンドウとイベント、描画部、リソース、効果と shader は責務を分け、公開 header と実装を対応するファイルに置きます。共有 handle、vector、string、container は foundation module が管理します。

## ソースの書式

自作ソースはUTF-8 BOM付き・CRLFで保存します。型・関数・制御文の開始中カッコは次の行に置き、短いブロックも1行に畳みません。関数呼び出しや条件の括弧、初期化子の内部は1行に揃えます。コメントは自然な日本語にし、exampleやバージョンなどの一般的な用語はそのまま使います。

ルートの `.clang-format` を使って整形します。固定した開発用依存物にはclang-format 12が含まれています。対象は変更する自作コードに限定し、`third_party`や取得済み依存物を一括整形しないでください。FSLのSRT宣言など、formatterが構文を扱えないマクロは元の階層を維持します。

Windows APIも使うコードでは、Windows.hを先に読み込み、その後にgkcore.hを読み込んでください。gkcore.hは公開APIとの衝突を避けるためLoadImageマクロを除去します。Windows側の画像読み込みを呼ぶ場合はLoadImageWまたはLoadImageAを明示します。

## ブランチの管理

開発中の変更は `dev` に機能単位でコミットし、検証済みの変更をリモートの `dev` へ push します。レビューと必要な検証を通った安定区切りを `dev` から `main` に反映します。

## 現在の制限

Windows 11 Pro、Visual Studio 2026 / v142、Windows SDK 10.0.22621.0、RTX 4070 SUPERで、Release RuntimeとCTest 32/32が成功しました。GPU画素検査はdefault/direct/tint/model/model_rotatedの5 modeを固定領域・色・照明変化で確認し、mixed_sceneとmodel_lightingの表示も目視しました。全面画像による画質acceptance、Runtime Debug、別GPUでの実行は未確認です。

固定したThe Forgeでは開発用のshader reloadが有効で、Runtimeには含めない`reload-server.txt`がない旨のエラーがログに出ます。今回のRelease実行はその後も継続し、全テストが成功しました。現在はこの開発用機能の無効化を整理していません。

## Windows GPU smoke と画面の目視確認

DX12 対応 GPU を搭載した Windows PC で次を実行すると、通常の build/test に加えて、Windows 描画 smoke を CTest で実行します。

```bat
PRE_SETUP.bat --gpu-check
```

2026-10-05に`PRE_SETUP.bat --gpu-check`を再実行し、依存物の照合、Forgeとshaderのビルド、Release Runtimeとサンプル、全CTest 32/32が成功して`BUILD READY`になりました。GPUはRTX 4070 SUPER、driverは610.74です。GPU smokeは2.46秒、5 modeの画素検査は5.95秒、SDK consumer GPU smokeは3.77秒、全体は15.05秒でした。最終ログは`build/native-validation/pre-setup-render-final.log`です。

`gkcore.backend_smoke`は初期化、カスタムポスト shader、960×540へのresize、Scene/UI描画のPresent、shaderの無効化・再有効化と削除、終了・再初期化をAPIとclient sizeで確認します。別の`gkcore.render_capture`が最終swapchain画像を読み戻し、2D/3D、UI、日本語文字、tint、モデル球の色と照明変化を検査します。固定領域の画素検査は全面画像の画質判定ではありません。取得画像と方法は[GPU描画検証](render-validation.md)を参照してください。

色の変化を目で確かめるには、同じ build が作る `build\runtime-windows\Release\gkcore_custom_post_effect.exe` を起動します。ウィンドウを 960×540 以上に保ち、Space キーでポスト効果を切り替えてください。有効時には Scene の三角形と矩形の色が変わり、無効時には元の色に戻ります。緑の UI 矩形と画面下部の ON/OFF 表示は Scene の効果に影響されず、ウィンドウをリサイズしても表示されることを目で確認します。Escape キーで終了します。

輪郭矩形サンプル `build\runtime-windows\Release\gkcore_rectangle_outline.exe` では、塗りつぶし矩形、`DrawRect(..., false)` の 1 ピクセル幅、`gk::DrawRectOutline` の太線、Scene と UI の描画を見比べられます。Space キーで Bloom を切り替え、Escape キーで終了します。

モデル照明サンプル `build\runtime-windows\Release\gkcore_model_lighting.exe` は、サンプルの GLB を読み込み、非金属・滑らかな球と金属・やや粗い球を並べて描きます。左 / 右キーで方向光を回し、Space で光の方向を約 90 度切り替えます。明るい側と暗い側が移動すること、2 種の材質で反射の色や広がりが異なることを確認します。Escape キーで終了します。

モデルファイルを実行ファイルと同じ場所から読み込むため、そのフォルダーへ移動して起動してください。

```bat
cd build\runtime-windows\Release
gkcore_model_lighting.exe
```

2026-10-05に通常Runtime DLLでmodel_lightingを起動し、左右の球の色、明暗、反射を目視確認しました。GPU画素検査でも基準方向と90度回転方向の色画素と変化量を確認しています。Spaceによる方向切り替えとEscape終了の自動操作は未確認です。検査範囲と画像は[GPU描画検証](render-validation.md)を参照してください。

## 開発テストの実行

リポジトリの開発用テストは次のコマンドで構成・ビルド・実行する設計です。

```sh
cmake -S . -B build -DGKCORE_BUILD_RUNTIME=OFF -DGKCORE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows Runtime の build には指定バージョンの The Forge、DXC 1.8.2405、Windows SDK 10.0.22621.0、Visual Studio 2022 または Visual Studio 2026 の C++ 開発環境と v142 14.29 toolset が必要です。CMake の要件は Visual Studio 2022 では 3.21 以降、Visual Studio 2026 では 4.2 以降です。`PRE_SETUP.bat` は前提を確認し、依存物を取得して The Forge の library/shader、gkcore Runtime/sample、CTest を build します。GPU smoke は必要な場合にだけ有効にします。Linux では Runtime を無効にして CPU テストを実行します。

固定したThe ForgeのGUID定義が新しいWindows SDKと重複するため、現在のRuntimeビルドではSDK 10.0.22621.0を選びます。Visual Studio Installerの個別コンポーネントでWindows 11 SDK (10.0.22621.0)を追加してください。CIでも同じ[Microsoft公式のcomponent ID](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=visualstudio)を使って導入します。これは現在の依存バージョンの互換条件です。SDKを変更する場合はThe Forgeとgkcoreを同じSDKで再ビルドし、CMakeの生成したprojectでも選択値を確認します。

Windows でも The Forge を使わない CPU 契約テストを構成・ビルド・実行できます。Visual Studio 2026 x64 の例です。

```powershell
cmake -S . -B build/dev-windows -G "Visual Studio 18 2026" -A x64 `
  -DGKCORE_BUILD_RUNTIME=OFF -DGKCORE_BUILD_TESTS=ON
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```

2026-10-05 に Windows x64、Visual Studio 18 2026 / MSVC 19.51.36260.0、Windows SDK 10.0.28000.0、CMake 4.3.1、Python 3.11.9 で実行し、Debug と Release の全 target build、および CTest 27/27件が成功しました。Windows コンパイラでの CPU 契約と開発用 target の link を確認した結果です。Runtime を無効にしているため、The Forge を含む Runtime の link、DX12 実行、画面表示を確認した結果ではありません。実行ログは`build/native-validation/cpu-{build,test}-{debug,release}-capture.log`にあります。
