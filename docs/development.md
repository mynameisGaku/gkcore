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

CMake による開発用 build 設定と、The Forge を使う Windows 描画部があります。Windows 実機での Runtime build と GPU 描画はまだ確認していません。Linux の CPU テストは API の約束を検査しますが、GPU の表示結果は保証しません。

## Windows GPU smoke と画面の目視確認

DX12 対応 GPU を搭載した Windows PC で次を実行すると、通常の build/test に加えて、Windows 描画 smoke を CTest で実行します。

```bat
PRE_SETUP.bat --gpu-check
```

この smoke は初期化、カスタムポスト shader の有効化、960×540 へのサイズ変更、Scene と UI を含む描画命令の `Present`、ポスト shader の無効化・再有効化、無効化後の削除、終了と再初期化を確認します。API の戻り値とウィンドウの client size を検査しますが、画素の読み戻しは行わないため、色や UI が期待どおりに見えることまでは判定しません。

色の変化を目で確かめるには、同じ build が作る `build\runtime-windows\Release\gkcore_custom_post_effect.exe` を起動します。ウィンドウを 960×540 以上に保ち、Space キーでポスト効果を切り替えてください。有効時には Scene の三角形と矩形の色が変わり、無効時には元の色に戻ります。緑の UI 矩形と画面下部の ON/OFF 表示は Scene の効果に影響されず、ウィンドウをリサイズしても表示されることを目で確認します。Escape キーで終了します。

輪郭矩形の例 `build\runtime-windows\Release\gkcore_rectangle_outline.exe` では、塗りつぶし矩形、`DrawRect(..., false)` の 1 ピクセル幅、`gk::DrawRectOutline` の太線、Scene と UI の描画を見比べられます。Space キーで Bloom を切り替え、Escape キーで終了します。

モデル照明サンプル `build\runtime-windows\Release\gkcore_model_lighting.exe` は、サンプルの GLB を読み込み、非金属・滑らかな球と金属・やや粗い球を並べて描きます。左 / 右キーで方向光を回し、Space で光の方向を約 90 度切り替えます。明るい側と暗い側が移動すること、2 種の材質で反射の色や広がりが異なることを確認します。Escape キーで終了します。

モデルファイルを実行ファイルと同じ場所から読み込むため、そのフォルダーへ移動して起動してください。

```bat
cd build\runtime-windows\Release
gkcore_model_lighting.exe
```

この操作手順は Windows DX12 実機で実行し、法線による明暗と材質の違いが表示されることを目視で確認してください。CPU 契約テストや shader / C++ 構文検査は画面表示の確認を代替しません。Runtime の Windows/MSVC link と GPU 上の見た目は未確認です。GPU smoke 自体も画素読み戻しをしないため、手動の目視確認が必要です。

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

2026-10-05 に Windows x64、Visual Studio 18 2026 / MSVC 19.51.36260.0、Windows SDK 10.0.28000.0、CMake 4.3.1、Python 3.11.9 で実行し、Debug と Release の全 target build、および CTest 26/26 件が成功しました。Windows コンパイラでの CPU 契約と開発用 target の link を確認した結果です。Runtime を無効にしているため、The Forge を含む Runtime の link、DX12 実行、画面表示を確認した結果ではありません。実行ログは `build/dev-windows/` にあります。
