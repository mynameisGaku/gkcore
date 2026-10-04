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
