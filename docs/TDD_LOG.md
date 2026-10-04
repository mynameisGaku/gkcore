# TDD 検証ログ

このログは実際に実行されたテスト結果を記録します。実装済みであることと、テストが実行済みであることを区別します。

## 2026-10-03: API・リソース・効果・shader helper 契約（旧 API 実装時）

- RED (実装 agent からの報告。reviewer は初期ログを独立再実行していません): core API の最初の g++ link は公開 API と `SetBackendForTesting` の未定義参照で失敗。resource link は `LoadImage` / `LoadModel` / `Has*` / `Delete*` / `ClearResources` の未定義参照で失敗し、共有画像リソース型の compile error も確認。effects 単独テストは `Reset` / `Current` 未定義で失敗。
- GREEN: 依存を含まない統合 C++17 テストを root が実行し、終了コード 0 を確認。以下は報告された実行コマンド。

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -DFORGEDX_TESTING=1 \
  -Iinclude -Isrc \
  tests/core_tests.cpp tests/resource_tests.cpp tests/effect_tests.cpp \
  src/core/ForgeDx.cpp src/resources/Resources.cpp \
  src/effects/Effects.cpp src/effects/Shaders.cpp \
  -o /tmp/gkcore-review-tests
/tmp/gkcore-review-tests
```

検査対象はモック backend を使う API/描画順、リソースファイル/handle、Scene/UI とポスト設定値、pixel shader handle と定数 snapshot の契約です。別途行った実装後の mutation sensitivity checks は、関数実装を一時的に外すとテストが失敗することの確認であり、tests-first RED evidence には数えません。次元上限のテスト前失敗は実装 agent から RED として報告されています。実 GPU 描画は検査していません。

## 2026-10-03: Runtime パッケージ許可リストと install 拒否契約

- テストは最小 Runtime manifest を受け入れ、開発/上流ファイル、core-only package、通知ファイル不足、未知 Runtime file、prefix escape を拒否する契約を持ちます。
- 初回 RED 実行の記録はありません。存在しない module の import error を RED として記載しません。
- GREEN: `python3 tests/package/test_allowlist.py` の 5 tests が成功したと root が確認。
- CMake/CTest には実 renderer がないとき Runtime install を拒否するテストがあります。最終 CTest で 3/3 tests が成功したと root が確認しました。
- 検査対象は候補ファイル一覧と Runtime の fail-closed 挙動です。実際の Windows Runtime SDK は生成していません。

## 2026-10-03: CMake/CTest 統合確認（旧 API 実装時）

以下の CTest 記録は namespace / Runtime source 分割を行う前の状態です。現在の checkout 全体が同じ結果になることを示す記録ではありません。

root が最終ソースで以下を実行し、build と CTest が exit 0、3/3 tests successful であることを確認しました。

```sh
/tmp/gkcore-review-tools/cmake/data/bin/cmake \
  -S /workspace/gkcore -B /tmp/gkcore-review-build \
  -G 'Unix Makefiles' -DFORGEDX_BUILD_RUNTIME=OFF -DFORGEDX_BUILD_TESTS=ON
/tmp/gkcore-review-tools/cmake/data/bin/cmake \
  --build /tmp/gkcore-review-build --parallel 4
/tmp/gkcore-review-tools/cmake/data/bin/ctest \
  --test-dir /tmp/gkcore-review-build --output-on-failure
```

CTest checks package allowlist cases, Runtime install refusal without a real renderer, and integrated core/resource/effects/shader-constant contracts.

同じ final source の strict warning build と ASan/UBSan test run も exit 0 でした。LeakSanitizer はこの環境で ptrace 非対応のため実施できていません。Windows/The Forge backend の build、WARP、実 GPU 画像確認は未実施です。

RED の初回出力全文や一部の実行コマンドは保存されていません。明示した RED は実装担当 agent の報告内容で、reviewer が直接確認した結果ではありません。

## 2026-10-03: shader handle constant isolation regression

- RED (effects implementation agent report; reviewer did not independently capture the first run): an A/B regression failed with `switching through the built-in shader must preserve other shader constants`. Selecting the built-in shader was clearing constants belonging to other live shader handles.
- GREEN (effects implementation agent report): constants now persist for other handles while the selected shader returns to built-in; the focused strict g++ suite exits 0 with no output.

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  tests/effect_tests.cpp src/effects/Effects.cpp src/effects/Shaders.cpp \
  /tmp/effects_test_main.cpp -o /tmp/effects_tests
/tmp/effects_tests
```

