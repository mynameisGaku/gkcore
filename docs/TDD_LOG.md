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

## 2026-10-04: 色調整・FXAA・モデルの基本色

設定 API、効果の計算、描画経路、モデル材質、配布構成、説明書を並行して進め、root が統合部分をレビューしました。Runtime の公開コードには STL を追加していません。CPU の FXAA 計算は検証用で、Runtime ライブラリのソース一覧から外しています。

- 設定 API の RED は、先に追加したテストをビルドした際の `SetSaturation` / `SetContrast` / `SetFxaaEnabled` と設定フィールドの未定義でした。実装後は初期化前の設定保持、`BeginFrame` での複写、フレーム途中の変更、不正値での設定保持、終了時の初期値復元を確認しました。彩度とコントラストは有限値の `[0, 2]`、初期値は `1` です。FXAA は初期設定で有効です。
- モデル描画計画と形状展開は、`ModelDrawPlan` と `AppendModelPart` の未定義によるビルド失敗を先に確認しました。実装後は材質ごとの添字範囲、順序、線形の基本色係数、画像選択、不正な範囲や材質、クリップ後の UV と頂点を検査しています。gkcore 用に作った GLB には 2 つの材質と 1 つの埋め込み PNG があり、公開 API で読み込み、描画を予約した後にモデルを削除しても `Present` まで材質が残ることも確認しました。この GLB の統合検査は実装後に追加した回帰テストです。
- FXAA のレビューで、境界を横切る方向へサンプリングしていることを見つけました。縦横の境界を保つテストを追加すると `FXAA softened an axis-aligned pixel-art edge` で失敗しました。境界に沿う方向へ CPU と FSL の計算を修正した後、縦横の境界、斜めの段差、非正方形、1 画素の画像、出力範囲、alpha、画像端、入力と出力の重なりの検査が成功しました。
- 配布許可リストとビルド計画は、FXAA ファイルを要求するテストで先に失敗しました。修正後は `gkcore_fxaa.frag` を含む 11 個のシェーダー関連ファイルを要求します。別の検査が必要な `dxcompiler.dll` を開発用コンパイラーと誤認していたため、有効な Runtime 一覧を拒否する RED を確認して修正しました。`dxc.exe` と FSL ツールは引き続き拒否します。

描画処理のレビューでは、記録しただけのバリアを GPU に送信済みの状態として扱う問題を修正しました。ターゲットの状態は `queueSubmit` の後に確定し、中断した命令では変わりません。この方針を実際の描画部が使い、CPU テストで中断後の状態を確認しています。また、完全にクリップされたモデルの画像をキャッシュへ登録する処理を省きました。画面外の材質だけでキャッシュ上限に達する問題をコードから見つけたもので、GPU 上で失敗を観測した記録ではありません。

固定した The Forge の FSL と Linux DXC 1.8.2405 で、post 頂点段階と Bloom 抽出・ぼかし・色調整・FXAA のピクセル段階を実際にコンパイルしました。最初の FXAA コンパイルは、定数型の宣言前に SRT を読み込んでいたため `PostProcessConstants` 未定義で失敗しました。include 順の修正後は全段階が成功しています。DXC の reflection では、定数バッファが 48 bytes、`Parameters` / `ColorAdjustment` / `ImageSize` がそれぞれ offset 0 / 16 / 32、binding が `cb3` / `s2` / `t0` であることを確認しました。実際の FXAA artifact は 6,272 bytes で、Runtime のパーサーを通すテストはファイル追加前に失敗し、追加後に成功しました。Windows 用 root signature のコンパイルはこの検査に含みません。

root が Linux、GCC 14.2、Python 3.12、CMake / CTest で最新ソースを再ビルドしました。Release と Debug の ASan / UBSan 構成は、どちらも **20/20 件成功**です。再実行コマンドは直前のカスタムシェーダー検証と同じで、`ASAN_OPTIONS=detect_leaks=0` のためリーク検査は行っていません。説明書に列挙した Runtime 39 ファイルも、構成名を `release` に置き換えて許可リストと照合しました。

MinGW による描画部、形状展開、材質計画、ポスト処理、Windows GPU smoke、混在描画サンプルの構文検査も成功しました。Windows GPU smoke には、標準と独自シェーダーでの GLB 描画、予約後のモデル削除、Bloom / FXAA の切り替えを追加しました。MSVC のリンク、Windows の実行、実 GPU での見た目、配布 SDK だけを使ったアプリの起動は未確認です。PBR 照明・影・環境マップ・alpha mode は今回の変更に含みません。

変更はローカルの `dev` へ保存します。Windows の動作確認が残るため、今回の描画変更は `main` へ反映せず、リモートへの push も保留します。

## 2026-10-04: FBX 静的モデル読み込み

- **基準挙動の比較（テスト先行ではない）:** 有効な FBX を旧 OBJ / GLB 専用ローダーで処理する比較を行いました。FBX importer 作成後に実行した `/tmp/gkcore-fbx-red` は終了コード 1 となり、`valid FBX was rejected: OBJ contains no faces` を出しました。この実行は旧ローダーと比較した証拠であり、FBX importer 全体の tests-first RED ではありません。完全な compile command は保存されていません。
- **テスト先行 RED:** importer 実装後、`legacy_uv_transform` fixture の非恒等 UV 変換を拒否するテストを追加し、検証コードを入れる前に実行しました。`/tmp/gkcore-fbx-validation/gkcore_fbx_tests` は終了コード 1 となり、`invalid or unsupported FBX fixture was accepted: gkcore_legacy_uv_transform.fbx` と出しました。UV 変換の拒否を実装後、root の統合テストが成功しています。
- FBX importer 全体を最初から tests-first で進めたとは確認できません。FBX の初期テストは実装時系列の後に追加されたものがあり、geometry test は実装前に書かれたものの、最初に失敗した実行記録はありません。strict C++17 の単独 suite が実装後に終了コード 0 になったとの報告がありますが、完全な compile command は保存されていません。
- 実装は FBX ファイルを 64 MiB、解析時の一時領域を 64 MiB、結果領域を 128 MiB、階層深度を 64 に制限します。静的メッシュを読み込み、アニメーションの再生は行いません。スキニング、モーフ、ジオメトリ cache は拒否します。モデルの単位と軸を右手系 Y-up / meter に変換し、階層・幾何変換を反映します。UV は先頭の UV セットを使い V を反転します。外部画像は FBX からの相対パスで読み、絶対パスは拒否します。FBX で確認した画像は外部・埋め込み PNG です。RGB 材質係数は線形値を保ち、透明度係数はアルファへ反映します。
- テスト入力・assertion の修正は実装修正の RED と分けます。UV set 名の fixture は ufbx の `Properties70` に正しく記述するよう修正しました。POSITION index を不正にしたケースは当初必要な NORMAL が欠けていたため、index 検査へ到達していませんでした。正常な NORMAL を fixture に加え、期待する POSITION index 診断を確認する assertion に直しました。geometry の頂点数 assertion も実データに合わせて修正しました。
- `DrawModel` を予約した後 `DeleteModel` して `Present` する公開 API 寿命テストと、アロケーション失敗の注入テストは importer 実装後に追加した回帰検査です。確保失敗は最大 256 回順に注入し、最初に読み込みが成功した時点で止め、失敗後の再試行も確認します。これらを importer の初期実装に対する tests-first RED とは数えません。
- package allowlist は ufbx の license file を欠いた候補 manifest を拒否し、追加後の package suite は 6/6 件成功しました。最終 Runtime の 40 パスも許可リストと一致しました。MinGW で FBX 関連の 8 translation unit を構文検査し成功しました。検査時にはテスト fixture の union 初期化で missing-braces 警告も報告され、これは runtime source の警告ではありません。vendored ufbx の SHA-256 は元の固定版と一致し、fixture generator の再実行後も全モデル asset の SHA-256 は変わりませんでした。これらの package / 構文検査は MSVC build / link ではありません。
- legacy UV transform の拒否、公開 API の寿命検査、確保失敗処理の最新変更を含む Release CTest は **22/22 件成功**し、所要時間は 0.77 秒でした。Debug ASan / UBSan CTest も **22/22 件成功**し、所要時間は 2.45 秒でした。実行には `ASAN_OPTIONS=detect_leaks=0` を指定したためリーク検査は行っていません。root は同じソースからモデル asset generator を再実行し、生成 asset の SHA-256 が一致することを確認しました。

```sh
/tmp/forgedx-review-tools/cmake/data/bin/cmake --build /tmp/gkcore-review-build --parallel 6
/tmp/forgedx-review-tools/cmake/data/bin/ctest --test-dir /tmp/gkcore-review-build --output-on-failure

/tmp/forgedx-review-tools/cmake/data/bin/cmake --build /tmp/gkcore-sanitize-build --parallel 6
ASAN_OPTIONS=detect_leaks=0 /tmp/forgedx-review-tools/cmake/data/bin/ctest \
  --test-dir /tmp/gkcore-sanitize-build --output-on-failure
```

テスト内の union 初期化は各成分の代入へ直し、MinGW の `-Werror` 構文検査と Release・ASan / UBSan の FBX テストを再実行しました。どちらも 1/1 件成功しています。

MSVC のビルドとリンク、Windows の実 GPU 表示、Runtime SDK だけを使う別プロジェクトの起動は未検証です。`main` への反映は Windows 検証後とし、リモートへの push は行っていません。

## 2026-10-04: カスタムポストエフェクト shader のフレーム設定

- Effects 側の RED は、`ShaderBindings::SnapshotFor` を実装する前に対象を build し、同 method がないため失敗したものです。Effects 担当の報告では 6 箇所の呼び出しで compile error になりました。実装後は `gkcore.core` が通過し、handle ごとの sorted Float4 定数、無効 handle からの空 snapshot、古い handle、確保失敗時に出力を保つ契約を確認しました。実行コマンドは担当報告に記録されていません。
- Core API の RED は、テスト追加直後、`gkcore.h` / `Backend` / `Context` / API 実装より前に実行しました。

```sh
/tmp/forgedx-review-tools/cmake/data/bin/cmake \
  --build /tmp/gkcore-core-model-build --target gkcore_tests -j2
```

build は終了コード 2 で、`FramePacket.postEffectShader`、`postEffectConstantCount`、`postEffectConstants` と `gk::SetPostEffectShader` がまだ定義されていない旨の compile error になりました。実装後、同 build tree の `gkcore.core` focused CTest は 1/1 成功しました。選択と定数を `BeginFrame` で snapshot すること、フレーム途中の変更は次フレームに反映されること、無効 handle は効果を切ること、描画 shader 選択と独立していること、フレームが shader を保持している間は描画命令がなくても削除を拒否すること、Shutdown で状態を初期化することを確認しています。
- Post-effect plan の portable test と ASan / UBSan focused test も成功したとの報告があります。個別実行の完全なコマンドは保存されていません。これらは component test であり、Windows/MSVC link や実 GPU 描画を確認していません。
- root が最新統合ソースを Release と Debug ASan / UBSan で再ビルドし、両方の CTest が 23/23 件成功したと確認しました。Release は `/tmp/gkcore-review-build` で 0.65 秒、Debug は `/tmp/gkcore-sanitize-build` で 1.50 秒です。Debug 実行には `ASAN_OPTIONS=detect_leaks=0` を指定したため、リーク検査は行っていません。

```sh
/tmp/forgedx-review-tools/cmake/data/bin/cmake --build /tmp/gkcore-review-build --parallel 6
/tmp/forgedx-review-tools/cmake/data/bin/ctest --test-dir /tmp/gkcore-review-build --output-on-failure
/tmp/forgedx-review-tools/cmake/data/bin/cmake --build /tmp/gkcore-sanitize-build --parallel 6
ASAN_OPTIONS=detect_leaks=0 /tmp/forgedx-review-tools/cmake/data/bin/ctest \
  --test-dir /tmp/gkcore-sanitize-build --output-on-failure
```

ポスト shader の選択・定数 snapshot、層の順序、削除時の寿命条件を含む CPU 契約は統合済みです。Windows/MSVC でのリンク、実 GPU 上の描画、Runtime SDK だけを使った別プロジェクトのビルドは未確認です。`main` への昇格も保留中です。

root の追加レビュー後、MinGW で `ForgeRenderer.cpp`、`PostProcessRenderer.cpp`、`PostEffectRenderer.cpp`、`CustomShaders.cpp`、`PostEffectPlan.cpp`、`examples/custom_post_effect.cpp` の 6 translation unit を構文検査し、終了コード 0 を確認しました。構文検査では `EXTERNAL_CONFIG_FILEPATH` に一時的な `ForgeSyntaxConfig.h` を指定して MSVC compiler whitelist を回避しています。upstream header の警告が残るため、この結果は MSVC の build/link や Windows 実行を示しません。

Linux DXC で `examples/shaders/post_effect_tint.hlsl` を `ps_6_0`, entry point `main` としてコンパイルしました。生成 DXIL は既存 FSL artifact helper で包み、`tests/assets/shaders/post_effect_tint.frag` と byte-for-byte 一致することを確認しました。DXIL は 4,376 bytes、FSL ファイルは 4,436 bytes、SHA-256 は `8b041c784a2cac219af7bd6a60fce74a6f82d1eafe0be042f310b0105257728a` です。DXIL reflection では `b0, space3` の 1,024-byte `float4[64]` 定数、`t0`、`s0`、`SV_Target0` を確認しました。これはシェーダーのコンパイルと ABI の確認であり、Windows GPU 上の実行確認ではありません。

ポスト shader artifact のテスト先行 RED は build/packaging 担当が記録を確認しました。次のコマンドは fixture が存在しない状態で実行され、非ゼロ終了となり、`custom post-effect pixel shader fixture could not be loaded` を出しました。

```sh
c++ -std=c++17 -Wall -Wextra -Wpedantic -DGKCORE_TESTING=1 \
  -DGKCORE_TEST_SOURCE_DIR=\"/workspace/gkcore\" -Iinclude -Isrc \
  tests/shader_artifact_tests.cpp src/render/Shaders.cpp \
  src/resources/ResourceIO.cpp src/foundation/Memory.cpp \
  src/foundation/Array.cpp src/foundation/String.cpp \
  /tmp/gkcore_shader_artifact_runner.cpp -o /tmp/gkcore_shader_artifact_red \
  && /tmp/gkcore_shader_artifact_red
```

