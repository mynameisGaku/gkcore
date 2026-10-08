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

Windows 11 Pro、Visual Studio 2026 / v142、Windows SDK 10.0.22621.0、RTX 4070 SUPER / driver 610.74で、Release・Debug Runtimeの全CTestは各57/57件が成功しました。Debugの画像取得ではD3D12 InfoQueueの取得を必須確認しています。金属度・粗さの25画像に加え、明示NORMAL/TANGENTを使う法線画像と画像共有の38画像も検査し、Release/Debug間でbyte単位に一致しました。これは全面画像の画質やGPU-based validationの確認ではありません。他GPUでの実行と異常終了・device loss時の復旧は未検証です。最新の結果は[TDD検証ログ](TDD_LOG.md)に記録しています。

固定したThe Forgeでは開発用のshader reloadが有効で、Runtimeには含めない`reload-server.txt`がない旨のエラーがログに出ます。ReleaseとDebugの実行はその後も継続し、全テストが成功しました。現在はこの開発用機能の無効化を整理していません。

## Windows GPU smoke と画面の目視確認

DX12 対応 GPU を搭載した Windows PC で次を実行すると、通常の build/test に加えて、Windows 描画 smoke を CTest で実行します。

```bat
PRE_SETUP.bat --gpu-check
```

Debug Runtimeを含む全テストとGPU smokeを実行する場合は、Debug専用のbuild rootを使う次のコマンドを実行します。

```bat
PRE_SETUP.bat --configuration Debug --gpu-check
```

Visual Studio用CMakeを直接実行する場合も、Debug rootは`CMAKE_CONFIGURATION_TYPES=Debug`だけ、Release rootは`CMAKE_CONFIGURATION_TYPES=Release`だけにし、同じ構成のThe Forge buildを`GKCORE_FORGE_BUILD_DIR`へ指定します。DebugとReleaseを同じrootへ混在させると、Debug専用のSDK Layers配置条件が成立しません。`PRE_SETUP.bat`は構成ごとにrootと依存buildを分けてこの条件を設定します。

2026-10-05に`PRE_SETUP.bat --gpu-check`を再実行し、依存物の照合、Forgeとshaderのビルド、Release Runtimeとサンプル、全CTest 34/34が成功して`BUILD READY`になりました。GPUはRTX 4070 SUPER、driverは610.74です。GPU smokeは2.46秒、5 modeの画素検査は5.95秒、SDK consumer GPU smokeは3.77秒、全体は15.05秒でした。最終ログは`build/native-validation/pre-setup-render-final.log`です。

`gkcore.backend_smoke`は初期化、カスタムポスト shader、960×540へのresize、Scene/UI描画のPresent、shaderの無効化・再有効化と削除、終了・再初期化をAPIとclient sizeで確認します。別の`gkcore.render_capture`が最終swapchain画像を読み戻し、2D/3D、UI、日本語文字、tint、モデル球の色と照明変化を検査します。固定領域の画素検査は全面画像の画質判定ではありません。2026-10-07の追加検証では、同一アプリ内の6フレームを取得してポスト効果の有効・無効切り替えとUIの色維持も確認しました。Release全CTestは34/34件成功しています。ログは`build/native-validation/input-stress-final-{build,tests}.log`です。取得画像と方法は[GPU描画検証](render-validation.md)を参照してください。

Debug Runtimeの確認では、Debug構成が`_DEBUG`からThe Forgeの`FORGE_DEBUG`と`ENABLE_GRAPHICS_VALIDATION`を有効にすることに加え、実行時のInfoQueueを確認します。`d3d12SDKLayers.dll`がDebug出力先にない最初の実行ではInfoQueueを取得できず、取得を必須にしたテストがPresentで失敗しました。固定したAgility SDKの同DLLをDebug出力先へstageした後、全13箇所でInfoQueueが有効と記録され、Debug全CTest 34/34が成功しました。Debug構成を選んだだけではSDK Layersの取得を保証しません。GPU-based validationは有効化していません。結果ログは`build/native-validation/pre-setup-debug-validated.log`（21.59秒）です。

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


## 押下の保持と連続描画

`WasKeyPressed`は直近の`ProcessEvents`で受信した押下を次の処理まで保持します。`IsKeyDown`は現在の押下中状態を返します。純CPU契約テストでは公開47キーのVK変換と0〜255の全入力slotを確認し、輪郭サンプルではSpace切り替えと短いEscape入力による終了を実際に操作しました。全キーを実画面から操作する検査は未実施です。[キー入力](input.md)を参照してください。

`gkcore.frame_stress`は123フレームの多数描画を実行し、119・120番目だけを取得します。取得前のフレームは通常の描画同期を使います。検査用の`GKCORE_TEST_CAPTURE_START_FRAME`は0〜65535、取得枚数は1〜16で、省略時は最初の1枚を取得します。これは性能の合否判定や全面画質評価ではありません。