`/tmp/effects_test_main.cpp` was a temporary harness defining the test entry point and is not part of the repository. root の最終統合 CTest にもこの shader regression が含まれ、成功しています。

## 2026-10-03: effect setters before initialization

- RED (core agent report): a regression showed that valid effect settings accepted before application initialization were reset and did not reach the first frame.
- GREEN (core agent report): initialization now preserves those settings; the strict warning-clean C++ harness passes.
- Exact focused command and output were not supplied. Final integrated CTest after this fix is awaiting root confirmation.

## 2026-10-03: FSL artifact parser/reflection contract

- Added Python tests for the actual pinned The Forge `@FSL` container layout, derivative bounds, nested DXBC/DXIL container validation, planned primitive pixel-shader reflection, and rejection of FSL/DXC development compiler files in a Runtime manifest.
- GREEN: `python3 tests/shader_contract_tests.py` ran 7 tests and passed. The artifact tests use synthetic byte sequences; no compiled shader from the real FSL/DXC toolchain was available, so this does not verify a real output binary or GPU pipeline.
- Added `tests/backend_contract_tests.cpp` for Windows Runtime initialization, external window resize, queued rectangle + 3D triangle, present, and close, plus both Windows/gkcore header include orders. CMake creates the native executable and compile-only header checks on Windows; CTest runs the GPU smoke only when `GKCORE_RUN_BACKEND_SMOKE=ON`. The Windows targets were not compiled or executed in this environment. Renderer support for filled rectangles/triangles/model payloads is not a substitute for this host-GPU test.

## 2026-10-03: Runtime 初期化の繰り返しと Windows header 順序

Windows GPU smoke は、ウィンドウの大きさ変更、矩形・3D 三角形・文字列の同一フレーム描画、表示、`WM_CLOSE` の処理に加え、終了後の再初期化と次フレームの表示を確認します。最後に再度終了します。これは thread 全体に残る終了メッセージが次回初期化へ漏れないことも確認します。`Windows.h` を先に含む場合と、`gkcore.h` を先に含む場合の両方で `gk::ColorRGB` と `gk::DrawString` を呼ぶ compile-only test も用意しました。Windows / DX12 GPU 上ではまだ実行していません。

## 2026-10-03: shader parser とパッケージ手順の再確認

reviewer が `python3 tests/shader_contract_tests.py` を実行し、7 tests が成功しました。個別実行したパッケージ許可リスト 6 件、The Forge checkout 3 件、The Forge build 計画 2 件、shader build 計画 1 件、setup 計画 1 件、DXC lock 1 件もすべて成功しました。`g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only -Iinclude examples/mixed_scene.cpp` も成功しました。

この時点の `python3 tests/no_stl_tests.py` は `src/api/gkcore.cpp` 内の `std::` 使用を 162 件報告して失敗しました。これは後の no-STL 化より前の中間状態です。後述の更新版では同じ検査の 3 tests が通っています。各 Python test は個別に実行した結果で、CTest 全体の成功を示すものではありません。

## 2026-10-03: RawArray の領域拡張時における別名参照

- RED (foundation implementation agent report): AddressSanitizer が `RawArray::Append` で容量を拡張する際に、再確保で無効になった `const T&` を読み取る heap-use-after-free を検出しました。
- GREEN (foundation implementation agent report): 更新後の strict warning + ASan/UBSan harness は終了コード 0 でした。LeakSanitizer は無効にして実行しており、リーク検査の結果ではありません。

## 2026-10-03: gk namespace/API split and frame/resource lifetime contracts