fixture 追加後は統合 CTest 23/23 件で成功しています。さらに core 側でフレーム途中に定数を変更するケースを追加し、`gkcore.core` と `gkcore.post_effect_plan` の Release / ASan・UBSan focused CTest が各 2/2 件成功しました。Runtime の 40 パスは不変で、no-STL 3/3、package 6/6 件成功との報告があります。これらは CPU 契約、開発側コンパイル、manifest 検査です。Windows/MSVC link、GPU 実行、MSVC reflection は未確認で、`main` への昇格も保留中です。

Windows GPU smoke を実装後、root が `tests/backend_contract_tests.cpp` を MinGW で `-Wall -Wextra -Wpedantic -Werror` 付き構文検査し、成功を確認しました。コードレビューでは、ポスト shader の有効化、選択中のリサイズ、Scene/UI を含む描画、無効化・再有効化、無効化後の削除、終了・再初期化の経路を確認しています。これは既存機能をまとめた統合検査の準備で、実装前に失敗を観測した RED / GREEN の証拠ではありません。Windows 上で smoke を実行した記録はなく、画素読み戻しもないため、Windows/GPU の実行や見た目の確認とは扱いません。目視確認の手順は [開発ガイド](development.md) に記載しました。

## 2026-10-04: 輪郭矩形 API

- core の API 契約テストを実装前に追加し、Release の test target を build しました。初回 build は終了コード 2 となり、公開 API と描画 packet の outline 用 field が未定義という compile error で失敗しました。

```sh
/tmp/forgedx-review-tools/cmake/data/bin/cmake -S . -B /tmp/gkcore-outline-core-build \
  -G 'Unix Makefiles' -DGKCORE_BUILD_RUNTIME=OFF -DGKCORE_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
/tmp/forgedx-review-tools/cmake/data/bin/cmake --build /tmp/gkcore-outline-core-build \
  --target gkcore_tests --parallel 4
```

- API 実装後、core 担当の報告では `gkcore.core` focused CTest が 1/1 成功しました。公開 API の入力検査と packet 契約の結果です。

```sh
/tmp/forgedx-review-tools/cmake/data/bin/ctest \
  --test-dir /tmp/gkcore-outline-core-build --output-on-failure \
  -R '^gkcore\.core$'
```

- 図形展開側の RED は、geometry test target は build 成功したものの、既存 geometry 実装が輪郭矩形を受け付けず失敗しました。

```sh
/tmp/forgedx-review-tools/cmake/data/bin/cmake --build \
  /tmp/gkcore-outline-geometry-build --target gkcore_geometry_tests -j4
/tmp/gkcore-outline-geometry-build/gkcore_geometry_tests
```

実行結果は終了コード 1、`FAIL: outlined rectangle geometry is accepted` でした。これは実装前に描画 geometry が輪郭矩形を受け付けないことを示す RED です。

geometry 実装後、同じ target の再 build と `gkcore.geometry` focused CTest は 1/1 成功し、`RectangleGeometry.cpp` / `Geometry.cpp` の strict C++17 構文検査も成功したと担当から報告されました。

```sh
/tmp/forgedx-review-tools/cmake/data/bin/cmake --build \
  /tmp/gkcore-outline-geometry-build --target gkcore_geometry_tests -j4
/tmp/forgedx-review-tools/cmake/data/bin/ctest \
  --test-dir /tmp/gkcore-outline-geometry-build --output-on-failure \
  -R '^gkcore\.geometry$'
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc \
  -fsyntax-only src/render/RectangleGeometry.cpp src/render/Geometry.cpp
```

テストは外枠の範囲、空の内側、4辺を重ねずに覆う三角形、全体に正規化した UV、色、短辺の半分以上の太さ、幅 0.5 の狭い矩形、正確な頂点容量と容量不足時の既存データ保持を検査します。カラー期待値を最初に誤って設定したテスト実行は、sRGB の 80/255 を線形値 `0.08021982` とすべきところ `0.07805664` としていたため失敗しました。これはテスト期待値の修正で、runtime の色処理不具合を示す RED ではありません。

root が統合後の Release と Debug ASan / UBSan の全 CTest を再実行し、どちらも 23/23 件成功したと報告しました。Release は `/tmp/gkcore-review-build` で 1.20 秒、Debug は `/tmp/gkcore-sanitize-build` で 1.96 秒です。Debug 実行は `ASAN_OPTIONS=detect_leaks=0` のため、リーク検査はしていません。Runtime manifest の 40 path、no-STL 3/3、package 6/6 も再確認しました。

同じソースの MinGW `-Wall -Wextra -Wpedantic -Werror` 構文検査では `Draw2D.cpp`、`RectangleGeometry.cpp`、`Geometry.cpp`、`examples/rectangle_outline.cpp`、`tests/backend_contract_tests.cpp` の 5 translation unit が成功しました。固定 The Forge の一時 `ForgeSyntaxConfig` を使った構文検査では `ForgeRenderer.cpp` と Direct3D 12 Runtime の 3 translation unit も成功しました。upstream の macro 警告が 4 件あります。この確認は MSVC の link や Windows 実行を代替しません。Windows/MSVC link と実 GPU 表示は未確認です。

## 2026-10-04: モデルの方向光と材質照明

照明 API の snapshot 契約、モデル材質係数、法線変換と clipping を CPU 側に追加しました。実装前に照明単体 test を baseline 上で compile すると、新しい `src/effects/Lighting.h` が存在しないため失敗しました（診断: `/tmp/gkcore-lighting-unit-red.log`）。別の core API 契約 RED では、`GKCORE_TESTING` と `GKCORE_TEST_SOURCE_DIR` を正しく定義して baseline を compile し、`FramePacket::lighting` と `gk::SetAmbientLight` / `gk::SetDirectionalLight` が未定義で失敗しました（診断: `/tmp/gkcore-lighting-api-red-correct.log`）。最初の core compile 試行は test 用 macro を設定しておらず、無関係な test-hook errors が混じったため RED の証拠として数えていません。

モデル geometry の修正前には、回転済みの欠損法線 fallback をもう一度回転させるケースと、極端な有限 view direction の clipping 補間で値が有限性を保つケースが失敗しました。

```sh
/tmp/gkcore-model-geometry-regression
/tmp/gkcore-model-geometry-extreme
```

両実行は終了コード 1 で、それぞれ `world-space fallback face normal is not rotated a second time`、`extreme finite view-direction interpolation stays finite` と報告しました。修正後は lit geometry の strict C++17 build / run が成功しました。model draw plan、lit geometry、既存 geometry の focused CTest は 3/3 件成功しました。照明 API、法線、材質係数、frame snapshot の CPU 契約も最終 Release / sanitizer の全件へ含まれます。

サンプル用 GLB は一時 C++ runner から `LoadGlbPayload` で読み込み、850 頂点、2 primitive、2 材質（metallic 0 / 1）を確認しました。全頂点の明示法線は有限で、長さが 1 でした。これは GLB CPU 読み込みの確認であり、描画の目視検査ではありません。

Linux の frozen-source Release build と Debug ASan / UBSan build、全 CTest を再実行し、それぞれ 25/25 件成功しました。

```sh
/tmp/forgedx-review-tools/cmake/data/bin/cmake --build /tmp/gkcore-review-build --parallel 6
/tmp/forgedx-review-tools/cmake/data/bin/ctest --test-dir /tmp/gkcore-review-build --output-on-failure -j6
/tmp/forgedx-review-tools/cmake/data/bin/cmake --build /tmp/gkcore-sanitize-build --parallel 6
ASAN_OPTIONS=detect_leaks=0 /tmp/forgedx-review-tools/cmake/data/bin/ctest --test-dir /tmp/gkcore-sanitize-build --output-on-failure -j6
```

Release の CTest は 1.09 秒、Debug sanitizer の CTest は 1.92 秒で完了しました。`ASAN_OPTIONS=detect_leaks=0` のため LeakSanitizer は実行していません。`GKCORE_BUILD_RUNTIME=OFF` の CPU build なので、これらの成功は Windows/MSVC link や GPU 描画を確認したものではありません。

Linux MinGW `-Wall -Wextra -Wpedantic -Werror -fsyntax-only -Iinclude -Isrc` による最終構文検査では、`src/effects/Lighting.cpp`、`src/render/LightingAbi.cpp`、`src/render/WorldGeometry.cpp`、`src/render/ModelGeometry.cpp`、`src/render/Geometry.cpp`、`src/render/ModelDrawPlan.cpp`、`examples/model_lighting.cpp`、`tests/backend_contract_tests.cpp` の 8 translation unit が成功しました。固定 The Forge の native syntax configuration を使った renderer 2 translation unit の確認も成功しましたが、upstream macro 警告が合わせて 6 件あり、これは MSVC link の結果ではありません。

固定した Forge FSL toolchain で model vertex / pixel stage を生成し、Linux DXC で compile して reflection を検査しました。Linux 向け生成では、その環境の compiler 制約のため FSL 内の root-signature 宣言だけを省いています。

```sh
python3 tests/shader_contract_tests.py --artifact tests/assets/shaders/gkcore_model.vert --dxc /tmp/gkcore-dxc18-linux/bin/dxc --require-reflection --model-vertex
python3 tests/shader_contract_tests.py --artifact tests/assets/shaders/gkcore_model.frag --dxc /tmp/gkcore-dxc18-linux/bin/dxc --require-reflection --model-pixel
```

両コマンドは成功し、lighting constant buffer は `b0, space1` の 32 bytes、画像は `t0`、既存 sprite sampler は `s1` と確認しました。Linux DXC artifact / reflection の確認であり、Windows 用 shader package や Windows COM reflection の確認ではありません。Runtime の no-STL scan は 3/3、package allowlist は 6/6、shader build plan は 1/1 成功しました。更新後の manifest template を Release 構成へ展開して package allowlist validator に渡し、Runtime の 42 path を検査して成功しました。モデル照明の概念図 SVG も XML parser で読み取れることを確認しました。

Windows/MSVC build と link、Windows/DX12 実機の表示、model lighting の目視、Runtime SDK のみを使った利用者 project の build / run は未実施です。GPU smoke は準備されていますが実行されておらず、異なる材質や照明が画面にどう見えるかは未確認です。

## 2026-10-05: Windows/MSVC CPU 契約テスト

Windows x64 の Visual Studio 18 2026 / MSVC 19.51.36260.0、Windows SDK 10.0.28000.0、CMake 4.3.1、Python 3.11.9 で、Runtime を無効にした開発用全 target を構成・buildし、Debug / Release の全 CTest を実行しました。

```powershell
cmake -S . -B build/dev-windows -G "Visual Studio 18 2026" -A x64 -DGKCORE_BUILD_RUNTIME=OFF -DGKCORE_BUILD_TESTS=ON
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```

初回の MSVC build では `Array.h` の `alignof(max_align_t)` が global `max_align_t` を解決できず、C2187 / C2061 で失敗しました。`Memory.h` に確保時の基準 alignment `kAllocationAlignment` を置き、MSVC では `alignof(double)`、それ以外では `alignof(max_align_t)` を使うよう修正しました。`alignas(std::max_align_t)` を指定した検証用の値型を Array に格納し、Reserve 後および growth 後のアドレスの配置と値の保持を契約テストに追加しました。`alignas(64)` 型の負の compile check は意図した `static_assert` 診断を確認します。assert を一時的に外す mutation 検査でテストが configure 時に失敗することを確認し、変更は復元しました。

次に `tests/model_draw_plan_tests.cpp` と `tests/model_geometry_tests.cpp` の NaN / infinity 生成式に対して MSVC が C2124（定数 0 による除算）を出し、Debug build が失敗しました。テスト式を `<limits>` の `quiet_NaN()` / `infinity()` に置き換えました。この RED はコンパイラ上のテストコード互換性を示し、製品コードの失敗を示すものではありません。

修正後、Debug と Release の全 target build が成功し、CTest は各 26/26 件成功しました。CI と同じ構成コマンドをローカルの Windows clean shell で実行すると、従来の Ninja generator 指定は compiler 検出に失敗しました。generator の自動選択と Release 構成を明示する形へ直し、`build/ci-windows-fixed` の configure 成功を確認しました。主な実測ログは `build/dev-windows/{configure-final.log,build-debug-nan-red.log,build-debug-green.log,ctest-debug-green.log,build-release-green.log,ctest-release-green.log}` にあります。

この検証は Windows/MSVC での CPU 契約と開発用 target の build / link を対象とします。`GKCORE_BUILD_RUNTIME=OFF` のため、The Forge を含む Runtime の build / link、DX12 実行、GPU 表示の確認ではありません。Runtime build が必要とする Visual Studio 2022 / v142 toolset はこの PC にないため、`tools/setup.py` の前提確認で停止し、Runtime 側の検証は未実施です。

## 2026-10-05: Windows The Forge / OS build の文字コード

Visual Studio 18 / MSBuild 18.10.1 に導入した v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.28000.0、CMake 4.3.1 で The Forge の Renderer / OS Release|x64 build を確認しました。最初は CP932 環境で `MemoryTracking.c` の C4819 が `/WX` により C2220 となり、OS build が失敗しました（RED: `build/native-validation/forge-release-x64.log`）。既存の `CL` 環境設定を維持したまま compiler option に `/utf-8` を追加し、Renderer と OS の build が成功しました（GREEN: `build/native-validation/forge-release-x64-utf8.log`）。関連する focused unittest 3 件も成功し、指定バージョンの The Forge 1,825 ファイルと DXC 7 ファイルの検証も成功しました。

この結果は The Forge の Renderer / OS build と依存ファイル検査の確認です。gkcore Runtime の build / link、DX12 実行、GPU 表示の成功を示すものではありません。前項の CPU テスト記録にある v142 未導入の前提確認は、この toolset を導入する前の時点の結果です。

## 2026-10-05: Visual Studio検出と依存アーカイブの取得

VS2026にv142を導入した環境で、従来のセットアップはVS2022だけを検索して失敗しました。VS2026のみの構成を再現するテストでも同じ失敗を確認しました（`build/dev-windows/setup-vs2026-red.log`）。修正後はv142、MSBuild、CMake generatorを同じVisual Studio instanceから選択します。CMakeのバージョンに応じたVS2022への選択切り替え、v142不足の診断、GPU smoke指定時にも全CTestを実行する契約を含め、`python -B tests/package/test_setup_plan.py` の8件が成功しました。実環境でもVS2026と導入済みv142の検出を確認しました。