2026-10-07のRelease全CTestは34/34、CPU RuntimeOFFのDebug/Releaseは各28/28成功しました。Runtimeはv142 / SDK22621、CPUはMSVC19.51 / SDK28000を使った別構成です。最新ログは`build/native-validation/input-stress-final-{build,tests}.log`、`input-cpu-{debug,release}-{build,tests}.log`です。


## 効果別のGPU画像テスト

`gkcore.post_effect_capture`は各効果の個別設定と、BeginFrameの前後での設定取り込みを実GPU画像で比較します。露出の明暗順、彩度0の無彩色化、Contrast0の共通中間値、Bloomのにじみ、FXAAの斜辺処理、UI領域の保持を独立した条件で判定します。新しい画像テストを含むRelease/Debugの全CTestは各35/35成功しました。対応する14画像も同GPU・driver内で一致しました。内容と再実行コマンドは[効果の使い方](effects.md)を参照してください。


## 内蔵モデルshaderの検査用ファイル

Runtimeへ配布するFSL出力はbinding名などの照合情報を削ります。DXC reflectionテストが使うモデル用の2ファイルは、次の開発用ツールで照合情報を残して生成します。Runtime出力の設定は変えません。

```bat
python tests/support/compile_model_shader_fixtures.py --forge-root .devtools/The-Forge --dxc-root .devtools/dxc-1.8.2405 --output-dir tests/assets/shaders
```

このツールは既存のFSLビルド機構と固定コンパイラーを使い、開発用buildフォルダーから`gkcore_model.vert`と`gkcore_model.frag`だけを検査用assetsへ保存します。追加された材質データは内部の頂点配置を160byteにし、vertex inputのTEXCOORD3とpixel inputのTEXCOORD4へ2成分のアルファ抜き情報、vertex inputのTEXCOORD4とpixel inputのTEXCOORD5へ材質画像のUVを渡します。法線マップにはvertex inputのTANGENT0、TEXCOORD5/6から接線・独立UV・有効値とscaleを渡し、pixel inputはTEXCOORD6/7/8です。DXCが2成分をregisterのxy/zwへ詰める場合も成分数と使用箇所を照合します。画像bindingは基本色t0、MR t1、法線t2、自己発光t3、遮蔽t4、基本色sampler s5、MR sampler s6、法線sampler s7、自己発光sampler s8、遮蔽sampler s9です。自己発光はvertex inputのTEXCOORD7/8から独立UVとRGB係数・強度を渡し、pixel inputはTEXCOORD9/10です。遮蔽はvertex inputのTEXCOORD9からUVと強度の3成分を渡し、pixel inputはTEXCOORD11/12です。内部の頂点は14属性で、固定Forgeの15属性・TEXCOORD0〜9の範囲内に収めています。公開カスタムshaderの入力は変更していません。

2026-10-08のGLB自己発光ではfactor・strength・sRGB画像、独立UV・sampler・ミップ・MASK、HDR/Bloom、Scene/UIを45画像で確認し、Release/Debug間でbyte単位一致しました。RuntimeOFF Debug/Releaseは各35/35、配布SDK consumerも両Runtime構成で成功しています。自己発光の追加と検査方法は[モデルガイド](models.md#glbの自己発光)、実行結果は[TDD検証ログ](TDD_LOG.md)に記録しています。

2026-10-08のGLB環境遮蔽画像では、強度・UV・sampler・ミップ・MASKと、方向光・自己発光への非適用を43画像で確認しました。Release/Debugは全CTest各53/53、RuntimeOFFは各36/36成功しています。43画像は両Runtime構成でbyte単位一致し、各参照比較は全640×480を検査します。使い方は[モデルガイド](models.md#glbの環境遮蔽画像)、実行結果は[TDD検証ログ](TDD_LOG.md)を参照してください。

GLBの接線生成は `src/model/ModelTangents.cpp` がprimitive-local位置・法線・法線画像のUVからcorner frameを作り、`GlbLoader.cpp` がnode変換と出力indexへの再割当を担当します。固定MikkTSpaceは `MikkTSpace.cpp` で一度だけコンパイルし、sourceの改変なしで割当をfoundationへ接続します。`gkcore.model_tangent_generation` と `gkcore.model_tangent` はCPU契約、`gkcore.model_tangent_capture` は実GPU画像、`gkcore.mikktspace_lock` とpackage検査は固定コードと配布noticeを確認します。

GLB接線自動生成の追加後、Runtime Release/Debugは全CTest各57/57、RuntimeOFF Debug/Releaseは各39/39成功しました。追加32画像と判定JSONは両Runtime構成で完全一致しています。SDK consumerとnotice必須の配布物検査も両構成で確認しました。条件と結果は[TDD検証ログ](TDD_LOG.md)に記録しています。