- Tests were written before the replacement API implementation. Initial RED: `g++ -std=c++17 -Wall -Wextra -Werror -Iinclude -Isrc -DGKCORE_TESTING=1 -fsyntax-only tests/core_tests.cpp` failed because the new backend packet referenced `ImageResource` and `ModelResource` while the resource header still exposed the prior `forgedx::detail` contract. This was an integration-contract compile failure, not a rendering-behavior RED.
- A later meaningful RED from the resource API test found a valid GLB produced 3 vertices, 3 indices, 1 primitive, 2 materials, and 1 texture; the expected payload had one PBR material. Reusing the initial material slot and deduplicating equal material values made this test pass.
- GREEN: the strict combined dependency-light harness below builds and exits 0. It covers fixed-error reporting on allocation failure, invalid dimensions/geometry, resize propagation, camera snapshots per draw, scene/UI layers, effect frame snapshots, shader registration rollback and stale handles, queued image/model payloads surviving deletion, model transform snapshots, resource parsing/lifetimes, and shader artifact validation.

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Wpedantic -Iinclude -Isrc -DGKCORE_TESTING=1 \
  tests/core_tests.cpp tests/resource_tests.cpp tests/effect_tests.cpp \
  tests/foundation_tests.cpp tests/shader_artifact_tests.cpp \
  src/api/gkcore.cpp src/core/Context.cpp src/core/Frame.cpp src/core/Input.cpp \
  src/draw/Draw2D.cpp src/draw/Draw3D.cpp \
  src/foundation/Memory.cpp src/foundation/Array.cpp src/foundation/String.cpp src/foundation/RefCount.cpp \
  src/resources/Resources.cpp src/resources/ResourceIO.cpp \
  src/image/Image.cpp src/image/ImageLoader.cpp \
  src/model/Model.cpp src/model/ModelLoader.cpp src/model/Cgltf.cpp \
  src/effects/Effects.cpp src/effects/Shaders.cpp \
  src/render/Shaders.cpp src/render/Geometry.cpp -o /tmp/gkcore_core_tests
/tmp/gkcore_core_tests
```

- GREEN: `python3 tests/no_stl_tests.py` ran 3 tests and passed after replacing STL use in runtime resource loaders. This does not validate a Windows/MSVC build or GPU presentation.

## 2026-10-03: 実際の FSL/DXIL artifact の読み込み

- RED (effects implementation agent report): C++ test の初回 compile は `src/render/Shaders.h` と `ParseCompiledPixelShader` がまだ存在せず失敗しました。parser 実装後、空の DXIL program を受け入れる regression test も追加し、修正前に失敗することを確認したとの報告です。
- GREEN (effects implementation agent report): parser は固定した The Forge commit と DXC 1.8.2405 の生成物から pixel shader を読み、vertex shader を pixel shader として拒否します。破損した header/table、範囲外 offset、欠落した DXIL、壊れた DXBC part、空 program、失敗時の出力保持も検査します。Linux で DXC 実行ファイルへの一時 symlink を使って得た artifact であり、Windows shader build、root signature、GPU pipeline の検証ではありません。
- GREEN (effects implementation agent report): foundation と shader artifact C++ tests を strict warning と ASan/UBSan 付きでまとめて実行し、終了コード 0。LeakSanitizer は無効です。最新 CMake/CTest 全体はこの記録時点で再実行していません。

## 2026-10-03: RawArray::AppendRange の境界と alias 契約

- RED (foundation implementation agent report): `Array<uint32_t>` に `AppendRange` がないためテストが compile に失敗しました。
- GREEN (foundation implementation agent report): strict warning + ASan/UBSan の foundation と shader artifact suite が成功。外部範囲のコピー、null/zero、再確保をまたぐ自己参照、未使用容量まで伸びる自己参照、count/byte overflow、OOM 時に内容と storage pointer を保つケースを検査しています。
- 最新 combined harness の成功は agent から報告されましたが、正確な実行コマンドは共有されていません。Windows/MSVC や GPU 動作の検証ではありません。

## 2026-10-03: Runtime shader allowlist の内蔵 sprite artifact

- RED (build/packaging agent report): Runtime manifest の許可リスト検査は、新しく追加された `gkcore_sprite.vert` / `.frag` artifact を余分なファイルとして拒否しました。
- GREEN (build/packaging agent report): CMake install と package allowlist を更新し、color / sprite shader binaries と root signatures を Runtime 一覧に加えました。package allowlist test は成功したとの報告です。shader artifact の同梱は sprite 描画 pipeline が GPU で動くことを検証するものではありません。

## 2026-10-03: keyboard and mouse state queries

- Tests-first RED: after adding representative `Key::A`, arrow, digit, editing-key and mouse expectations plus a capture backend, `g++ -std=c++17 -Wall -Wextra -Werror -Wpedantic -Iinclude -Isrc -DGKCORE_TESTING=1 -fsyntax-only tests/core_tests.cpp` failed because those enum values and mouse API/backend methods did not exist yet. The run reported missing `Key::A`, `Key::ArrowUp`, `MouseButton`, `IsMouseButtonDown`, and non-overriding backend methods.
- GREEN: the full strict combined test harness passed after adding the key map, focus-gated mouse API, and native adapter contract. The test covers all digits and Latin letters, each arrow, common editing keys, modifiers, all three mouse buttons, invalid enum diagnostics, focus loss, and client coordinates.
- GREEN: `python3 tests/no_stl_tests.py` passed 3 tests. MinGW strict syntax checks passed for input/core/API, Windows header-order probes, and `WindowsWindow.cpp`; this is not a full MSVC/The Forge build or GPU test.

## 2026-10-03: レビュー担当による Release・メモリ検査（文字・描画統合前）

レビュー担当が STL を使わない実装への移行後に Release ビルドと CTest を実行し、11 件すべての成功を確認しました。続いて AddressSanitizer / UndefinedBehaviorSanitizer を有効にした Debug ビルドでも、同じ 11 件が成功しました。LeakSanitizer は無効です。

```sh
/tmp/forgedx-review-tools/cmake/data/bin/cmake \
  -S /workspace/gkcore -B /tmp/gkcore-review-build \
  -G 'Unix Makefiles' -DGKCORE_BUILD_RUNTIME=OFF -DGKCORE_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