The Forgeの取得先URLが、保存済みSHA-256と異なる形式のアーカイブを返していました。公式codeload URLのアーカイブが既存のSHA-256 `21ac9381f9711de6fa8723704533068b60abc8a02600aa69b608b859045b90ba` と一致することを確認し、URLだけを変更しました。commitと1,825ファイルの検証値は変更していません。

Windowsの長い一時パスへの展開は修正前にWinError 206で失敗しました。長いパスに対応した展開処理を追加し、移動後の内容確認と不正なパスの拒否を含む `python -B tests/package/test_forge_checkout.py` の5件が成功しました。実アーカイブを展開し、既定の `.devtools/The-Forge` でも1,825ファイルの検証に成功しました。今回変更したPythonソースはUTF-8 BOM付き・CRLFで確認しています。

変更後に `ctest --test-dir build/dev-windows -C Debug --output-on-failure` とRelease構成の同コマンドを実行し、どちらも26/26件成功しました。既存のRuntime無効ビルドを使った回帰検証です。ログは `build/native-validation/ctest-tools-debug.log` と `ctest-tools-release.log` に保存しました。

## 2026-10-05: Windowsポストエフェクトshaderの画像割り当て

WindowsのFSL/DXC実行では、sourceとBloomを別々のtextureとして宣言すると両方がt0になり、register重複でコンパイルが失敗しました（`build/native-validation/shaders-first.log`）。2要素のtexture配列にまとめ、CPU側から同じ順番で2枚を渡すよう修正しました。生成したpost-compositeのDXILをDXCで検査し、textureがt0〜t1、samplerがs0、constant bufferがb0であることを確認します。

UTF-8 BOM付きのFSL入力は固定バージョンのコンパイラで処理に失敗しました。元ソースはBOM付き・CRLFのまま保ち、ビルド時の一時コピーだけBOMを除去し、子Pythonの文字コードをUTF-8へ固定しました。元ファイルの内容を保持するテストも追加しました。

`python -B tests/package/test_shader_build_plan.py` の3件と `python -B tests/shader_contract_tests.py` の13件が成功しました。固定ForgeとDXC 1.8.2405を使うWindows実行で13個のshader artifactを生成し、生成物のreflection検査も成功しました（`build/native-validation/shaders-arraybinding-final.log`）。この確認はshaderの生成と割り当てを対象とし、GPU表示や画質の検証ではありません。

中カッコを改行する書式と、括弧・初期化子内部を1行に保つ設定を追加しました。変更したC++とFSLはclang-format 12の再実行で差分が出ず、UTF-8 BOM付き・CRLFを確認しました。整形後もWindowsで13個のshaderを再生成し、reflection検査が成功しました（build/native-validation/shaders-final-format.log）。

## 2026-10-05: Windows Runtimeのリンクと実行ファイルの配置

Visual Studio 18 2026 / v142 14.29.30133、MSVC 19.29.30159、CMake 4.3.1を使用しました。Windows SDK 10.0.28000.0ではdxguid.libと固定Forgeの3つのGUID定義が重複し、LNK2005で失敗しました。XInputの3つの未解決symbolにはXinput9_1_0を追加しました。重複を無視する設定は使わず、SDK 10.0.22621.0をForgeとgkcoreの両方で選択します。

SDK切り替え初期の試行は古いCMake cacheのSDK28000を参照していたため、22621での検証として数えていません。新しいbuild directoryの生成projectが22621を選択していることを確認しました。Forgeも再ビルドし、RendererとOSのlastbuildstateが22621になっていること、実際の再コンパイルと0 warning / 0 errorを確認しました（forge-sdk22621.log）。setupのSDK不足・別バージョン拒否・CMake指定のテスト10件とForgeビルド計画4件が成功しました。

次にWindows.hを先に読み込むとLoadImageマクロによってgk::LoadImageAを要求し、backend smokeのリンクが失敗しました。公開ヘッダーで同名マクロを除去し、配布SDKを使うconsumerにもWindows.h先行の実呼び出しを加えました。

Releaseの全targetがビルドできるようになりました。ビルド出力に実行時DLL5件とshader13件を配置するtargetを追加し、D3D12Core.dllとgkcore_color.vertを消した後の増分ビルドでも両方が復元されました。削除はこの検証用build出力だけを対象としています。

SDK22621のRelease全CTestは30件中29件成功し、配布ファイルの許可リスト検査、SDKのみを使うconsumerのビルド・実行、Windowsのshader reflectionを確認しました（runtime-ctest-release.log）。GPU smokeはgkcore.dll内のアクセス違反で失敗しました。MAPによる例外位置はaddLogFile内で、ログ初期化に必要なRD_LOGのI/O設定不足を特定しました。この時点ではGPU初期化・描画の成功を示しません。配置修正前のsmokeは60秒でtimeoutしましたが、それだけでは停止原因を特定できないため、DLL不足だけが原因だったとは断定していません。

Runtime無効のWindows CPUビルドも更新後のソースでDebug / Releaseを再ビルドし、CTestはそれぞれ26/26件成功しました。build/native-validation/cpu-{debug,release}-final-{build,tests}.logに保存しました。

## 2026-10-05: Runtime初期化と実GPU smoke

SDK22621のRelease Runtimeは、初期化直後に`0xc0000005`で終了しました。MAPとWindowsの障害情報から`addLogFile`内を確認すると、tool用のファイルシステム初期化では`RD_LOG`が未設定のまま`initLog("gkcore")`がファイルを開いていました。最初にファイル名なしでloggerを初期化し、DLLの場所を`RD_LOG`に設定した後でログファイルを開くよう修正しました。再初期化時のパス設定の警告も、初期化済みloggerで扱います。

次の実行ではGPUを検出しましたが、`gpu.data`と`gpu.cfg`の不足で初期化が失敗しました。固定Forgeの`Common_3/OS/Windows/pc_gpu.data`を`gpu.data`へ、`Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg`を同名のまま、ビルド出力とRuntime SDKのbinへ配置します。独自のGPU設定やvendorソース変更は加えていません。Runtime allowlistに2ファイルの欠落を検出するテストを先に追加し、REDを確認してから必須ファイルを更新しました。`python -B tests/package/test_allowlist.py`は7/7成功しました。

出力先のGPU設定2ファイルを除いた後の増分ビルドで両方が復元され、固定Forgeのコピー元とSHA-256が一致しました。DLLとshaderに続き、再配置を確認した記録は`build/native-validation/runtime-stage-gpu-incremental.log`です。GPU smokeには60秒のtimeoutを設定しました。

Windows 11 Pro build 26200、Visual Studio 18 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0、CMake 4.3.1、Python 3.11.9、NVIDIA GeForce RTX 4070 SUPER / driver 610.74で、次を実行しました。

```powershell
.\PRE_SETUP.bat --gpu-check
```

指定Forgeの1,825ファイルとDXC 1.8.2405の7ファイルの照合、ForgeのRenderer/OS、13個のshader artifact、Release Runtimeと全targetのビルドが成功しました。生成された`gkcore.vcxproj`でもSDK22621とv142を確認しました。CTestは30/30件成功し、GPU smokeは2.46秒、全体は7.46秒で`BUILD READY`になりました。最終実行ログは`build/native-validation/pre-setup-gpu-final.log`、選択GPUの記録は`build/runtime-windows/Release/gkcore.log`です。

GPU smokeは初期化、PNG画像とGLBモデルの読み込み、2D・3D・文字・照明・独自shader・ポスト処理のAPI呼び出しとPresent、960×540へのresize、リソース削除後の描画、終了と再初期化を確認しました。COM reflectionとモデルshaderのreflection検査も成功しました。配布検査ではinstall済みSDKだけを参照する別consumerのビルド・実行が成功しましたが、そのconsumerはInitを呼ばず、GPU描画は検証しません。

同日のRuntimeOFF構成はDebugとReleaseの全targetを再ビルドし、CTestが各26/26成功しました。ログは`build/native-validation/cpu-debug-final-{build,tests}.log`と`cpu-release-final-{build,tests}.log`にあります。

画素読み戻し、サンプルの見た目と品質、FBXなど全形式の実GPU表示、Runtime Debug、他のGPUとWindows 10での検証は未実施です。固定Forgeの開発用shader reloadは`reload-server.txt`がないエラーを出しますが、今回のRelease実行は継続して成功しました。Runtimeの設定項目に無効化の指定がないため、依存物のビルド設定として今後整理します。配布ライセンスの最終確認と初学者による導入確認も残っています。

## 2026-10-05: Windows画素読み戻しによる描画確認

NVIDIA GeForce RTX 4070 SUPER / driver 610.74、Windows 11 build 26200、Visual Studio 18 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0、Python 3.11.9で、開発用のGPU画素読み戻しを使い、最終swapchain画像を検査しました。Sceneの代表画素が灰色 `(188, 188, 188)` のままになる問題を、default/direct/tintの画像検査で再現しました（RED: `build/native-validation/render-capture-red.log`）。

原因調査では、固定FSL compilerが文字列register名をPythonの`is`で比較し、配列registerの割り当てを飛ばすことを確認しました。製品に含めない一時copy上で比較を`==`へ変え、texture配列`t0[2]`、sampler `s2`、constant buffer `b3`の割り当てを確認しました。既存のreflection期待値も実際の割り当てに合わせました。変更したFSL一時copyから13 shader artifactを再生成した後、default/direct/tintの画素検査が成功しました（GREEN: `build/native-validation/render-capture-green.log`）。Sceneの赤は `(232, 0, 0)`、tint適用後は `(149, 0, 0)`、UIの緑 `(0, 255, 0)`は維持されました。3D三角形は青 `(0, 0, 232)`、fixture PNGはmagenta `(232, 0, 232)`、日本語UI文字は719個の白画素として検出されました。

画素検査専用のRuntime DLLは通常の配布SDKに含めない構成です。別途、Runtimeを無効にしたWindows開発構成をMSVC 19.51 / SDK 10.0.28000.0でDebugとRelease再buildし、CTestは各27/27件成功しました。ログは`build/native-validation/cpu-{build,test}-{debug,release}-capture.log`にあります。mixed_sceneは通常表示と最大化表示で目視し、custom shaderとrectangle outline / Bloomを有効にした描画を確認しました。

この時点では、model_lightingの球が黒く小さな反射点だけ見える別問題を調査していました。default/direct/tintの結果はモデル照明を検証しません。続く節にモデルの修正と最終検証を記録しています。SpaceとEscapeのキー操作は未確認です。

## 2026-10-05: モデル材質のGPU画素検査と白textureの修正

Windows 11 build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 18 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0で、モデル照明サンプルと同じ`model_lighting.glb`、camera、model transform、ambient/directional lightを使うGPU画像検査を実行しました。基準方向と90度回転方向を別々にcaptureし、既存のdefault/direct/tintを含む5 modeがすべて成功しました（`build/native-validation/model-texture-green.log`）。

RED調査では、白の1x1 fallback imageを作るTextureCacheに`TEXTURE_CREATION_FLAG_FORCE_2D`がなく、D3D12でTexture1Dとして分類される一方、shaderはTexture2Dとしてsampleしていました。比較診断ではColor描画に有色画素が26,570個あるのにTexture描画は0個で、色付きgeometryとtexture経路の問題を分離しました。TextureCacheに2D指定を加えた後のcaptureでは、基準方向の非金属球/金属球の色画素が9,461/12,551個、90度回転時が9,041/12,563個でした。方向間で色が変わった画素は非金属側9,687個、金属側7,945個です。

同じWindows PCでmixed_sceneを通常表示と最大化表示で確認し、カスタムポストエフェクト、輪郭矩形、Bloomを有効にした描画を目視しました。通常Runtime DLLでモデル照明サンプルも再起動し、左右の球の色・明暗・反射を確認しました。Alt+F4による終了を確認しました。画素判定は固定画像の領域・色優勢・差分画素数による回帰検査であり、全画面の基準画像比較や全GPUに対する画質acceptanceではありません。Spaceによる照明切り替えとEscape終了の自動操作も未確認です。

画素capture用Runtime DLLは開発テスト専用で、通常の配布SDKに含みません。install済みSDK consumerには`GKCORE_PACKAGE_GPU_SMOKE`のopt-in経路があり、Init、Scene/UI描画、Present、Shutdownを呼びます。従来の通常consumer検査はGPUを使わない構成です。最終の`PRE_SETUP.bat --gpu-check`は`BUILD READY`となり、Release全CTestが32/32件成功しました。GPU smokeは2.46秒、5 modeの画像検査は5.95秒、インストールSDK consumerは3.77秒、全体は15.05秒でした。ログは`build/native-validation/pre-setup-render-final.log`です。通常の`gkcore.dll`には画像取得用の環境変数文字列がなく、検査用DLLだけに含まれることも確認しました。RuntimeOFFのWindows CPU構成はMSVC 19.51 / SDK 10.0.28000.0でDebug/Release各27/27件成功し、ログは`build/native-validation/cpu-{build,test}-{debug,release}-capture.log`にあります。

## 2026-10-07: 同一アプリの連続フレームと効果切り替え

同じアプリ内で6回のPresentを行い、ポスト効果を無効・有効へ交互に変更するGPU画像テストを先に追加しました。既存の画像取得は最初の1枚で終了するため、2枚目の`sequence.ppm.frame1.ppm`がないことでREDとなりました。ログは`build/native-validation/sequence-red.log`です。

開発テスト専用の画像取得に`GKCORE_TEST_CAPTURE_FRAMES`を追加しました。省略時は1枚、指定時はASCII数字の1〜16を受理し、0、17、数値でない値を診断付きで拒否します。各画像は対応するframe fenceの完了後に保存し、読み戻しresourceを解放してから次の画像を取得します。要求枚数へ到達すると取得を止めます。通常Runtimeの公開APIや配布ファイルは変更していません。

focused画像テストが成功し、Sceneの赤成分が232、149、232、149、232、149となり、UIは全フレームで `(0, 255, 0)`を保ちました。各フレームの3D三角形、PNG画像、日本語文字も判定しました。枚数を省略した場合と明示的な1枚指定が途中で取得を止めること、不正枚数で終了コード1と変数名入りの診断を返し画像を作らないことも確認しています。独立レビューで同期・resource寿命・枚数上限を確認しました。