/tmp/forgedx-review-tools/cmake/data/bin/cmake \
  --build /tmp/gkcore-review-build --parallel 4
/tmp/forgedx-review-tools/cmake/data/bin/ctest \
  --test-dir /tmp/gkcore-review-build --output-on-failure

/tmp/forgedx-review-tools/cmake/data/bin/cmake \
  -S /workspace/gkcore -B /tmp/gkcore-sanitize-build \
  -G 'Unix Makefiles' -DGKCORE_BUILD_RUNTIME=OFF -DGKCORE_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
/tmp/forgedx-review-tools/cmake/data/bin/cmake \
  --build /tmp/gkcore-sanitize-build --parallel 4
ASAN_OPTIONS=detect_leaks=0 /tmp/forgedx-review-tools/cmake/data/bin/ctest \
  --test-dir /tmp/gkcore-sanitize-build --output-on-failure
```

この結果には、後から追加した sprite のキャッシュ・ポスト処理・文字描画の統合テストを含みません。Windows のリンクや実機描画も検証していません。

`RawArray::AppendRange` と確保失敗時の契約は、別途 strict warning と ASan / UBSan を付けた foundation 単独テストをレビュー担当が実行し、成功しました。自己参照を含む一括追加と、失敗時に既存の内容を維持するケースが対象です。

モデル読み込みのレビューでは、`g++ -c -Wframe-larger-than=32768` が OBJ 読み込み関数のスタック領域を 1,049,072 bytes と報告しました。Windows の標準スタック容量に対して大きいため、ヒープ上の行バッファへ変更する修正対象としました。`-fsyntax-only` ではこの検査を代用できません。

## 2026-10-03: UTF-8 システムフォント文字描画

- Tests-first RED: 日本語 UTF-8、malformed UTF-8、範囲外サイズ、画像寿命、同じ key の位置違い再利用を含む fake-backend regression を追加し、`g++ -std=c++17 -Wall -Wextra -Werror -Wpedantic -Iinclude -Isrc -DGKCORE_TESTING=1 -fsyntax-only tests/core_tests.cpp` を実行しました。文字 API と backend rasterizer の宣言がまだなく、`DrawText` 不在および `RasterizeText` override 不一致で失敗しました。Win32 の同名マクロ問題を避けるため、公開名は `gk::DrawString` にしています。
- GREEN: strict warning の combined C++ harness が通り、同じ UTF-8 / 色 / size はフレームをまたいでも 1 回だけ rasterize し、キュー中の複数 draw は同じ retained image を使うことを確認しました。64 件上限、16 MiB RGBA 上限、Shutdown 時の参照解放、UTF-8 と bounds rejection も検査します。
- CMake build / CTest は 12 件すべて成功し、`python3 tests/no_stl_tests.py` も成功しました。MinGW cross-compiler は GDI helper の syntax と、Windows.h を先に include した consumer の `DrawString` symbol を確認しました。Windows link / 実機フォント表示 / GPU 描画はこの実行環境では検証していません。

## 2026-10-03: 3D shader state snapshot と初期 window size

- Tests-first RED: fake backend regression で Triangle3D / Model draw の custom shader handle と、その各 draw 時点の float4 値を検査しました。実装前の `gkcore.core` CTest は `capture.shaderIds[0] == 1 && capture.shaderIds[1] == 1` で失敗し、従来の Rect/Image のみの snapshot 範囲を検出しました。
- GREEN: QueueDraw が全 draw kind で shader bindings を snapshot するよう変更した後、CMake build と CTest 12/12 が成功しました。追加の default-size assertion は初期化前に SetWindowSize を呼ばず、1280x720 で backend initialize されることを確認し、既存の明示設定テストは 640x480 override を確認します。default 初期化は既に実装済みだったため、そこに production code change は不要でした。

## 2026-10-03: sprite・ポスト処理・文字表示の統合

root が Release 構成の configure/build と CTest を改めて実行し、12 件すべて成功したと報告しました。この構成には画像 texture の方針テスト、ポスト処理の計算と実際の FSL 生成 pixel shader artifact の検査、文字列描画の CPU 契約が含まれます。別途、Forge renderer、Win32 window/text、texture cache、post-process renderer と Windows header 順序の構文検査も通過したとの報告です。vendor code の警告はありました。

この検査では Windows/MSVC のリンクや DX12 実機での表示は行っていません。したがって sprite、ポスト処理、日本語文字の経路が renderer に接続されていることは確認できても、Windows の画面で期待した見た目になることまでは確認していません。以前の 11 件の Release・sanitizer 結果は、これらの統合より前の履歴です。

## 2026-10-03: Windows 文字描画とタブ配置

- RED: タブ展開テストを先に追加し、次を実行しました。`TextLayout.h` がまだないため compile が失敗しました。

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Iinclude -Isrc \
  tests/text_layout_tests.cpp src/foundation/Memory.cpp src/foundation/String.cpp \
  -o /tmp/gkcore-text-layout-tests
```

- GREEN: `TextLayout` が UTF-8 を壊さず、タブを常に 4 個の空白へ置き換え、失敗時は出力を保持することを strict warning build で検査し、テスト実行も成功しました。

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc \
  tests/text_layout_tests.cpp src/text/TextLayout.cpp src/foundation/Memory.cpp \
  src/foundation/String.cpp -o /tmp/gkcore-text-layout-tests
/tmp/gkcore-text-layout-tests
```

build/packaging 担当が CMake に portable test target を登録し、configure/build と text layout および shader ABI の個別 CTest が成功したと報告しました。これは Windows GDI 検査の実行結果ではありません。

Windows 専用の GDI 検査は、文字の画素が実際に生成されることと、タブが 4 個の空白と同じ画像になることを確認します。Windows 実行環境がないため、この GDI 検査はまだ実行していません。

## 2026-10-03: 最終 Release・sanitizer 検証

root が最新ソースを独立して Release 構成と Debug の AddressSanitizer / UndefinedBehaviorSanitizer 構成で再ビルドし、どちらも CTest 14/14 件が成功したと確認しました。sanitizer 実行では `ASAN_OPTIONS=detect_leaks=0` を指定しており、リーク検査は行っていません。GLB 材質・階層深度、shader ABI、文字レイアウトの修正を含む結果です。

MinGW による `WindowsText`、Windows 文字描画テスト、Windows ヘッダー順序テストの構文検査も成功しました。Windows/MSVC でのリンク、GDI テスト実行、実 GPU 表示は未実施です。

## 2026-10-04: カスタムピクセルシェーダー

開発内容はローカルの `dev` に機能単位でコミットし、リモートへの push は保留します。Windows 上での動作を確認していない描画変更は、今回 `main` へ反映していません。

- モデルの UV が描画頂点から失われることと、矩形に正規化 UV がないことを担当のテストで先に確認しました。修正後は、頂点と UV の対応、近・遠クリップ面での補間、不正値、出力容量を検査しています。root の strict warning / ASan / UBSan ビルドでも成功しました。
- コンパイルツールのテストは実装前のモジュール未存在で失敗しました。完成後は DXIL の範囲・ピクセル段階・サイズ制限、固定 DXC の検証、失敗時の出力保持などを検査します。追加した日本語診断のテストは実際の子プロセスから UTF-8 を出力し、修正前に `UnicodeDecodeError` を確認しています。修正後のコンパイラー検査は 12 件成功しました。入力と同じファイルを出力先にした場合も拒否します。
- 共通入力、出力先、定数バッファ、画像型の検査を CPU で実行できる形に分けました。float4 の TEXCOORD 入力が誤って通ることを失敗するテストで確認し、float2 の範囲へ修正しました。重複した入力、不正な定数の形、整数型の画像も拒否します。パイプライン選択とフレーム・描画数の検査も独立したテストを追加しました。
- Linux 用 DXC 1.8.2405 で公開 HLSL ヘッダーを使う tint サンプルを実際にコンパイルし、3,756 バイトの FSL ファイルを生成しました。DXC の出力から `b0, space3`、64 個の float4、1,024 バイトの配置を確認しています。画像・sampler も使う正常なシェーダーと、不正な定数配列・整数画像の検査用ファイルも生成しました。Windows 用 DXC パッケージと通常 CLI を使った実行確認ではありません。

ネイティブ部分のレビューでは、デスクリプターが寿命の切れたローカル変数を参照する問題、4 MiB 全体に定数ビューを作る問題、フレーム間に描画定数が残る問題を修正しました。また、最初の画像描画でテクスチャーが誤った種類の描画命令へ結び付くため、パイプラインを先に選んでから画像を設定する順序へ直しました。これらはコードと固定依存の実装を調べた結果であり、GPU 上で失敗・成功を観測した記録ではありません。

Windows の GPU smoke に、画像を最初に描くケース、異なる定数を使う連続フレーム、シェーダーを削除した後の通常描画を追加しています。GPU smoke と、実際の DXC COM reflection を使う Windows テストは、この環境では未実行です。

root の検証環境は Linux、GCC 14.2、Python 3.12、CMake / CTest です。

```sh
cmake -S . -B /tmp/gkcore-review-build -DGKCORE_BUILD_RUNTIME=OFF -DGKCORE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/gkcore-review-build --parallel 4
ctest --test-dir /tmp/gkcore-review-build --output-on-failure

cmake -S . -B /tmp/gkcore-sanitize-build -DGKCORE_BUILD_RUNTIME=OFF -DGKCORE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build /tmp/gkcore-sanitize-build --parallel 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/gkcore-sanitize-build --output-on-failure
```

両方の CTest が 17/17 件成功しました。リーク検査は無効です。配布ファイルの許可リスト、Runtime の STL 検査、新しいコンパイラーとシェーダー入力検査を含みます。説明書の配布一覧 38 ファイルも許可リストと照合しました。

MinGW の Windows 向け構文検査は、カスタムシェーダー管理、reflection、入力検査、描画部、GPU smoke、Windows reflection テストで成功しました。検査用の設定から固定依存の MSVC 限定条件だけを外しており、製品の依存ソースは変更していません。依存ヘッダーのマクロ再定義や UUID 属性の警告が残るため、MSVC のコンパイル・リンクや Windows の実行確認の代わりにはなりません。