Windows 11 Pro build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0のRelease構成で、次を実行しました。

```powershell
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
```

全targetのビルドとCTest 32/32件が成功しました。GPU smokeは2.52秒、画像検査は10.18秒、配布SDK consumerは4.82秒、全体は21.37秒です。ログは`build/native-validation/sequence-final-build.log`と`sequence-final-tests.log`です。変更したC++/PythonはUTF-8 BOM付き・CRLFで、C++はclang-format 12で確認しています。

この検査は各フレームの画像取得時にGPUの完了を待ちます。高負荷時のちらつき、複数フレームがGPU上で同時に処理される状況の競合、入力キー操作を検証した結果ではありません。画像取得の設定はプロセス内で一度読み取り、同じ取得用DLLインスタンスをShutdown後に再Initして取得し直す用途は未対応です。


## 2026-10-07: 短いキー押下の保持と、多数描画の連続実行

`IsKeyDown`は問い合わせ時の押下中状態を返すため、イベント処理の間に押して離したキーを検出できませんでした。新しい`WasKeyPressed`は直近の`ProcessEvents`で記録した新しい押下を次の処理まで保持し、問い合わせで消費しません。既存の`IsKeyDown`の意味は維持しました。Spaceの切り替えには新APIを使い、Escape終了は両者を調べて短い押下と押し続けに対応します。

純CPUの`FKeyboardState`は、同じ処理内のdown/upで押下を残す契約をstubが満たさず終了コード1となるREDを確認してから実装しました。focusで既にheld状態を登録していても初回のdownで押下を記録し、状態を消去した後のrepeatで誤った押下を作らない追加契約もREDからGREENへ進めました。Win32の初回押下と反復は[WM_KEYDOWN](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-keydown)と[WM_SYSKEYDOWN](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-syskeydown)のbit30で区別しています。テストは短い押下、非消費の問い合わせ、次の処理での消去、長押し・反復・再押下、focusでの消去と復帰、仮想キー範囲を検査します。

公開APIのREDは`build/native-validation/input-public-red.log`に記録しました。追加した契約テストが`WasKeyPressed`未宣言でコンパイルに失敗し、公開宣言・キー変換・focus境界・backend転送の実装後にcoreテストが成功しました（`input-public-green-build.log`、`input-public-green.log`）。インストール済みSDK consumerでも新APIをリンク・呼び出す経路を追加し、packageテストが成功しました（`input-package-final.log`）。

連続描画のfixtureは123フレームを処理します。各フレームでSceneの全画面矩形を1024枚重ね、最後の矩形を赤・青へ交互に切り替え、3D三角形とUIを加えます。既存の取得機能が最初の2フレームを保存した際は、期待するフレーム119の青に対して赤 `(232, 0, 0)`が読み戻され、REDとなりました（`frame-stress-red.log`）。開発テスト専用の`GKCORE_TEST_CAPTURE_START_FRAME`を追加し、119フレームまでは画像取得のための待機を入れず、通常の描画同期で進めました。フレーム119はScene `(0, 0, 232)`、120は `(232, 0, 0)`となり、両方で3D `(0, 232, 232)`、UI `(0, 255, 0)`、日本語文字を検査しました。設定値は枚数1〜16・開始番号0〜65535の範囲で読み取り、不正な値には診断を返す実装です。開始番号を省略した場合は0です。

通常Runtimeの輪郭サンプルを起動し、短いSpace入力でBloomのON→OFF→ONと輪郭の明るさの変化を確認しました。短いEscape入力でウィンドウとプロセスが終了することも確認しました。操作にはcomputer-useのskyを使い、SendInputやPostMessageを使うテスト用の操作コードは加えていません。Arrowの長押し、全キー・全サンプルの操作は未確認です。

Windows 11 Pro build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0のRuntime Releaseで、次を実行しました。

```powershell
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
```

全targetのビルドとCTest 34/34件が成功しました。GPU smokeは2.60秒、連続取得を含む画像検査は11.02秒、多数描画は2.11秒、SDK consumerは3.83秒、全体は22.58秒です。ログは`build/native-validation/input-stress-final-build.log`と`input-stress-final-tests.log`です。新APIを使うconsumerへ更新した後のpackage単独検査も成功しました。CPU RuntimeOFFはMSVC19.51 / SDK28000でDebug/Releaseをビルドし、各28/28件成功しました（`input-cpu-debug-{build,tests}.log`、`input-cpu-release-{build,tests}.log`）。

多数描画の試験は描画順・フレーム更新・終了までの回帰検査です。GPU使用率や目標FPS、全フレームのちらつき、全エフェクトの画質を評価した結果ではありません。Runtime Debug、別GPU、全モデル形式の確認は残っています。


## 2026-10-07: 全キーの契約とRuntime Debugの実行

公開47キーの独立したvirtual-key期待表を使い、`IsKeyDown`と`WasKeyPressed`について、期待するキーの問い合わせ、別キーの状態が漏れないこと、focusがない場合はbackendへ問い合わせないこと、不正値と未初期化時の診断を確認しました。内部の仮想キー256個についても、押下・解放が同じ処理内にある場合、非消費の問い合わせ、次の処理での消去、seed・反復・focus消去を確認します。これは既存契約の回帰範囲を広げる変更で、新しいRuntime動作や未検出の不具合を装うREDは追加していません。実物の全キーを操作した検証ではありません。

`PRE_SETUP.bat --configuration Debug`を追加しました。既定はReleaseのまま、DebugではForgeを`build/forge-debug`、Runtimeを`build/runtime-windows-debug`へ保存します。MSBuildの構成、CMakeの生成対象、buildとCTestの構成を同じ値に揃えます。設定選択のテストは新しい引数が未対応でTypeErrorとなるREDを確認してから実装し、13/13件成功しました。構成不明値は拒否します。

最初のDebug実行は描画が成功しましたが、画像取得時の診断キューを調べると`GKCORE_TEST_D3D12_INFOQUEUE=unavailable`でした。Debug macroの有効化処理やDXGIのlive-objectレポートだけでは、D3D12 Debug Layerが動いている証明にはなりません。Debug画像テストでInfoQueueがない場合に失敗する契約を追加し、Presentが`The D3D12 Debug InfoQueue is unavailable`で失敗するREDを記録しました（`build/native-validation/debug-layer-required-red.log`）。

固定Agility SDKの`d3d12SDKLayers.dll`をDebug出力へ配置すると、InfoQueueがactiveになり画像テストが成功しました（`debug-layer-staged-test.log`）。CMakeのDebug専用構成にだけこのDLLをstage/installするようにし、Debug packageでの必須化とReleaseでの混入拒否をテストしました。allowlistはAPI未対応のREDから実装後12/12成功へ進みました。DLLを別の検証用ファイルへ移した後の増分ビルドでも復元され、固定依存物とSHA-256が一致しました（`debug-layer-restage.log`）。Release出力には同DLLがありません。

MSVCの`/MDd`で`_DEBUG`が定義され、固定ForgeのConfig.hで`FORGE_DEBUG`、GraphicsConfig.hで`ENABLE_GRAPHICS_VALIDATION`とruntime checksが有効になります。ForgeとRuntimeはどちらもDebugに揃え、条件付きのヘッダー構造とCRTを一致させています。`RendererDesc`のGPU-based validationはfalseのままです。固定Forgeの診断にはメッセージのフィルターがあり、この結果はすべての診断・性能・画質の全面合格を示しません。既知の`reload-server.txt`不足のエラーも残ります。

Windows 11 Pro build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0、CMake 4.3.1、Python 3.11.9で次を実行しました。

```powershell
.\PRE_SETUP.bat --configuration Debug --gpu-check
.\PRE_SETUP.bat --gpu-check
```

Debugの全34テストが成功し、画像取得13回すべてでInfoQueue activeを記録しました。GPU smokeは2.48秒、画像検査は11.05秒、連続描画は1.73秒、Debug SDK consumerは3.77秒、全体は21.59秒です。ログは`build/native-validation/pre-setup-debug-validated.log`です。Releaseも全34テストが成功し、同じ順に2.32秒、10.17秒、1.63秒、3.62秒、全体20.31秒でした（`pre-setup-release-after-debug.log`）。Release/Debugで取得した通常・FXAA無効・tint・モデル照明2方向・連続6フレームの計11画像はbyte単位で一致しました。これは同一GPUとdriverの回帰結果です。

RuntimeOFFのCPU構成もDebug/Releaseで再ビルドし、各28/28テストが成功しました。MSVC19.51 / SDK28000の別toolchainで、ログは`build/native-validation/key-coverage-cpu-{debug,release}-{build,tests}.log`です。Debugのビルド出力をReleaseと混ぜた場合の動作、別GPU、GPU-based validation、全キー・全モデル形式の実操作は未確認です。


## 2026-10-07: 各ポストエフェクトのGPU画像確認

独立した実GPU fixtureを追加し、基準、露出低・高、トーンマッピング無効、彩度0・2、コントラスト0・2、Bloom強度0・4、FXAA有効の11設定を比較しました。HDR式やCPU側のreference式を期待画像へ再実装せず、効果の方向・境界条件とUIの不変を検査しています。

初回検査では、露出0.25の白い三角形が `(165, 165, 165)`となり、共通の『白180超』という条件で失敗しました（`build/native-validation/post-effects-first.log`）。これは露出による正しい明度低下をテストが拒否したもので、Runtimeの不具合ではありません。彩度0・コントラスト0で色を失う場合にも橙色の固定条件を当てないよう、描画存在の確認を基準画像へ限定しました。コントラストの比較も、共に暗い2領域の差ではなく、白い三角形と灰色領域の明暗差を検査します。Runtimeの係数・アルゴリズムは変更していません。

露出0.25/1/4で橙色は `(63, 17, 3)`、`(154, 61, 19)`、`(227, 150, 67)`となり、全成分が順に増えました。彩度0は `(90, 90, 90)`、2は `(195, 0, 0)`でした。コントラスト0の基準領域は188の共通中間値、2では白255・灰0となりました。トーンマッピング無効時は白が232から255へ変わりました。

Bloomの有効・強度0は無効時の基準と全画素一致しました。強度4では白い矩形の外側で1,188画素が明るくなり、最大RGB差は156でした。FXAAは斜辺を含む局所領域で558画素が変わり、中央の平坦部は232を維持しました。11設定のすべてで緑の不透明UI矩形と、黒背景を含む文字領域の画像データは同一でした。透明UI一般を検証した結果ではありません。

設定の保持は3フレームで確認しました。BeginFrame後に露出を4へ変更した最初の画像は基準設定、次のBeginFrame後に0.25へ変更した画像は露出4、3枚目は露出0.25の個別画像と全画素一致しました。独立レビューを受け、tone-offの質的方向の検査と、失敗実行で余分な画像が残った際の再実行性も改善しました。

Windows 11 Pro build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0のRuntimeで、両構成をbuildして全CTestを実行しました。

```powershell
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
```

Releaseは全35/35件成功、追加画像テスト14.14秒、全体35.59秒でした。Debugも35/35件成功、追加画像テスト15.45秒、全体38.72秒でした。ログは`build/native-validation/post-effects-{release,debug}-final-{build,tests}.log`です。DebugはInfoQueue取得を必須とする既存画像取得経路を使います。設定11枚・設定保持3枚の計14画像はRelease/Debugでbyte単位に一致しました。同じGPUとdriverの回帰結果です。

新しい取得コードとテストは開発用で、Runtime SDKのinstall対象には含めません。全面画像の画質、ちらつき、GPU負荷や目標FPS、他GPU、透明UI全般の評価は別に行います。ユーザー向け文書には実GPU画像を色変換せずPNGとして掲載しました。


## 2026-10-07: 透明UI、画像回転と削除後のGPU描画

開発用GPU fixtureで、RGBA赤画像のalpha 0・64・128・255を青い不透明UI背景へ合成し、alphaBlendを無効にした場合も確認しました。alpha0は青 `(0, 0, 255)`、64は `(137, 0, 224)`、128は `(188, 0, 187)`、255は赤 `(255, 0, 0)`でした。alphaBlend=falseではalpha0を含む両端が赤になりました。1×1の半透明画像を64倍に拡大した場合も `(188, 0, 187)`でした。

4色の32×16画像を原寸表示し、中心指定で2倍・90度回転した表示も検査しました。画面の下方向を正のYとする時計回り回転で、回転画像は左上が青・右上が赤・左下が白・右下が緑になりました。初回テストは原寸画像を2倍と誤解した座標で範囲外を読み、期待の緑に対してScene背景 `(40, 80, 120)`となって失敗しました（`build/native-validation/image-lifetime-first.log`）。独立レビューでも同じ指摘があり、サンプル座標と比較領域を32×16内へ修正しました。これはテストの座標誤りであり、Runtimeの描画処理は変更していません。

画像を描画queueへ登録した後、Presentより前に3つのhandleを削除しました。古いhandleでの新しいDrawImageは診断付きで拒否され、登録済みの画像は取得画面に残りました。別の実行では132フレームで毎回3画像を新規LoadImageし、描画登録とhandle削除を繰り返しました。396個のpayloadがGPU cacheの128枠を超えた後、フレーム130と131だけを取得し、基準・効果有効時と同じUI画素が保たれることを確認しました。256MiB byte budgetの上限検査ではありません。

Sceneは効果無効時の `(40, 80, 120)`から有効時 `(151, 151, 151)`へ変わりましたが、青いUI背景内の透明画像・原寸の不透明画像・回転の不透明画像の領域はbyte単位で一致しました。検査対象の背景を固定した透明UIの結果で、異なるScene背景を透過させるあらゆるUIの確認ではありません。Release/Debugの基準・効果有効・cache入れ替え後2枚の計4画像もbyte単位で一致しました。

Windows 11 Pro build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0で両Runtime構成をビルドし、新しい画像testを含む全CTestを実行しました。Releaseのログは`build/native-validation/image-lifetime-release-final-{build,tests}.log`、Debugは`image-lifetime-debug-final-{build,tests}.log`です。各構成の画像と検査値は`build/runtime-windows/image-lifetime-captures/Release`と`build/runtime-windows-debug/image-lifetime-captures/Debug`へ保存しています。

Runtime本体のソースは変更せず、取得用DLLとfixtureはSDKのinstall対象に含めません。GPU負荷・目標FPS、全フレームのちらつき、device lossや画像転送失敗を含む異常終了時のcleanup、全blend方式、他GPUは未検証です。独立レビューで見つかったTextureCacheの転送drain失敗時の資源破棄分岐も、今回の正常経路の試験では評価していません。

Releaseは全36/36件成功、画像寿命検査4.03秒、全体39.89秒でした。Debugも全36/36件成功、画像寿命検査4.36秒、全体42.61秒でした。SDK consumerを含む配布物検査も両構成で成功しています。

画像ガイドのC++サンプルはv142で構文検査し、成功しました。イベント処理の失敗と通常のウィンドウ終了を区別し、API失敗を終了コードへ反映します。


## 2026-10-07: 独自ピクセルシェーダーの定数と画像のGPU確認

描画命令用shaderは従来API戻り値とPresentまでのGPU smokeを実行していました。今回、開発用の画像検査を追加し、定数slot 0と63、画像とUV座標、描画色、登録時の値の保持、shaderの削除条件を最終描画先の画素とAPI診断で確認しました。Runtimeの実装は変更していません。

検査用HLSLは公開Shader.hlslを使い、描画色・画像sample・slot 0・slot 63を掛けます。最初の矩形だけslot 63が未設定で黒になり、以降はslot 0でSceneの赤と緑、slot 63でUIの青と黄を交互に指定しました。最後に両slotを0へ変更してからPresentしても、登録済みの矩形は登録時の色を保ちました。2種類の64×32 PNGをSceneとUIへ各1枚ずつ描き、各四象限の内部4画素を固定した期待RGBで検査しています。画像を指定しないcyan矩形は、白い代替画像と描画命令の色で描けました。

6フレームすべてで個別の期待色を確認し、偶数フレーム同士と奇数フレーム同士は画像全体が一致しました。4枚の画像領域、cyan矩形、未設定または0の定数を使う黒矩形の計6領域は全フレームで一致しました。使用中のshader削除はframe 0で診断付き-1、6回のPresent後は成功、削除済みhandleのSetPixelShaderも診断付き-1でした。

検査の対照として、slot 0・slot 63・texture・input.colorの参照をそれぞれ省いたHLSLを開発用出力先に生成してコンパイルしました。4つとも実GPU描画は完了した後、画素のassertionで失敗しました。slot 0を省くと赤いScene矩形が白、slot 63を省くと青いUI矩形が白、textureを省くとPNGの赤象限が白、input.colorを省くとcyanの代替画像矩形が白でした。ログは`build/native-validation/shader-binding-negative-controls/{ignore-slot0,ignore-slot63,ignore-texture,ignore-color}.log`です。これは既存機能の検査能力を確認するための失敗実行で、Runtimeの不具合によるREDを記録したものではありません。

Windows 11 Pro build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0で両構成をビルドし、全CTestを実行しました。

```powershell
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
```

Releaseは全37/37件成功、追加画像検査1.29秒、全体41.30秒でした。Debugも全37/37件成功、追加画像検査1.43秒、全体44.33秒でした。SDK consumerを含む配布物検査も両構成で成功しています。ログは`build/native-validation/shader-binding-{release,debug}-final-{build,tests}.log`です。Debugの画像取得はInfoQueue取得成功を必須とする既存経路で行っています。GPU-based validationは有効化していません。取得6画像はRelease/Debug間でbyte単位に一致しました。

独立レビュー後にコメントだけを整え、両構成の検査用targetを再ビルドして画像検査を再実行しました。Release 1.30秒、Debug 1.40秒で成功し、ログは`shader-binding-{release,debug}-{comments-build,focused-final}.log`です。ガイドのC++サンプルもv142の構文検査が成功しました。ROADMAPの検証状況は最新結果へ整理し、過去の各実行の詳細は本ログへ残しています。

検査用HLSLのartifactはCMakeがbuildフォルダーに生成し、取得用DLL・実行fixture・Python・入力PNGとともにRuntime SDKへinstallしません。ユーザー向け文書には取得RGBを変更せずPNGとして掲載しました。全64slot・4096件上限、独自shaderの半透明と3D深度検査、モデル描画、GPU負荷、全面画像の画質、他GPUは別の検証範囲です。


## 2026-10-07: GLB材質が選ぶ画像座標の修正

基本色の画像があるGLB材質でも、ローダーはTEXCOORD_0を固定で読み込んでいました。材質のbaseColorTexture.texCoordに従うテストを先に追加し、変更前のReleaseでCPU検査は『uv1.glbの座標が不正』、GPU検査は『UV1とUV0の画像が同じ』と失敗しました。ログは`build/native-validation/glb-uv-release-red-tests.log`です。

材質に基本色画像がある場合は、指定された番号の座標を選ぶSelectBaseColorUvを追加しました。指定省略は0、画像がない材質は従来のUV0保持と、座標がないときの0初期値を維持します。画像があるのに指定先がない場合や負の番号は診断付きで失敗します。成分型はFLOATか、0から1へ正規化するUNSIGNED_BYTE / UNSIGNED_SHORTを許可します。座標の頂点数・VEC2・bufferView・sparse制約は既存の検証経路を使います。KHR_texture_transformは未対応として明示拒否し、UVの上書き指定や変換を黙って誤描画しないようにしました。

開発用スクリプトは13種類のGLBを生成します。6種類はtexCoord省略・UV0・UV1・UV2・正規化U16・正規化U8で、7種類は指定先欠損・負数・頂点数不一致・未正規化U16・符号付き整数・normalized FLOAT・UV変換です。C++テストは読み込み前にファイルの存在とGLB2のmagic・宣言サイズを検査し、ファイル欠落を不正形式の拒否と誤認しないようにします。頂点数不一致はcgltf_validateが先に拒否します。

検査の初稿ではC++の診断変数の宣言順を修正しました。実装修正後の最初の確認では、正規化U16の入力が誤ってFLOAT + normalizedで生成されていることを発見しました。仕様で許可されないFLOATの正規化を拒否する実装は維持し、正規化U16の生成をcomponentType 5123へ修正しました。normalized FLOATは別の拒否テストとして残しています。U8のVEC2には2byteの余白を入れ、頂点属性を4byte strideで配置しました。

実GPUでは同じ形状と画像を使い、環境光1・方向光0、Bloom・tone mapping・FXAA無効で色を比較しました。UV0の四象限は赤・緑／青・白、UV1は緑・赤／白・青で、画像全体では45,000画素が変わりました。省略指定はUV0と、UV2・正規化U16・U8はUV1と全画素一致しました。背景と緑のUI領域も保持され、Release/Debugで取得した6画像はbyte単位に一致しました。材質の照明品質や全モデル形式を評価する結果ではありません。

Windows 11 Pro build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0で両Runtime構成をビルドしました。

```powershell
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```

Releaseは全39/39件成功、追加GPU検査7.40秒、全体48.37秒でした。Debugも全39/39件成功、追加GPU検査7.76秒、全体52.78秒でした。SDK consumerを含む配布物検査も成功しています。ログは`build/native-validation/glb-uv-{release,debug}-final-{build,tests}.log`です。Debug画像取得では既存の必須InfoQueue確認を通し、GPU-based validationは有効化していません。

RuntimeOFFはMSVC 19.51 / Windows SDK 10.0.28000.0の別toolchainで各29/29件成功し、全体はDebug 2.80秒、Release 2.69秒でした。ログは`glb-uv-cpu-{debug,release}-{build,tests}.log`です。コメントを日本語に整えた後、元のソースに今回の実装変更を加えた内容と、最終ソースのコメント・空白を除いた要素が一致することを確認しました。Releaseの追加2テストも再ビルド後に成功し、GPU検査7.16秒、全体7.21秒でした（`glb-uv-release-focused-final.log`）。モデルガイドのC++サンプルもv142で構文検査が成功しています。

検査用モデル・画像・生成スクリプト・画像取得DLLはRuntime SDKへinstallしません。公開APIと描画shaderのABIは変更していません。GPU画像はRGBを変更せずPNGに保存してモデルガイドへ掲載しました。仕様根拠は[glTF 2.0 Texture Info](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_textureinfo_texcoord)と[Accessor normalized](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_accessor_normalized)です。


## 2026-10-07: GLB材質のアルファ抜き

MASK材質の切り抜きをテストから追加しました。材質と描画計画へalphaMask/alphaCutoff、内蔵モデル用頂点へ2成分を保持する宣言を先に用意し、実装前のCPU/GPUテストを実行しました。CPUは『mask-defaultのalpha情報を保持していない』、GPUは『透明stripeの背景色期待に対して赤く描かれた』と失敗しました（`build/native-validation/model-alpha-red-tests.log`）。描画値の伝播を追加検査したCPUも、頂点へ既定cutoff .5が渡らず失敗しました（`model-alpha-payload-red-tests.log`）。初稿のPNGが白RGBだった点とmixed材質のOPAQUE既定cutoff期待値は、失敗を捕捉する前に入力・期待値を修正しています。

GLBローダーはOPAQUE/MASKを保持し、BLENDと非有限・負のcutoffを読み込み時に拒否します。cutoffは0以上の有限値で、2も許可します。材質の重複判定へmask/cutoffを含め、同じ画像・色のOPAQUE、MASK .5、MASK 1を誤って統合しません。ModelDrawPlanは値を検証・複写し、ModelGeometryは全頂点へ同じ有効値・cutoffを渡します。不正cutoff/NaNで以前の描画計画と頂点出力を変えない契約もCPUで検査しました。

内蔵モデル用頂点は72から80byteへ拡張しました。vertex inputのTEXCOORD3、pixel inputのTEXCOORD4へ2成分を渡し、材質の値はFLAT（頂点間で補間しない指定）で渡します。shaderは画像の線形alphaと材質alphaを掛け、MASK時にcutoff未満の画素だけdiscardします。等号は残し、残った画素とOPAQUEはalpha 1で出力します。既存の深度検査・書き込み設定を使い、discardした画素は色と深度を更新しません。公開カスタムshaderの40byte頂点と入力ABIは変えず、MASKモデルを独自pixel shaderで描こうとしたPresentは、材質条件を無視せず診断付きで失敗させます。

15種類のGLBを生成し、11種類のScene、4種類のUI、1種類の深度対照の計16画像を検査します。OPAQUEのalpha無視、MASKの既定.5、cutoff 0・1・2、factor .5・.25、alpha128/255と等しいcutoff、画像なしの材質を確認しました。青い面をモデルの後から奥へ描く深度対照では、穴だけが青くなり、残った赤い部分は前面に保たれました。独自shaderでMASKを描く呼出しは指定の診断付きで拒否され、画像を生成しませんでした。Release/Debugの16画像はbyte単位に一致しています。

最初の全体Release検査はモデルshaderのreflection 2件で失敗しました（`model-alpha-release-first-full-tests.log`）。新しいvertex inputへTEXCOORD4を誤って要求した検査をTEXCOORD3へ修正し、outputはTEXCOORD4を必須としました。また、Runtime用FSLは-Qstrip_reflectでbinding名を削るため、そこからコピーした検査用fixtureでは名前・buffer情報を照合できませんでした。既存の固定FSL/DXCと互換複製機構を共有する開発用`tests/support/compile_model_shader_fixtures.py`を追加し、検査用に--debugで照合情報を残したモデル2ファイルを生成しました。Runtime出力は通常どおり情報を削り、検査用ファイル・コンパイラーはSDKへ入れません。reflectionの3件を再検査して成功しました（`model-alpha-reflection-green-tests.log`）。

```powershell
python tools/build_gkcore_shaders.py --forge-root .devtools/The-Forge --dxc-root .devtools/dxc-1.8.2405 --output-dir build/runtime-windows/gkcore_shaders
python tests/support/compile_model_shader_fixtures.py --forge-root .devtools/The-Forge --dxc-root .devtools/dxc-1.8.2405 --output-dir tests/assets/shaders
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```

Windows 11 Pro build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0でRuntimeを検証しました。最終Releaseは全41/41件成功、追加画像検査19.88秒、全体67.97秒です。Debugも全41/41件成功、追加画像検査21.52秒、全体75.93秒でした。ログは`build/native-validation/model-alpha-{release,debug}-final-{build,tests}.log`です。SDK consumerを含む配布物検査も成功し、Debugは画像取得時のInfoQueue必須確認を通しています。GPU-based validationは有効化していません。

RuntimeOFFはMSVC 19.51 / Windows SDK 10.0.28000.0でDebug・Release各30/30件成功、全体2.94秒・2.84秒でした（`model-alpha-cpu-{debug,release}-{build,tests}.log`）。shaderのコンパイルは固定13artifact、契約テストは14件が成功しました。日本語コメントを整えた9ファイルは、コメント・空白を除いた要素のhashが実装後の基準と一致しました。変更する内部headerはSPDX NOASSERTIONとinclude guardを使い、配布ライセンス未確定の状態でライセンスを付与したと主張しません。

取得RGBを変えずにPNGへ保存し、モデルガイドへOPAQUE/MASK/深度対照を掲載しました。検査は代表画素と固定領域による機能確認で、全面画質、ちらつき、全GPU、BLEND、alpha-to-coverageの品質、影を評価する結果ではありません。未知のalphaMode文字列を既定値へ扱うcgltfの既存挙動を含め、全JSON schemaを検証したとは主張しません。標準alpha modeの根拠は[glTF Alpha Coverage](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#alpha-coverage)です。

最終のC++書式確認で、fieldの行末コメントを宣言直前へ移し、括弧内部を1行へ整えました。formatter検査と、9ファイルの機能要素のhash一致を再確認しています。

最終の書式整理後もRelease・Debugの再ビルドが成功しました。ログは`model-alpha-{release,debug}-formatted-build.log`です。


## 2026-10-07: GLB金属度・粗さの画像

MR画像をテストから追加しました。保持する画像番号と第2のUVを宣言後、実装前のCPU/GPUを実行しました。CPUはuniform-mrの画像資源が不足し、GPUはMR画像モデルと数値係数だけの参照モデルに45,000画素・最大RGB差88が出て失敗しました（`build/native-validation/model-material-red-tests.log`）。MR画像を無視した結果と参照が偶然一致しないよう、MRなしの係数1/1モデルとの色差も別に確認します。

GLB画像登録を既存のAddTextureへ共通化し、同じglTF textureを基本色とMRに使う場合は同じImageResourceを保持します。MR画像番号を材質と描画計画へ追加し、材質の重複判定にも含めました。基本色とMRそれぞれのtexCoordを選び、位置とUVの境界検査、未対応の座標変換拒否を共有します。基本色がなくMRだけを持つ材質にも対応しました。新しい項目を途中へ置いた初回全体ビルドでは、既存のaggregate初期化が崩れたため、材質と描画計画の新項目を末尾へ移し、従来の初期化順を保ちました。

第2のUVはモデル頂点からワールド変換・両clip平面・射影まで保持し、内蔵モデル頂点88byteの末尾からvertex TEXCOORD4/pixel TEXCOORD5へ渡します。近平面の交点でも基本色UVと混同しないことを、異なる2本の線形関係を持つMR UVでCPU検査しました。NaNのMR UVも拒否し、出力を部分的に追加しません。アルファ抜き情報と公開の40byteカスタムshader入力は変更していません。

TextureCacheは画像pointerと色空間をkeyにし、基本色をSRGB、MRをUNORMの線形textureへ転送します。同じImageResourceの両用途は別のGPU資源として保持・byte計上・frame固定します。既存Sprite descriptorは維持し、モデルは基本色t0/MR t1/sampler s2のdescriptorを使います。物理資源と、2画像のdescriptor pairは同じcache ownerが管理し、各128枠です。追い出し時はqueue idleと転送完了を待ち、pair descriptorを物理textureより先に破棄します。Shaderは線形MRのGを粗さ・Bを金属度に読み、既存係数を掛けます。R/AはMR計算へ使いません。MRがない場合はG/B=1の線形白を使い、従来の係数材質を保ちます。

19種類のGLB（有効15、拒否4）で画像参照・色・係数・UV・描画計画を検査しました。GPUはScene15、UI8、132回読み直した後の2画像の計25枚です。単色MRと数値係数だけの参照、R/A無視、係数乗算、同一画像の2色空間、MRのみ、独立UV1、異なるMR画像pair、MASKとMRを比較しました。単色ケースは全画素、mixed pairはモデル領域、UV patternは内部の代表画素が参照と一致しました。MRなしの係数1/1との対照は45,000画素がRGB差2を超え、最大差88でした。照明式をCPU/Pythonへ再実装して期待色を作った結果ではありません。

連続再読込は共有画像モデルを132回LoadModelし、描画登録後にhandleを削除してPresentします。sRGB/線形の264資源が128枠を超えた後のframe130/131でも参照と全画素一致しました。25画像はRelease/Debug間でbyte単位に一致しました。256MiBのbyte上限、GPU性能、全frameのちらつきを測った検査ではありません。

```powershell
python tools/build_gkcore_shaders.py --forge-root .devtools/The-Forge --dxc-root .devtools/dxc-1.8.2405 --output-dir build/runtime-windows/gkcore_shaders
python tests/support/compile_model_shader_fixtures.py --forge-root .devtools/The-Forge --dxc-root .devtools/dxc-1.8.2405 --output-dir tests/assets/shaders
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```

Windows 11 Pro build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133（MSVC 19.29.30159）、Windows SDK 10.0.22621.0のRuntimeを検証しました。Releaseは全43/43成功、追加GPU検査30.32秒、全体99.12秒です。Debugも全43/43成功、追加GPU検査32.77秒、全体109.17秒です。ログは`build/native-validation/model-material-{release,debug}-final-{build,tests}.log`です。SDK consumerを含む配布物検査も成功、Debugは必須InfoQueue取得を確認し、GPU-based validationは有効化していません。

RuntimeOFFはMSVC 19.51 / Windows SDK 10.0.28000.0でDebug・Release各31/31成功、全体2.98秒・2.93秒でした（`model-material-cpu-{debug,release}-{build,tests}.log`）。固定13shaderをコンパイルし、検査用metadata付きモデル2shaderも生成しました。内部のtexture/UV契約を更新し、公開カスタムshader ABIとRuntime SDKのファイル構成は変えません。新規enum/SRT headerはSPDX NOASSERTIONとinclude guardに従います。3ファイルのコメント整理は実装後の機能要素hashと一致しています。

独立レビューでnear-planeの第2UV検査漏れを指摘され、補間と不正値のCPU契約を追加して実行しました。取得RGBを変えずにPNGへ保存し、モデルガイドへ掲載しました。根拠は[glTF metallic-roughness texture](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_pbrmetallicroughness_metallicroughnesstexture)です。MRは内蔵モデル照明の機能で、独自pixel shaderの既存非照明経路へMR/PBR入力を追加したものではありません。影、環境マップ、normal map、BLEND、他GPU、異常な転送失敗やdevice loss時のcleanup、全面画質は別の対応・検証範囲です。

最終の書式整理は全変更C++の機能要素hashを変えず、Release・Debugの再ビルドも成功しました（`model-material-{release,debug}-formatted-build.log`）。開発ガイドの現状欄も43/43とMR25画像へ更新しました。


## GLBの法線画像と接線方向

2026-10-07にnormalTextureの対応を追加しました。先に有効・拒否GLBのCPU契約と、単色normal画像を頂点法線だけの参照と比較するGPU検査を用意しました。初回Releaseでは `gkcore.model_normal` が `uniform-normal.glb` の画像数・材質情報不足で失敗し、`gkcore.model_normal_capture` は参照との45,000画素の差（RGB差2超、最大5）で失敗しました。この時点ではnormal画像がモデルへ登録されず、shaderにも渡されていません。

geometryの先行テストも `normal map keeps its independent UV` で失敗しました。nodeと描画時の変換で法線には逆scale、接線には位置と同じscaleを使い、鏡映の符号を保持します。変換後の接線を法線へ直交させ、clip交点で法線UVと接線を位置と同じ比率で補間します。参照テストは幾何変換だけで法線を作り、照明式は複製しません。

CPU参照fixtureの `normal-reference.glb` に期待法線を割り当てる分岐の抜けがあり、テスト側を修正しました。これはRuntimeのREDとは区別します。GPUの対照条件は斜めの方向光 `(0.7, 0, 1)` と強さ2を使い、画像の有無によるRGB差10超の画素を100以上要求します。ambientは0.1、標準ポスト処理は無効、露出・彩度・コントラストは1です。

内蔵頂点は120byteで、接線TANGENT0、法線UV TEXCOORD5、有効値とscale TEXCOORD6を追加しました。pixel入力はTEXCOORD6/7/8、法線画像t2、sampler s3を使います。新しいreflection契約は古い2shaderで失敗し、新規shader生成後はDXCのxy/zw詰め込みを成分数と使用成分の一致で扱うよう修正し、vertex/pixel両テストが成功しました。公開40byteの独自shader入力は変えません。

画像cacheは基本色sRGB・MR線形・法線線形を組み合わせます。同じtexture indexを使うMR/法線は同じ線形textureを共有し、物理画像を破棄する前にそれを参照する全descriptorを無効にします。normalなしの材質は既存の係数・照明経路を保ちます。


読み込みの境界条件は正常19件・拒否10件の29GLBで確認しました。正常なscale 1e-5（行列式1e-15）を特異と誤判定する条件をゼロ/非有限判定へ直し、法線・接線はdoubleで変換・正規化してからfloatへ保存します。同一primitiveでも三角形ごとに接線Wが+1/-1となるモデルを許可し、三角形内だけでWの一致を要求します。元の法線と接線が平行な入力は、非一様変換で偶然別方向になる前に拒否します。

最初のGPU統合検査では共有画像モデルと参照の材質値が不一致でした（26,651画素がRGB差2超、最大14）。共有画像の係数をmetallic=1/roughness=1、参照をmetallic=1/roughness=128/255へ揃え、CPUにも係数検査を追加しました。これはRuntimeの不具合ではなくfixture設定の修正です。

Release全45/45は140.58秒、Debug全45/45は146.91秒で成功しました。normal画像のGPU検査はそれぞれ35.83秒、39.19秒です。ログは `build/native-validation/model-normal-{release,debug}-final-{build,tests}.log` に保存しています。Windows 11 Pro build 26200、RTX 4070 SUPER（選択GPU名をLastTest.logで確認）、driver 610.74、VS 2026 / v142 14.29.30133・MSVC 19.29.30159、SDK 10.0.22621.0で実行しました。インストールSDKだけを使うconsumerのGPU smokeも、配布物検査内で成功しています。

その後、接線Wの誤処理を画像で確実に検出できるよう、単色normalをRGBA=(192,192,255,255)、方向光を(0.7,0.4,1)へ強めました。接線Wを使わないshaderを開発用フォルダーへ生成し、鏡映モデルと参照を描く負例では45,000画素がRGB差2超、最大18となり拒否されました。本体ソースと正規shader artifactは元のbyte列へ必ず復元しています。正規shaderでのRelease再検査（normal CPU/GPU・package 3件）はすべて成功、36.19秒のnormal検査を含む合計39.90秒でした。通常法線だけのモデルとの対照は45,000画素がRGB差10超、最大13です。Sceneの単色/scale/符号/alpha/共有画像/node鏡映とruntime鏡映は参照と全画素一致しています。UI共有画像の比較は差2以下の許容範囲です。

最終fixtureに対するRuntimeOFFはMSVC 19.51.36260 / SDK 10.0.28000.0でDebug・Release各32/32成功（3.05秒・2.60秒）。ログは `model-normal-cpu-{debug,release}-final-{build,tests}.log` です。固定13shaderの生成、metadata付き2shaderとDXC reflection、NoSTL/配布物の既存検査も実行しました。

独立レビューでbatch key/bind、11入力/120byte layout、TBN、cacheの保持・退避・解放順、node変換・型・数値制約を確認しています。別texture indexが同じimageを参照する場合のdecode共有は未実装で、同じtexture indexを複数役割で使う共有とは区別します。接線の自動生成、他GPU、GPU-based validation、device loss復旧、全面画質は今回の検証範囲ではありません。


最終設定のDebug再検査（normal CPU/GPU・package 3件）も成功しました。GPU検査38.68秒、合計42.60秒で、ログは `model-normal-debug-strengthened-{build,tests}.log` です。Releaseは `model-normal-release-strengthened-tests.log` に対応します。29枚すべてのPPMがRelease/Debug間でbyte一致し、文書のPNGも最終取得RGBのまま保存したことを検査しました。18個の変更C++は固定clang-format12で書式検査に通り、BOM/CRLFを確認しています。最終のテスト設定変更後もRuntime C++の機能要素hashはレビュー時と一致しました。

全体検証と最終追加検査のコマンドです。

```bat
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
ctest --test-dir build/runtime-windows -C Release -R "^gkcore.model_normal$|^gkcore.model_normal_capture$|^gkcore.package$" --output-on-failure
ctest --test-dir build/runtime-windows-debug -C Debug -R "^gkcore.model_normal$|^gkcore.model_normal_capture$|^gkcore.package$" --output-on-failure
```

CPU構成は `build/dev-windows` でDebug/Releaseそれぞれをビルドし、全CTestを実行しました。根拠の仕様は [glTF normalTexture](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_normaltexture) と、同じ仕様の接線・bitangent定義です。


## GLBで同じimageを参照するtextureの共有

2026-10-08、別texture indexが同じimageを参照する場合の重複decodeを修正しました。先に共有画像・別image項目・役割ごとのUV・不正なUV選択/変換の契約を追加し、変更前のRelease CPUテストは `shared-image-aliases.glb` のtexture resource数で失敗しました。期待は1画像ですが、旧実装はtexture indexごとに3画像を作ります。ログは `build/native-validation/model-image-alias-cpu-red-{build,tests}.log` です。

実GPUの先行テストは、同じGLBを50回新しく読み込み、同じ位置に重ねて1frameへ登録し、全handleをPresent前に削除しました。旧実装では基本色sRGB・MR線形・法線線形を別の画像から作り、50組で150資源を要求するため128 entry上限へ達します。Presentは `A frame uses more unique images than the bounded texture cache supports` で失敗し、画像は生成されませんでした。loaderが変更前のHEADと一致することを確認してからビルドした結果で、失敗記録は `model-image-alias-gpu-red/failure.txt` に保存しています。通常のreload-server.txt未配置の起動ログとは分けて判定しています。

source imageの項目を対応表のkeyにし、decodeした画像の初期所有参照をモデルのslotへ1回だけ移します。map再利用では参照を追加せず、モデル破棄時にslotごとに1回解放します。対応表は画像数の上限を確認して確保し、画像slotへの追加成功後に公開します。役割ごとのUV検査・保存と、GPUの色形式別cacheはそのまま使います。別image項目の内容が同じでもmergeしません。


修正後のRelease全45/45は150.07秒で成功しました。追加したcaseを含むnormal画像検査は47.61秒、配布SDKのconsumer GPU smokeを含むpackage検査は3.94秒です。ログは `build/native-validation/model-image-alias-release-{build,tests}.log` に保存しています。CPU RuntimeOFFのDebug・Releaseは各32/32成功（3.63秒・2.90秒）で、`model-image-alias-cpu-{debug,release}-{build,tests}.log` に記録しています。

34 GLB（正常22・拒否12）で、同一imageの別texture indexの共有、別image項目・同PNG内容を別資源として保持、基本色/MR UV0と法線UV1の独立、UV欠損・未対応変換の拒否を検査しました。GPUは38画像（Scene20、UI11、runtime鏡映2、既存stress2、alias stress2、同frame50モデル1）です。1frame50モデルとalias再読込後の2画像は、参照と全画素一致しました。alias3種類のSceneも全画素一致し、UIは差2以下で確認します。

Windows 11 Pro build 26200、NVIDIA GeForce RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29.30133 (MSVC 19.29.30159)、Windows SDK 10.0.22621.0、CMake 4.3.1で実行しました。選択GPUとdriverはCTestのLastTest.logでも確認しました。RuntimeOFFはMSVC 19.51 / SDK 10.0.28000.0です。

独立レビューはsource画像・texture・bufferViewの配列所属、画像数上限、Append成功後のmap公開、初期参照の所有と失敗時cleanup、per-view UV保持、GPUテストの描画保持・handle削除順を確認しました。sampler対応は今回追加せず、固定samplerの既存仕様を維持します。image共有はモデル内だけで、別LoadModel同士や別image項目の内容比較による共有は対象外です。

```bat
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```

textureがsource imageとsamplerを参照する定義は [glTF Texture Data](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#textures) に従います。画像byte数上限、GPU性能、他GPU、device loss復旧、全面画質の評価を追加した結果ではありません。


Debugも全45/45成功（158.93秒、normal GPU画像50.83秒、package3.99秒）しました。ログは `model-image-alias-debug-{build,tests}.log` です。最終38枚のPPMはRelease/Debug間でbyte単位に一致し、文書の画像は取得RGBのままPNGへ保存しました。固定clang-format12による変更C++3ファイルの書式検査、BOM/CRLF、ローカル文書リンクと差分検査も通りました。最終の文書整理後もC++の機能要素hashはレビュー時と同じです。


## GLBの役割別samplerと縮小・拡大filter

2026-10-08に、GLBのsampler指定を標準のモデル描画へ反映しました。先行CPU契約は36状態の一意なindexと不正enumの出力維持でREDになり、純関数実装後は旧loaderの省略samplerがRepeatにならないmetadata契約でREDを確認しました。GPUの変更前は `sampler-wrap-repeat` の45,000画素が参照からRGB差2超・最大128で失敗しました。旧pixel reflectionも追加sampler s4/s5不足で失敗しました。ログは `build/native-validation/model-sampler-value-red-*`、`model-sampler-gpu-red-*`、`model-sampler-reflection-red-tests.log` です。

画像共有はimage recordと色形式のまま保ち、基本色・MR・法線のsamplerを独立して材質、描画計画、run、descriptorへ渡します。材質の重複除去、runの結合、model descriptorのcache key/Bind検索は全3設定を含みます。36状態のGPU samplerは既存TextureCacheが遅延生成し、画像descriptorを解放した後にcache終了時だけ解放します。別subsystemやRuntime用の独自containerは追加しません。

新しいmin fixtureの平面中心UVはpixel(320.5,240.5)に一致していなかったため、camera・60度視野・viewportから逆投影してUVを補償しました。これはテストの座標誤りの修正です。minは約16 texel/pixelの縮小、magは一定UVの拡大として、min/magの値を互いに反対に指定します。期待色はPNGの画素を線形RGBで補間して材質係数へ焼き込む参照で、照明shader式は複製しません。

最初の統合GPU検査ではmin=NEAREST/mag=LINEARでも中心画素が(125,118,255)となり、期待(128,64,255)に一致しませんでした。固定ForgeのDirect3D12実装はMipMapMode NEARESTかつ明示範囲なしではmaxLOD=0とするため、縮小が拡大filterで処理されていました。gkcore側でmSetLodRange=true/minLOD=0/maxLOD=FLT_MAXを指定し、画像の1 mipを維持したままfilter選択を分離しました。修正後の4ケースはmag nearest/linearが全モデル領域一致、min nearest中心(128,64,255)、min linear中心(125,118,255)が参照と一致しました。`model-sampler-focused-tests.log`がRED、`model-sampler-lod/results.json`が修正後の根拠です。依存ソースには変更していません。

sampler省略時のGLBはRepeat/Linear、非GLBの既存値型とnullptr引数はClamp/Linearを維持します。minの9984〜9987はミップ生成がない場合のglTF推奨fallbackへ変換します。独自pixel shader、Sprite/2D画像の既存sampler ABIは変更せず、標準モデルの内部bindingだけを3texture/3samplerへ拡張しました。内部vertexは120byteのままです。


最終検証はRelease全47/47（196.05秒、sampler GPU39.85秒）とDebug全47/47（204.14秒、sampler GPU43.94秒）が成功しました。SDKだけでビルドしたconsumerのInit・描画・Present・Shutdownを含むpackage検査も両構成で成功（3.77秒・3.93秒）。ログは `build/native-validation/model-sampler-{release,debug}-{build,tests}.log` です。Windows 11 Pro build26200、RTX 4070 SUPER / driver610.74、VS2026 / v142 14.29.30133 (MSVC19.29.30159)、SDK10.0.22621.0、CMake4.3.1で実行しています。Debug取得はD3D12 InfoQueue必須の経路です。

RuntimeOFFはMSVC19.51 / SDK10.0.28000.0のDebug・Releaseで各33/33成功（3.36秒・3.18秒）、`model-sampler-cpu-{debug,release}-{build,tests}.log` に記録しました。固定13shaderと検査用metadata付き2shaderを生成し、vertex/pixel reflectionが成功しました。新規CPU sampler契約は36状態全ての一意性、役割別metadata、同一画像と違うsamplerの材質保持、6 min-filter値のfallback、不正値/参照とplan出力保持を確認しています。

samplerの34画像はRelease/Debug間でbyte単位一致し、既存normal/画像共有38画像も一致しました。wrapの8actual/referenceはモデル領域一致、同じ画像/別sampler材質のScene/UIとstress2枚は全モデル領域一致、3役割のPBR samplerはRGB最大差1、mag2設定は全モデル領域一致、min2設定は中心画素一致です。別設定間の明確な画素差も要求し、samplerの設定を無視してテストを通せない条件にしています。文書のPNGは取得RGBを変えずに保存しました。

独立レビューでsource samplerの参照範囲と値・既定値、材質統合/planの原子的更新、sampler別run/cache/Bind key、3texture+3samplerのSRTとshader、36 sampler資源のownerと終了順、fixture座標・期待値を確認しました。LOD修正は固定Forgeの実装とGPU REDの両方から確認し、依存物にパッチを当てていません。ミップ生成、他GPU、GPU-based validation、device loss復旧、全面画質は未対応・未検証の範囲として扱います。

```bat
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```


## 色空間を保つミップ生成と全段転送

2026-10-08にGLB minFilterのミップ指定を保持して縮小画像を生成しました。先にCPU mip計画・平均・出力保持とsampler108状態の契約を追加し、stubでは `5x3 mip layout planning failed`、旧36状態では108状態の一意性がREDになりました。Runtime Releaseの根拠は `build/native-validation/model-mip-cpu-red-{build,tests}.log` です。

GPU変更前はLINEAR_MIPMAP_NEARESTの期待画素137に対し0が返り、差137で失敗しました。以前はminのfallbackと単一段だけを使っていました。`model-mip-gpu-red-{build,tests}.log` が5.08秒のREDです。新しいRGBA chainはNPOTの面積重みを含み、sRGB RGBだけ線形化して平均し、alphaとデータ画像は線形平均です。encoded normalの平均は既存pixel shaderの正規化へ通します。元画像は変更せず、candidate全体完成後だけ出力をMoveFromします。

GPUではrequested mip samplerを108状態へ拡張し、画像・色形式・ミップ有無をphysical cache keyへ含めます。計画の全段byte数をGPU資源作成前に256MiB上限へ照合し、全mipのsource/destination行strideを確認してstagingへcopyします。全段endUpdate後にCPU chainを解放します。1×1は単一資源へ正規化し、samplerがNoneの画像・公開2D/独自pixel shader経路は元画像だけを使います。samplerのLOD範囲を広げる既存修正は維持します。依存物の変更はありません。

最初のPBR fixtureは法線が+Zで方向光を受けず、MR・normalによる変化を実測できない設定でした。−Zへ修正し、同じUVでミップなし反例が明確に異なるようrho=256で最終段を選びました。元の境界UVではbase-level線形補間も平均と等しくなるため、source texel内のUVへ変更しています。これはRuntime不具合のREDとは区別する検証設定の修正です。ミップあり/なしの差は役割共有52、MR33、normal75でした。

Release focused CPU2/GPU1はすべて成功（32.61秒、GPU32.54秒）しました。4 minFilterの中心RGBは9984=0、9985=137、9986=100（参照99、差1）、9987=152（参照152）で、各選択方法を無視すると失敗する差も要求します。NPOT最終段は5×3で73、1×7で106となり参照と一致しました。照明式はテストへ複製せず、選択される画素・材質係数・頂点法線を参照にしています。ログは `model-mip-focused-{build,tests}.log` です。


独立レビューの指摘を受け、5×3線形画像の途中段58/82と最終70、8×8画像の確保失敗位置0〜11をCPUテストへ追加しました。失敗時はsourceと2出力配列を保持し、診断が空でないことを確認、成功時は別途生成した基準とbyte/配置一致を要求します。正確な確保回数を実装へ合わせたテストにはしていません。

初回全体ビルドではCMakeのsource追加が小さなcache方針テストにも入り、Array/String/PostProcessのlink依存不足で失敗しました。不要な生成コードを同targetから除外し、全構成を再ビルドしました。これは描画アルゴリズムのREDとは区別します。


Release全49/49は223.50秒、追加ミップGPU検査32.18秒、SDK consumerを含むpackage3.75秒で成功しました。RuntimeOFFのDebug・Releaseは各34/34成功（4.10秒・3.03秒）。ログは `build/native-validation/model-mip-release-final-{build,tests}.log`、`model-mip-cpu-{debug,release}-final-{build,tests}.log` です。従来のsampler34画像、normal/画像共有38画像を含む全体回帰を実行しています。

新GPU取得は28画像です。sampler4組、ミップなし、共有用途のScene/UI、MR/normalと各ミップなし反例、NPOT2サイズ、ミップ有無を同時に使うモデルの再読込後2画像と参照を含みます。個別の選択画素・設定差を確認するもので、全面画質や動的なちらつきの評価結果ではありません。

```bat
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```


検証PCはWindows 11 Pro build26200、RTX 4070 SUPER / driver610.74、VS2026 / v142 14.29.30133 (MSVC19.29.30159)、Windows SDK10.0.22621.0、CMake4.3.1です。RuntimeOFFはMSVC19.51 / SDK10.0.28000.0です。CPU生成物は依存物を追加せず、共通Array/Memory・既存sRGB変換を使用します。公開shader ABI・vertex120byteは変更していません。ミップcacheのbyte計上は全段の論理RGBA byte数で、GPU配置の整列やCPU一時領域は計測対象外です。


Debugも全49/49成功（238.05秒、ミップGPU35.13秒、SDK consumer package3.88秒）しました。ログは `model-mip-debug-final-{build,tests}.log` です。新規28画像と既存sampler34・normal/画像共有38画像はすべてRelease/Debug間でbyte単位一致しました。PNGは最終RGBをそのまま保存し、round-tripで一致を確認しています。14変更C++のfixed clang-format12とBOM/CRLF、文書リンク、最終機能要素hashも確認しました。公開SDK内容の追加依存やshader ABI変更はありません。

ミップなし/ありの有効GPU資源は分けますが、MRとnormalの同じ線形ミップ構成は共有します。1×1は単一段へ正規化します。生成時CPU一時領域やstaging行整列はcache byte budgetへ含めず、GPU配置byte実測・性能・動的ちらつき・全面画質・他GPU・GPU-based validation・device loss復旧は追加検証していません。

## 2026-10-08 GLBの自己発光材質

標準GLBモデルへemissiveFactor、emissiveTexture、KHR_materials_emissive_strengthを追加しました。線形RGB係数と強度を別々に保持し、sRGB画像のRGBだけを掛けて反射光へ加えます。画像なしは白、色の既定値は黒、強度の既定値は1です。係数は有限かつ0〜1、強度は有限かつ0以上を読み込みと描画計画で検査します。

最初にフィールドと実GLB fixture・契約テストだけを追加しました。RuntimeOFF Debugのmodel_emissiveは「emissive GLB material values were not retained」、model_geometryは「emissive UV uses the same clip-edge interpolation ratio」でREDでした。Release GPUも発光係数のモデルと既存の照明経路による参照に45,000画素の差、最大225を検出しました。REDログは `model-emissive-cpu-red-{build,tests}.log` と `model-emissive-gpu-red-{build,tests}.log` です。テストの最初のビルドはTextureSampler.hのinclude不足で失敗し、includeを直してから上記の実行時REDを取得しました。

GLBのimage map、UV選択、sampler読み込みを再利用し、自己発光画像の独立texCoord・sampler・ミップを保持します。clipの交点では他の属性と同じ比率でUVを補間し、GPU入力は内部144byte・13属性、pixel inputのTEXCOORD9/10へUVと材質係数を渡します。内蔵モデルのdescriptorはt0〜t3の4画像、s4〜s7の4samplerです。Scene/UIの両経路とrunの結合条件へ4役割を反映し、公開カスタムshader ABIは維持しました。

途中のGPU検査ではMASK fixtureのbase RGBが白のため、環境光と発光を加えた実描画が発光単独の参照と最大191異なりました。fixtureのbaseColorFactorを[0,0,0,1]にして、alphaの切り抜きを保ったまま環境光のRGBを除きました。さらに同fixtureのUVが全頂点(.5,.5)だったため、画像のアルファ差が左右へ配置されていませんでした。四隅のUVを画像全体へ対応させ、左を捨てて右を残す検査に直しました。これらは参照条件の不整合であり、Runtimeの修正ではありません。plan不変性テストもFLT_MAX成功後に古い強度のsentinelを比べていたため、直前の出力をsnapshotするよう修正しました。

独立レビューで、異なる画像のmixed材質だけではsamplerをkeyから除いた誤りを検出できないと分かりました。同じimageをRepeat/Clampの2samplerで参照する隣接材質と、同じimage・samplerで色係数/強度だけを変える材質を追加しました。loader・geometry、4役割のcache寿命、renderer・shaderを担当外のagentが読み取り専用で確認し、重大な実装問題は見つかりませんでした。

GPU検査は、色の既定値・係数・強度0・画像A無視・線形色の乗算・sRGB中間値・独立UV1・鏡映repeat・baseとの加算とimage共有・2種のmixed材質・ミップ指定・MASK・Scene/UIを参照と比較します。ambient=0、方向光=0でも発光色を保持すること、HDR強度4を露出0.25でBloom on/off双方のambient4参照と比べることも要求します。再読込132回では各Present前にhandleを削除し、frame130/131を読み戻します。参照は既存の基本色と環境光で作り、反射式をテストへ複製していません。

```bat
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```

Release全51/51は276.84秒、自己発光GPU57.32秒で成功しました。RuntimeOFF Debug/Releaseは各35/35成功（2.86秒・2.74秒）です。最終MASK fixtureを含めて再生成して確認しています。ログは `build/native-validation/model-emissive-release-final-{build,tests}.log` と `model-emissive-cpu-{debug,release}-final-{build,tests}.log` です。SDK consumerを含むpackage検査も通りました。

自己発光の取得は45画像で、Scene参照との差は最大1、UIは0です。照明0の発光、HDR/Bloom on/off、frame130/131は参照と全画素一致しました。同じimageのRepeat/Clampは左右の最大差96、ミップあり/なし反例は最大188、Bloomの有無は最大179の差を確認し、差がある画素数も要求します。単なる同一画像同士の比較ではありません。一般的な画質評価・動的なちらつき・性能・他GPUを確認した結果にはしていません。

Debugも全51/51成功（299.65秒、自己発光GPU62.66秒、SDK consumer package3.83秒）しました。45画像と判定JSONはRelease/Debug間で完全一致し、既存normal38・sampler34・mip28画像もbyte一致しました。DebugはD3D12 InfoQueueの取得を必須にした画像検査を通り、GPU-based validationは有効にしていません。最新ログは `model-emissive-debug-final-{build,tests}.log` です。PNGは最終取得RGBを変更せず保存し、PNGのdecode後もPPMとの一致を確認しました。fixed clang-format12、UTF-8 BOM/CRLF、文書リンク、STLなしの監査も確認済みです。

## 2026-10-08 GLBの環境遮蔽画像

occlusionTextureを追加しました。線形Rとstrengthから `1 + strength * (R - 1)` を求め、一様環境光だけへ掛けます。方向光と自己発光は変更しません。strengthは省略時1、有限かつ0〜1を使います。独立UV・sampler・ミップを保持し、同じimageの線形用途はMR画像とGPU資源を共有します。画像がないモデルはGPUへ渡す効果量を0にし、従来の明るさを保ちます。

まずフィールドと実GLB fixture・CPU契約だけを追加しました。RuntimeOFF Debugのmodel_geometryは「occlusion UV uses the same clip-edge interpolation ratio」、model_occlusionは「unexpected geometry or resource counts: occlusion-default.glb」でREDでした。Release GPUは遮蔽が未反映のdefault-repeat材質と参照に45,000画素の差、最大107を検出しました。ログは `model-occlusion-cpu-red-{build,tests}.log`、`model-occlusion-geometry-red-{build,tests}.log`、`model-occlusion-gpu-red-{build,tests}.log` です。

GPU REDの前にrootがfixtureを確認し、R値の並びと参照値が逆、共有画像の基本色RGBを均一な灰色へ置き換えていた参照の誤りを修正しました。共有PNGのsRGB基本色と線形Rを区別して期待色を作り、ミップの8bit平均を128/255へ揃えています。混合照明の比較も、実材質のambient1/AO0/strength.5と参照のambient.5/AOなしへ設定を分け、同じ方向光・自己発光を維持しました。これらはRuntimeの不具合とは分けた参照設定の修正です。

専用UVをloaderからnear/far clippingへ同じ交点比率で渡し、頂点末尾へUV・効果量・予約0を保持します。内部頂点は160byte、14属性です。固定Forgeの15属性・TEXCOORD0〜9の範囲内で、vertex inputのTEXCOORD9をfloat3として読み、pixel inputのTEXCOORD11 UV/TEXCOORD12固定強度へ分けます。SRTはt0〜t4の5画像、s5〜s9の5samplerへ拡張し、Scene/UIの準備・bind・描画の結合条件へ画像とsamplerを加えました。公開カスタムshader ABIは変更していません。

独立read-onlyレビューではloader・材質計画・geometryと、cacheの所有・容量・退避・descriptor、およびrenderer・shader・GPU参照を担当外のagentが確認し、重大な問題は見つかりませんでした。planの不正値では候補を公開せず、既存partの全論理fieldが保たれることを比較します。near/far両方のUV補間と、NaN UVでの出力保持もCPUで検査します。

最初のGPU focused検査はMASKの180画素、最大130で失敗しました。差は画像の左端1列だけで、中央の切り抜き境界と右側の色は参照に一致していました。元画像のRepeat/Linearではアルファが端で折り返されるため、硬い半面の参照とは一致しません。テスト用基本色のsamplerをClamp/Nearestへ明示し、AO画像側の既定Repeat/Linearは保ちました。Runtimeの描画や許容誤差は変更していません。

修正後のfocused CPU2/GPU1は成功（61.50秒、GPU61.45秒）。取得は43画像で、Scene/UI参照との差は0でした。R0/128/255の中心RGBは0/170/231、同じimageのRepeat/Clampは124/170、同じimage・samplerのstrength0/1は231/124となり、材質設定を無視すると失敗する差を確認しています。方向光のみ・自己発光のみ・混合照明、frame130/131は独立参照と全画素一致しました。反射式をテスト側へ複製せず、線形RGBへAOを焼いた既存の基本色材質や環境光の設定を参照にしています。

RuntimeOFF Debug/Releaseは各36/36成功（3.34秒・3.23秒）。ログは `model-occlusion-cpu-{debug,release}-final-{build,tests}.log`、focusedは `model-occlusion-release-focused-{build,tests}.log` です。

```bat
python tools/build_gkcore_shaders.py --forge-root .devtools/The-Forge --dxc-root .devtools/dxc-1.8.2405 --output-dir build/runtime-windows/gkcore_shaders
python tests/support/compile_model_shader_fixtures.py --forge-root .devtools/The-Forge --dxc-root .devtools/dxc-1.8.2405 --output-dir tests/assets/shaders
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```

Release全53/53は343.66秒で成功しました。遮蔽GPU56.54秒、SDK consumerを含むpackage3.79秒です。従来の自己発光・sampler・ミップ・法線・custom shader・入力・配布物も同じ全体実行で確認しました。ログは `build/native-validation/model-occlusion-release-final-tests.log`、ビルドは `model-occlusion-release-focused-build.log` です。

最終read-only確認で、MASKだけ自動比較がmodel領域に限られていると指摘されました。保存PPMは全画面でも一致していましたが、検査も全640×480へ広げ、結果JSONへ比較画素数を記録するようにしました。Runtimeとfixtureは変更せず、この強い条件をReleaseで再検査してからDebugの全体検査を実行します。

全画面比較へ広げたRelease遮蔽GPUも成功（56.69秒）しました。MASKを含む各参照比較は307,200画素を対象とし、最大差0です。ミップ有無の差の検査は指定model領域45,000画素を対象にしています。ログは `model-occlusion-release-strict-tests.log` です。

Debugも全53/53成功（371.43秒）しました。取得43画像と判定JSONはRelease/Debugで完全一致、全参照比較は307,200画素・差0でした。従来normal38・sampler34・mip28画像もbyte単位一致しています。ログは `model-occlusion-debug-final-{build,tests}.log` です。Debug画像テストはD3D12 InfoQueueの取得を必須にしており、その条件を通過しました。GPU-based validationは有効にしていません。

検証PCはWindows 11 Pro build26200、RTX 4070 SUPER / driver610.74、VS2026 / v142 14.29.30133（MSVC19.29.30159）、SDK10.0.22621.0、CMake4.3.1です。RuntimeOFFはMSVC19.51 / SDK10.0.28000.0です。新規ライブラリや公開shader ABIの変更はありません。画像生成からの動的なAO、全面画質、FPS、他GPU、GPU-based validation、device loss復旧は今回の確認範囲外です。

## 2026-10-08 GLBの接線自動生成

normalTextureを持ち、TANGENTがないGLB primitiveの接線を生成します。FLOAT NORMALと、法線画像が選ぶUVは必要です。明示TANGENTの検証・変換は従来経路を保ち、無効な明示値を生成で置き換えません。source primitiveの位置・法線・UVとlocal indexをnode変換前に一時保持し、生成後にnodeの位置・法線・接線変換を適用します。

helperをスタブにした状態でmodel_tangent_generationの平面接線契約をREDにしました。GLB loader契約はtangent-canonicalを「requires matching FLOAT NORMAL and TANGENT」で拒否し、native Release GPUも同じGLBの読込みで失敗しました。ログは `model-tangent-generation-red-{build,tests}.log`、`model-tangent-loader-red-{build,tests}.log`、`model-tangent-gpu-red-{build,tests}.log` です。

固定MikkTSpace commit `3e895b49d05ea07e4c2133156cfa94369e19e409` の原文を使用します。header/sourceを改変せず、専用wrapperがmalloc/freeをfoundationへ接続します。共通Arrayが入力とcornerごとの結果を保持し、同じsource vertexと完全に同じ接線frameだけをまとめ、異なるframeは頂点を分けます。source indexだけで上書き・平均は行いません。候補のvertices/indicesが完成したときだけ出力へ移し、途中失敗で既存出力を変えません。

入力位置とUVを参照頂点だけの共通平行移動・正の縮尺で整え、法線を正規化して計算範囲を保ちます。三角形またはUVの面積0、参照法線の零・非有限、生成frameの不正、上限超過は拒否します。出力へ使われない頂点は除きます。基本色・MR・normal・emissive・occlusionの各UVとmaterialを保ちます。

初回helper検査は割当失敗時の診断検査で失敗しました。診断Stringも動的領域を使うため、注入前に診断用容量を用意しました。上流の任意内部割当が失敗しても正常fallbackできる場合は成功を認め、解析基準と比較します。失敗時にはsourceとsentinel出力の保持、容量のある診断Stringへの理由を要求します。固定した確保回数に合わせる検査にはしていません。

独立レビューで、未参照頂点も先に法線を正規化していた不具合を修正しました。未使用判定を前へ移し、極端な未参照座標・零法線を計算へ参加させない単体テストと、GLBで5入力頂点から4出力頂点へ除外する契約を追加しました。source arraysは不変です。別の指摘を受け、平面の一様接線だけでは生成順を検証できないため、共有辺の角度重みが非一様変換で変わるモデルを加えました。

共有辺の解析モデルは2面の接線がXとY、共有cornerの角度が両面等しいため、sourceの共有接線が `(1,1,0)/sqrt(2)` です。node scale `(2,1,1)` では先に生成した接線を変換した `(2,1,0)/sqrt(5)` を参照にします。変換後に平均すると角度重みが変わります。この参照はMikkTSpaceを呼んで作らず、独立な幾何から明示TANGENTへ保存しました。面順序の反転でもsource frameが変わらないことをCPUで確認します。

旧missing-tangent fixtureはgeometryの巻き方向とNORMALが反転しているため、旧uniform-normalの明示W+1を生成の参照には使いません。新しい平面fixtureは位置・巻き方向・NORMAL・UVを揃え、既知のframeを明示したGLBへCPU corner payloadとGPU画像を比較します。Source-normalの反転自体を新規に禁止する変更はしていません。

初回native focused4/4（33.00秒）に続き、追加モデル込みfocused6/6が90.32秒で成功しました。接線GPU39.78秒、従来normal GPU50.30秒です。8基本対と角度重み2対、未使用頂点1対、no-normal-mapと6不正fixtureを含む29 GLBを使用しました。GPU取得31画像の参照差は0、normal-map有無には45,000画素で最大24の差がありました。UV鏡映、回転、UV1、nodeの非一様scale・回転・鏡映、tiny node、非indexed、共有境界、Scene/UI、runtime鏡映、132回再読込とPresent前のhandle削除後frame130/131を確認しています。

配布noticeを必須にするtest_allowlistの新契約を先にREDにし、CMake installとmanifestへ `mikktspace-LICENSE.txt` を追加して成功しました。Runtime SDKへsource/headerは入れず、新DLL依存もありません。固定source bytesとnotice原文は `gkcore.mikktspace_lock` で確認します。gkcore全体の配布条件が確定したという意味ではありません。

RuntimeOFF Debug/Releaseは各39/39成功しました。Release Runtime全57/57は376.92秒、接線GPU39.59秒、SDK consumerを含むpackage4.09秒で成功しました。最新ログは `model-tangent-cpu-{debug,release}-final-{build,tests}.log`、`model-tangent-release-final-tests.log`、ビルドは `model-tangent-release-focused-build.log` です。

```bat
cmake --build build/runtime-windows --config Release --parallel 8
ctest --test-dir build/runtime-windows -C Release --output-on-failure
cmake --build build/runtime-windows-debug --config Debug --parallel 8
ctest --test-dir build/runtime-windows-debug -C Debug --output-on-failure
cmake --build build/dev-windows --config Debug --parallel 8
ctest --test-dir build/dev-windows -C Debug --output-on-failure
cmake --build build/dev-windows --config Release --parallel 8
ctest --test-dir build/dev-windows -C Release --output-on-failure
```

生成順を誤った場合のGPU感度も確認するため、変換後の角度重みを使うvalid authored tangentの反例を追加しました。node scale2の後に共有頂点0で153.435°/116.565°、頂点1で12.529°/18.435°を使って平均したframeを、そのnodeの逆変換でsource TANGENTへ保存します。この期待値にはMikkTSpaceを使いません。30 GLBを使う最終strict CPU2/GPU1は41.37秒（GPU41.27秒）で成功しました。新しいCPU3追加pairとunused vertex数の契約もnative Releaseで再ビルドして通過しています。ログは `model-tangent-release-strict-{build,tests}.log` です。

誤順序反例との比較はmodel領域45,000画素のうち3,462画素で差2超、最大11でした。normal-map無しとの反例は45,000画素に10超、最大24です。現在の最終取得は32画像で、全参照の最大差0でした。画像の再現性と参照の幾何一致を検証するもので、全面画質の評価にはしていません。

Debugも全57/57成功（407.18秒）しました。取得32画像と判定JSONはRelease/Debugで完全一致し、すべての参照比較の最大差は0です。誤順序とnormal-map無しの反例の検出結果も両構成で一致しました。最新ログは `model-tangent-debug-final-{build,tests}.log` です。Debug画像検査はD3D12 InfoQueueの取得を必須にしており、GPU-based validationは有効にしていません。

検証PCはWindows 11 Pro build26200、RTX 4070 SUPER / driver610.74、VS2026/v142 14.29.30133（MSVC19.29.30159）、SDK10.0.22621.0、CMake4.3.1です。RuntimeOFFはMSVC19.51/SDK10.0.28000.0です。PNGは最終PPMのRGBを変更せず保存し、decode後の一致を確認しました。authored sourceのfixed clang-format12/UTF-8 BOM/CRLF、Runtime STLなし、機能token hash、vendor原文SHA、文書リンクも確認しました。NORMAL補完、アニメーション、全面画質、FPS、他GPU、device loss復旧はこの追加検証の対象外です。
