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

## 2026-10-08 GLBの欠損法線生成

NORMALがないGLB primitiveの位置・local indexから面法線を生成します。共通Arrayがcornerごとの法線を保持し、同じsource vertexと完全一致する法線は再利用、異なる面法線は頂点を分けます。入力TANGENTは0にし、基本色・MR・normal・emissive・occlusionの各UVは保ちます。法線画像を使う場合は生成法線と選択UVから既存接線helperを呼び、node変換は生成後に適用します。

helperの平面・鋭い辺・属性保持契約をスタブでREDにしました。GLB loaderとRelease GPUもgenerated-normal-flatを「matching FLOAT NORMAL」で拒否してREDでした。GPU失敗は1.88秒、loaderは0.06秒です。ログは `model-generated-normal-helper-red-{build,tests}.log`、`model-generated-normal-loader-red-{build,tests}.log`、`model-generated-normal-gpu-red-{build,tests}.log` です。

面積計算はfloat座標をdouble差分と外積へ拡げ、最大成分で正規化します。近い値を許容誤差でまとめず、生成法線のfloat値が一致するcornerだけを再利用します。零面積・非有限参照座標・不正indices・出力上限は診断付きで拒否し、候補arrays完成後に2出力へ移します。未参照頂点は生成にも出力にも使いません。空index、範囲外index、6頂点必要な折れ面へ上限5を指定した場合のsource・sentinel出力保持、確保失敗の境界も検査します。

最初のfocused CPUでは新規法線契約は成功しましたが、従来の接線生成モデル tangent-unused-source-vertex を拒否する回帰を検出しました。新しい明示NORMAL検査が、参照されない零法線まで接線helperより先に検証していたためです。staging中の接線生成では従来どおりhelperが参照頂点だけを検査し、未使用頂点を除くように戻しました。NORMALが存在する不正値を法線生成で置き換えるfallbackは追加していません。

面法線helper、GLB接続、GPU参照を独立read-onlyでレビューしました。no-mapの参照にTANGENT accessorがあっても、loaderはno-mapで読まずゼロ値を保つため、最初の参照不一致懸念は誤りとして撤回されました。特異nodeは、生成NORMALの変換では失敗、既存明示NORMAL/no-mapの経路では以前のfallbackを保持するCPU対照を加えました。

新規GPU focusedは46.26秒で成功。31 GLB（12 actual/reference対、平均法線の反例、6 CPU-only不正/特異変換の対照）を使い、35画像の参照差は0でした。sharp-foldを平均法線へ変えた反例は618画素で差2超、最大4、normal-map無効化は45,000画素に10超、最大24を確認しました。三角形の独立幾何を明示NORMAL/TANGENTへ保存した参照を使い、生成helperは期待値に呼んでいません。

RuntimeOFF Debug/Releaseは各41/41成功（3.45秒・5.37秒）しました。ログは `model-generated-normal-cpu-{debug,release}-final-{build,tests}.log`、初回focusedは `model-generated-normal-release-focused-tests.log` です。公開shader ABIの変更や新ライブラリの追加はありません。

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

Release全60/60成功（430.37秒、生成法線GPU44.25秒、SDK consumer package4.06秒）しました。ログは `model-generated-normal-release-final-{build,tests}.log` です。公開shader ABI・160byte頂点配置は変更しておらず、従来の各材質・接線・custom shader・効果・入力も同じ全体実行で確認しています。

Debugも全60/60成功（454.33秒）しました。生成法線35画像と判定JSONはRelease/Debugで完全一致し、全参照は最大差0でした。ログは `model-generated-normal-debug-final-{build,tests}.log` です。Debug画像検査はD3D12 InfoQueueの取得を必須にして通過しています。GPU-based validationは有効にしていません。

検証PCはWindows 11 Pro build26200、RTX 4070 SUPER/driver610.74、VS2026/v142 14.29.30133（MSVC19.29.30159）、SDK10.0.22621.0、CMake4.3.1です。RuntimeOFFはMSVC19.51/SDK10.0.28000.0です。PNGは最終PPMのRGBを変更せず保存し、decode後も一致を確認しました。sourceのfixed clang-format12、UTF-8 BOM/CRLF、Runtime STLなし、機能token hash、文書リンクも確認しました。全面画質、FPS、他GPU、GPU-based validation、device loss復旧は今回追加検証していません。

## 2026-10-08〜09 モデルアニメーション・ブレンド・ボーン対応・IK

GLB/FBXのclip・skin・morph、連番OBJ、外部アニメーション、独立したモデルinstance、2clipブレンド、人型ボーンの役割対応、2ボーン/汎用chainのIKを共通APIへ接続しました。再生時刻はアプリが渡し、DrawModelはその時点の変形結果を独立したモデルとして保持します。画像・材質は参照保持してコピーし、元の形状や他instanceを変更しません。

姿勢初期化とIKの未実装stubで2 CPUテストのREDを確認しました。OBJの公開再生APIは未宣言のcompile RED、GLBはskin/morph拒否、FBXはsourceを保持していない状態でREDでした。最小実装後に、固定時刻の補間、逆再生・loop、単一frameの長さ0、非有限値と有限overflow時の両slot保持、handle削除後の再生・描画予約を確認しました。

共通姿勢は位置・scaleを線形補間し、quaternionを最短経路で補間します。外部clipは名前または人型の役割で一対一に対応付け、初期姿勢のモデル空間差分を転送します。親の回転・scaleを含む位置変換、異なる骨長、morph名の順序違いと、曖昧な名前・役割重複で出力を保持する契約を検査しています。

IKは極小/極大・異なる骨長、回転・平行移動したroot、到達不能・反平行の目標、poleが目標と平行な場合を検査しました。投影の丸め残差を相対判定し、必要時は一定の軸から曲げ方向を選びます。公開APIからブレンド→IK→変形→予約まで通すテストも追加しました。

GLBは生成法線/接線を使うmorphだけ初期からcornerを分け、初期形状と各targetのframeを別々に計算して差分を係数合成します。weight 1、0.5、2targetの0.25/0.25を三角形の解析値へ照合しました。初期morph重み、生成法線のskin、TANGENTを無視すべき入力、skin付き/なしnodeのmesh共有、元byte列の解放後にclip評価できることも確認しました。FBXはufbxの評価と補助nodeを使い、単位変換、node/skin移動、morphと初期重み、固定rootの変更拒否を実データで検査しました。

独立レビューでtarget属性数の一致を要求して正当なGLBを拒否する問題を発見しました。POSITIONのみのtargetとPOSITION+NORMALのtargetを持つfixtureをREDにして、一致条件を削除し、省略した属性の差分を0とする動作を確認しました。仕様に合わなかったtangent生成fixtureも修正しました。固定cgltfのprimitive間morph個数制限は残るため、対応範囲に明記しています。

RuntimeOFFの最終CPUテストはDebug/Release各48/48成功（最終review後3.65秒/3.24秒）です。Runtime全体はRelease68/68成功（472.44秒）、Debug68/68成功（508.13秒）。この全体実行後の入力検証修正は描画計算を変えず、CPU全体とnative animation/packageを再実行しました。ログはbuild/native-validationのmodel-animation-*-final-*とmodel-animation-*-post-review-*です。

実GPUのアニメーション検査は12組の静的参照と132frame連続描画のframe130/131を比較し、すべて最大RGB差0でした。静止との画素差は1319〜4422画素、連続frame間は1655画素です。失敗時も結果JSONを残し、Debug/Releaseの判定JSONと代表PPMが一致することを確認しました。DebugはD3D12 InfoQueue取得が必須のcapture runtimeで実行しています。

検証PCはWindows 11 Pro x64 build26200、RTX 4070 SUPER / driver610.74、VS2026 / v14214.29.30133（MSVC19.29.30159）、SDK10.0.22621.0、CMake4.3.1です。CPU構成はMSVC19.51/SDK10.0.28000.0です。SDK consumerもInit・描画・Present・Shutdownまで成功しました。固定vendorソースは変更せず、自作sourceのBOM/CRLF、clang-format12、差分・文書リンクを確認しています。

## 2026-10-09 実モデルviewer・材質・処理時間の改善

実モデルの入口として`START.bat`と`OPEN_PROJECT.bat`を用意しました。編集対象は`examples/model_viewer.cpp`、通常のsolutionは`build/runtime-windows/gkcore.slnx`です。`START.bat -CheckOnly`では必要ファイルと起動引数を確認し、viewer自体を開きません。YUMEKAの基本色画像19枚と7材質の設定はローカル検証専用で、モデル・motion・変換物をSDKやGitへ追加しません。指定されたPMXモデルは変換条件を満たせないため変換していません。

GLBのBLENDを標準材質へ接続し、同じcameraで連続するSceneモデル群の透明三角形を奥から並べます。Rect/Image/Triangle、独自shader、camera変更は並び替えの境界で、UIは登録順です。深度検査は維持し、透明部分は深度を書きません。CPUの順序・境界・深度とGPUの合成を検査します。独自shaderのMASK診断を変更して既存alpha検査が失敗したため、従来の診断を戻し、alpha/BLENDのGPU検査を再実行して2/2成功しました。

`SetModelMaterial`は画像とalpha設定を持つ新しいsnapshotへ差し替えます。共有画像を他の材質役割が使う間は保持し、旧instanceや描画予約を変更しません。旧画像slotが残る契約を先に失敗させ、不要slotの解放と他用途の保持を修正しました。公開3headerをRuntime package許可リストへ追加し、許可リスト13テストが成功しています。公開API・材質寿命・VSync・packageの独立read-onlyレビューでは重大な問題を検出しませんでした。

quote includeはmodule rootまたは公開include rootに統一しました。初回の変換は477 include /192 files、shader参照も続けて統一しました。`tools/normalize_project_includes.py`の最終dry-run時点では264ファイル中変更候補0、未解決0、非include token hash不一致0です。固定依存物のsourceは形式変換していません。Runtime標準ライブラリ禁止の検査4件は成功しています。

画像取得用Runtimeはウィンドウを非表示で使います。通常DLLは表示を維持し、表示を伴うbackend smokeとSDK consumerのGPU実行だけ`GKCORE_RUN_INTERACTIVE_TESTS`で明示的に選びます。既定はOFFです。非表示と通常表示の同一静止画像も比較しました。ユーザーが表示試験を許可した後は通常DLLのvisible benchmarkでFPSを測定しています。

viewerはVSyncを無効にし、Windowsの性能counterで実経過時間を計り、アニメーションへ渡します。`SetVSyncEnabled`の未実装stubでframe snapshotの契約を失敗させてから実装し、次のBeginFrameへの適用とShutdown後の既定trueを確認しました。実モデルbenchmarkはwarmupを除外し、測定完了・平均FPS・p95・DrawModel/Present時間をJSONへ残します。

YUMEKAの透明三角形追加で、1三角形ごとに必要数ちょうどへReserveしていたため、巨大な頂点配列を繰り返しコピーしていました。foundationのまとめて追加するAPIと連続三角形のbatchで、静止表示が0.189FPS / Present5290.017msから24.120FPS /41.447msへ改善しました。形状をGPUへ保持し、cameraとmodel変換をshaderで適用することで静止340.016FPS、毎frame回転340.021FPSになりました。ログは`yumeka-static-performance-{initial,batched,cached,gpu-projected}.log`と`yumeka-rotate-performance-gpu-pose.log`です。

GPU cache・buffer offset・descriptor・終了とeviction時の寿命を独立read-onlyレビューし、重大な問題を検出しませんでした。続けて3軸回転・負の非一様scale・off-axis cameraを組み合わせたactualと、Pythonの独立軸回転・法線変換で事前に焼き込んだGLBを比較しました。ReleaseのBLEND画像検査は追加6 captureを含め1/1成功（19.26秒）。Opaque参照は最大channel差1、差2超0画素、全channel平均差約0.0000011、BLEND参照と逆提出順は全画素差0でした。変換を外したidentity反例は37,477画素で差12超になり、試験の感度も確認しています。ログは`model-projection-release-{build,tests}.log`、結果は`build/runtime-windows/model-blend-captures/Release/results.json`です。新しいGPU skin計算やcache予算超過の回帰を確認した結果ではありません。

FBXは動かない形状情報とskin入力を保持し、線形skinと法線生成を軽く評価する経路を加えました。対応できない入力は従来ufbx評価へ戻します。YUMEKAは175,075cornerから45,233position /55,747normalにまとめ、全cornerの位置・法線を従来評価へ照合しています。描画時点の姿勢は参照保持し、後から再生時刻やhandleを変えても予約済み描画に影響しません。source削除後のmaterialize、同一frameの異なる姿勢、未知sourceの不正出力と割当失敗時の保持をCPUで確認しました。focused deferred-pose API/透明順序の2/2は成功しています。

通常DLLでの外部Mixamo2motionのブレンドは、GPU投影・姿勢buffer・重複検査の整理後に156.214FPS、p95 6.765ms、DrawModel3.672ms、Present2.723msでした。300 frame /30 warmup、1280×720、基本色画像、既定の効果で測定しています。300FPSには未達で、変形計算のGPU化と統合検査を継続しています。数値は確認した時点の結果であり、以降の未検査変更の成功を表しません。

この測定のPCはWindows 11 Pro build26200、RTX 4070 SUPER / driver617.42、VS2026 /v14214.29.30133（MSVC19.29.30159）、SDK10.0.22621.0、CMake4.3.1です。過去のdriver610.74の記録とは区別します。通常SDKの計測コードを無効にした最終build、CPU Debug/Release全体、新しいGPU変形経路を含むnative Debug/Releaseとpackageの統合検査はまだ完了していません。

GPU skinの入力はmodel/animationの既存sourceが所有し、rendererがwire形式へ変換する境界にしました。poseごとのcluster行列、ゼロweight頂点用のmesh/geometry変換、face順・重複を保ったnormal CSRをCPUで確認します。試作のGPU起動では、未stageのcompute artifact、face/corner offsetと出力strideの不整合、buffer再利用時のstate管理を修正しました。ReleaseとDebugでdevice removalを検出したため、起動の反復を止め、即時InfoQueue診断を追加して小fixtureへ絞りました。

診断は`CREATE_CONSTANT_BUFFER_VIEW_INVALID_SIZE`（id650）で、2MiBの定数arena全体へCBVを作ってD3D12の64KiB上限を超えていました。既存のdraw constant arenaと同じ`NO_DESCRIPTOR_VIEW_CREATION`を付け、slotごとの96byte range CBVだけを作る修正後、小fixtureはDebugで完走し、id650とdevice removalが出なくなりました。ログは`gpu-skinning-fixture-{diagnostics,nodescriptor}.log`です。異なるshader ABIへ置き換えて回避する変更は採用していません。

capture専用`GKCORE_VERIFY_GPU_SKINNING=1`はGPU出力を読み戻し、保持済みCPU pose arenaへ全position/normalのXYZ/W、有限値、件数と範囲を比較します。通常SDKにはこの読み戻しと同期を含めません。Release tiny FBXの3frameは各4position/4normalで最大差0でした。一方、実YUMEKA external-blendは45,233position/55,747normalの位置最大差約0.000000238に対し、法線最大差約0.00864で許容0.0005を超えたため、Present失敗・`completed=false`になりました。GPU計算の正しさはこの時点で未達です。許容値を広げず、CPU計算を外す前に原因を修正します。ログは`gpu-skinning-readback-{tiny,yumeka,yumeka-detail}.log`です。

ufbxはdoubleの変形位置から面法線を計算していました。GPUへ渡す元位置・weight・bind変換・pose行列と、skin中間位置・面法線・group加算をdoubleに揃え、描画出力だけfloatへ変換しました。FP64非対応deviceはCPU経路を使います。Release readbackで、変換付きtiny fixtureの6frameは位置差0・法線最大差約0.000000060、実YUMEKA+両Mixamo motionと材質設定の6frameは毎回45,233position/55,747normalを検査し、位置差0・法線最大差約0.000000179で成功しました。許容0.0005は変更していません。ログは`gpu-skinning-fp64-{tiny,yumeka}.log`です。

immutable skin入力を64MiB/64entryのGPU_ONLY cacheへ保持し、source ModelResourceの参照と現在frameのpinを管理します。poseごとの行列は別の小さなupload bufferへ渡し、大きな入力の毎frame再構築を外しました。YUMEKA readbackの6frameは同じ精度で成功、通常表示の300frame/30warmupは144.148FPS、p95 7.282ms、DrawModel3.617ms、Present3.313msでした。ログは`gpu-skinning-cache-yumeka.log`と`yumeka-cached-fp64-profile.log`です。BLEND・複合変換の画像検査も1/1成功（19.10秒）。この時点ではCPUでも完全な変形を計算しているため、300FPSには未達です。

GPU対応の描画予約では共通poseとFP64行列を保持し、CPUはBLENDが参照する位置だけ評価します。GPU非対応backendや姿勢は旧評価へ戻り、CPU materializeが必要になった場合は保持したposeから完全な形状を作ります。GPU mockのfreeze・materialize・capability false・BLEND subset・zero influence fallbackはcore/model_animation_apiのDebug/Release各2/2で成功しました。reference検証用captureは完全なCPU評価を残します。

GPU評価結果の異常は4byteのGPU flagへ記録し、同frameのfenceと読み戻しでPresentが診断付き失敗にします。使われないnormal groupの零値は拒否しません。退化fixtureの初回はskin deformerの接続不足でtriangleが動かず、`unexpected_success`のREDでした。SkinとGeometry接続を直した後、正常rest描画→clip時刻0.5のDrawModel成功→Present失敗「degenerate referenced normal」をRelease native CTestで確認（1/1、1.32秒）。CPU reference評価を無効にしてproductionのerror flagを使います。ログは`model-gpu-skinning-error-release-{tests,green}.log`です。

placeholderのまとめ追加、GPU poseだけのframeでは不要なCPU uploadを外す処理、frame内のDrawPlan共有で、通常表示は261.180FPS、p95 4.223ms、DrawModel0.980ms、Present2.841msになりました。GPU/CPU readback精度は維持し、transparent planのdirect/cached一致も確認しています。ログは`yumeka-{staging,drawplan}-profile.log`です。300FPSと最終Debug/Release統合検査はまだ未完了です。

retargetのrest情報をbinding作成時に保持し、renderer内のGPU placeholderもまとめて追加することで、計測用コードを含めない通常SDKで300FPSを超えました。最終read-onlyレビューで、モデル間の共有face-normal buffer再利用にUAV barrierを追加し、double位置からfloatへ変換した結果の有限値も検査するよう修正しました。複数modelの8frame読み戻し、退化pose、float overflowと大きい合法poseの4 GPU回帰は成功しています。

大きい合法poseの対照検査では、HLSLの`isfinite(double)`がDXILで暗黙にfloatへ変換され、有限doubleのraw face normalを拒否する問題も発見しました。doubleの指数bitで有限値を検査し、描画用float位置には別のfloat有限値検査を適用します。FBX単位換算後の約5e37の位置は成功、約5e39の位置はPresentが診断付きで失敗しました。許容誤差は変更していません。

最終shader修正後の通常Release Runtime（metrics OFF、RTX 4070 SUPER / driver617.42、1280×720、300 warmup /3,000測定frame、YUMEKA+両Mixamo motion、基本色画像と既定の効果）は326.374FPS、p95 3.388ms、DrawModel0.726ms、Present2.330msで完走しました。ログは`yumeka-double-finite-normal-off-3000.log`です。6frameの全45,233position /55,747normalは位置最大差0、法線最大差約0.000000179で成功（`yumeka-double-finite-readback.log`）。静止・回転の同じ長さの測定は各340.020FPSでした。これは平均300FPSの確認で、すべてのframeが3.333ms以下だったことや他GPUでの性能保証ではありません。

compute shaderを含む配布物の必須契約も追加しました。欠落を拒否せず、追加したartifactを余剰として拒否するREDを記録してから、許可リスト13件とshader plan7件をGREENにしました。Runtime artifactは14個です。ログは`model-skinning-{package,shader-plan}-{red,green}.log`です。

旧生成directory17個を`build/archive`へ移し、既知のprobeを検証用build領域へ集めました。検査済みの範囲を削除する操作は自動承認レビューに拒否されたため、実削除は未完了です。`CLEAN.bat -WhatIf`で対象だけを確認する手段を用意し、現在のbuild・素材・capture・最新logは削除対象に含めません。

最終統合では、GPU cacheだけで構成されたSceneを描画しない条件漏れを既存render_captureが検出しました。cached vertex数をScene判定に加えて修正しています。続いて古い頂点shader artifactが残った状態では金属材質の照明変更検査が失敗したため、最新shaderを再生成してstageし、既存の画素差条件を変更せず確認しました。

最終CPUはDebug53/53件（3.25秒）、Release53/53件（3.58秒）成功。nativeはDebug78/78件（571.96秒）、Release78/78件（509.18秒）成功し、SDK consumerも含みます。ログは`final-cpu-{debug,release}-{build,tests}.log`、`final-native-debug-{config,build,tests}.log`、`final-native-release-confirm-{build,tests}.log`です。

最新shaderを使う通常Release Runtime、metrics OFF、1280×720、300 warmup /3,000測定frameの最終計測は、YUMEKAと両Mixamo motionのブレンド338.147FPS、p95 3.289ms、DrawModel0.679ms、Present2.272msでした。静止340.020FPS /p95 2.976ms、毎frame回転340.020FPS /p95 2.974msです。ログは`final-real-model-benchmark.log`、`final-real-model-static-benchmark.log`、`final-real-model-rotate-benchmark.log`。約9秒間の測定結果であり、全frameや他GPUの性能保証ではありません。

実YUMEKAの最終6姿勢はDebug/ReleaseともGPU readbackに成功し、各姿勢45,233位置・55,747法線で位置差0・法線最大差約0.000000179を確認しました。ログは`final-yumeka-{debug,release}-capture.log`です。

## Mixamoの太もも・すねの対応漏れ

足の動きの不具合報告を受け、実motionの骨名と対応表を確認しました。`mixamorig:LeftUpLeg`・`RightUpLeg`が正規化後の`upleg`に対応せず、親の役割を必要とする`LeftLeg`・`RightLeg`も未割当でした。足首・つま先だけが対応し、太もも・すねはtargetの初期姿勢に残っていました。従来のGPU/CPU一致と47本の件数確認では、この欠落を見逃しています。

左右のUpLeg→Leg→Foot→ToeBaseと腰の実際の階層を使う回帰テストを先に追加しました。DebugのREDは`mixamorig:LeftUpLeg: expected=15 actual=0`。`upleg`を太ももの別名へ加えた後、CPU Debug53/53件（5.72秒）、Release53/53件（3.80秒）が成功しました。ログは`mixamo-legs-red-{build,tests}.log`、`mixamo-legs-cpu-{debug,release}-{build,tests}.log`です。

実モデルcaptureの`GKCORE_TEST_REQUIRE_HUMANOID_LEGS=1`は、targetとmotionの8脚役割・source番号・role一致を描画前に検査します。両Mixamo motionは52role、YUMEKAは53role、対応51本となり、単独2種類・ブレンドの各6姿勢をDebug/Releaseで確認しました。18画像は構成間で全画素一致、位置最大差0、法線最大差約0.000000179です。修正後の通常Release Runtime・1280×720・300 warmup /3,000frameブレンドは324.762FPS、p95 3.497ms、DrawModel0.696ms、Present2.376msで完走しました。ログは`mixamo-legs-benchmark.log`です。全frameの300FPS保証ではありません。

native関連9/9件もRelease95.15秒、Debug108.19秒で成功しました。対応推定・binding・API・rig API・animation画像・BLEND画像・packageを含みます。全78件のGPU変形追加時点の結果と、今回の関連検査を区別して記録します。ログは`mixamo-legs-native-{debug,release}-tests.log`です。native CPU契約のtargetも最新sourceで再ビルドして再検査しています。

## Unrealモデルの外部アニメーション

Sci-Fi TrooperとClown Monsterのuassetを検証用プロジェクトへコピーし、Unreal Engine 5.8.3でFBXと基本色PNG、Idle/Walkを出力しました。spine_01とneck_01の役割未割当を回帰テストで先にREDにし、背骨・首・指のゼロ埋め名・ballへ対応した後GREEN。CPU Debug/Release53/53件（5.58秒/3.56秒）が成功しました。元motionとMixamoブレンドを両モデル・両構成で各6姿勢、計24画像の構成間一致と8脚役割を確認しました。全位置・法線のGPU/CPU比較は位置差0・法線最大差約1.79e-7で成功しています。

mainはゲーム構築用のファイルだけを公開する運用へ変更しました。testsはdevへ残し、allowlistから作るtools/prepare_game_repository.pyで公開構成を生成します。CMakeのtest/capture/benchmark target、開発用shader reflection実行、検証記録と私有assetは公開側に含めません。setupはtestのない構成でCTestを実行せず、カスタム効果サンプルのshaderもexamplesのHLSLから生成します。

Unreal由来のモーションを別人型へ移す確認は、ClownMonsterWalk→TrooperとSciFITrooperIdle→Clownの両方向・両構成で各6姿勢が成功しました。人型役割52、名前での補助骨を含む対応68、脚8役割とGPU/CPU参照一致を確認。Mixamo専用の経路ではありません。

公開用構成はtestsのない300ファイルからReleaseのRuntimeと5サンプルをビルドし、installしたSDKだけを使う別アプリのInit・描画・Present・Shutdownに成功しました。ログはgame-main-{config,build,install}.log、game-main-consumer-{config,build,run}.logです。作業コピー全体の削除と個別削除は自動承認レビューに拒否されたため、古い作業コピーを消す方式は止め、公開用の新しいGit treeを作る方式へ変更しました。元のdevと履歴は保持します。

任意の異なる骨名と、異なる初期回転・骨長を持つ5ボーンの汎用人型binding契約も追加しました。FBX source/GLB targetという形式指定で、人型役割による対応、期待回転、骨長保持と三角関数から独立に求めた足位置を確認し、Debug/Releaseとも成功。新しい公開構成契約を含むCPU全体はDebug54/54件（4.73秒）、Release54/54件（3.99秒）です。起動メニュー6項目のファイル・材質・両motionもCheckOnlyで通過しました。

## 人型ボーンの対応ファイル

自動判定できない骨名にも対応を保存して再利用するため、SetModelHumanoidBoneMap・SetAnimationHumanoidBoneMapを追加しました。UTF-8対応表はRoleName/tab/骨名の全置換です。候補を完成してから反映し、失敗時・適用済みclip・別instanceを変更しません。parserを未実装stubとして契約REDを確認し、実装でGREEN。公開APIもstubでRED、正規実装でGREENにしました。viewerの引数では除去後のargv終端と、次optionを値として消費しない条件を先にREDとして修正しています。

CPU Debug56/56件（4.47秒）、Release56/56件（5.36秒）成功。実モデルはCesium Man GLBの19役割を骨階層と関節位置から明示し、GLB motion→Trooper FBX、Trooper Idle FBX→Cesium GLB、Cesium GLBとClown Walk FBXの混在ブレンド→Clown FBXを各6姿勢でDebug/Release確認しました。8脚役割は全例で対応。FBX targetはGPU readbackで位置差0・法線最大差約1.79e-7、GLB targetは既存のCPU skinningを描画しています。GLB側でGPU-only readbackを要求した初回はdispatchなしで失敗したため、検証設定を正しく切り替えました。GPU処理が行われたという誤った記録にはしません。ログはhumanoid-map-*とhumanoid-profile-{debug,release}-*.log、画像はbuild/real-model-captures/{Debug,Release}/humanoid-profilesです。

対応表の追加後も、公開用のtestsなし305ファイルからRuntimeと5サンプルをビルドし、install済みSDKだけを使うconsumerのGPU起動・描画・終了が成功しました。関連ログはhumanoid-map-game-{config,build,install}.logとhumanoid-map-game-consumer-*.logです。3組の各6画像もDebug/Releaseで全画素一致しています。

## 補助骨を含む腰移動の体格補正

腰のlocal rest位置だけから倍率を求める旧方式では、腰の高さを親の補助骨へ移してlocal位置を0にすると倍率1のままになりました。補助骨あり/なしの同じ体格を用意し、source脚0.5+0.5、target脚1+1、source腰移動0.25に対してtarget移動0.5になる独立契約を先にREDにしました。完全に役割対応した脚のrest model原点間距離から倍率をbind時に保持し、cacheなし評価も同じ計算へ修正しました。片脚しかない場合はその脚を使い、完全な役割対応がない場合は従来の腰位置比へ戻します。

修正後は補助骨の有無、target骨長の維持、cache/fallback一致が成功。追加した試験のfoot期待値も、親高さ2+腰移動0.5-すね長1-足首長1=0.5という独立式へ訂正しました。初回の2という期待値は試験側の誤りでした。CPU Debug56/56（5.59秒）、Release56/56（4.43秒）が成功しています。ログはhips-body-scale-{red,green}-*とhips-scale-cpu-{debug,release}-*です。

実モデルはYUMEKAの外部ブレンド、GLB→FBX、FBX→GLB、GLB/FBX混在ブレンドの4組を各6姿勢、Debug/Releaseで確認しました。24画像は構成間で全画素一致。FBX targetの全位置・法線readbackも位置差0・法線最大差約1.79e-7で通過しています。公開用testsなし構成のRuntime・5サンプルのビルド、install済みSDKのGPU consumer起動・描画・終了も成功。ログはhips-scale-{debug,release}-*.log、hips-scale-game-{config,build,install,consumer-run}.logです。

## 登録済みアニメーションの対応診断

未対応の人型役割を見逃さないため、GetModelAnimationMappingInfo・GetModelAnimationMissingHumanoidRoleと公開FModelAnimationMappingInfoを追加しました。bind時のrequested roleを保存し、設定変更・source handle解放後も診断が同じ値になるようにしました。件数はbone/morphを区別し、名前だけの骨対応と同じ人型役割での対応も区別します。self/identity再生は設定した人型役割を対応済みとして数えます。

公開APIのstubで契約REDを記録してから、全対応・部分対応・名前fallback・identity・枠独立・再bind/Stop・無効照会の出力維持をGREENへ修正しました。試験側の空profile成功という誤った前提は、既存契約どおりSetModelBoneRole(None)での解除に修正。primary再bindでsecondaryが解除される条件、全一致なし再bindの失敗・既存状態保持も試験に合わせています。CPU Debug56/56（5.18秒）、Release56/56（5.45秒）が成功しました。

viewerは登録直後に診断を一度取得して表示文字列を保持し、毎frameの骨走査は増やしません。起動時に不足役割を表示します。UI追加の括弧不足をnativeビルドで修正し、日本語文字列はUTF-8で明示しました。

実モデルの完全対応52/52、GLB由来の部分対応19/52、腰だけ1/52を各6姿勢でDebug/Release確認し、18画像が構成間一致しました。未対応役割の照会と全位置・法線のGPU/CPU一致も成功。testsなし308ファイルの公開構成をビルドし、新headerを含むinstall済みSDKと既存consumerのGPU起動・描画・終了が通りました。関連ログはmapping-report-{debug,release}-*.log、mapping-report-game-*.logです。

## 人型役割を使うIK指定

SetModelHumanoidTwoBoneIkを追加し、現在のinstanceの役割から3骨を解決して既存IKへ渡します。未設定・None・重複・範囲外の役割、無効handle・非有限入力・weight範囲外・非連続階層は失敗し、直前のIK命令を保ちます。登録後は骨番号を保持し、役割を解除しても登録済みIKは変わりません。viewerと実モデルcaptureのIK/chainも役割対応を使い、--model-bones指定または自動推定で解決します。

API宣言前にrig API buildがC2039/C3861で失敗するREDをworkerが確認しました。この初回の出力はログファイルへ保存していません。初回GREEN検査は、試験側で前frameのPresent前にBeginFrameを呼んだため失敗し、frame終了の順序を修正しました。役割交換も既存の一意制約に合わせてNoneへ解除してから行います。骨名joint_a/b/cを持つ2instanceを異なる役割で設定し、独立した期待端点(1,1,0)、役割変更後の命令保持、削除済みhandle等を確認しました。

Trooperの実IKは前腕scale=[0.999998987,1,1]が旧1e-6相対判定をわずかに超えて拒否されました。既存inspectへrestScaleを追加して実値を記録し、その値とroot+middle累積の回帰を先にREDにしました。正のほぼ一様scaleだけを16*FLT_EPSILON相対差まで許容し、全3solverの端点・節長誤差2e-5以内と全行列成分の有限性を検査します。差8e-6・2倍・ゼロ・負のscaleは拒否します。CCDの検査はfloat計算より細かい1e-7停止条件で収束しなかったため1e-6にし、64回でも未収束だったため既存上限256回で確認しました。公開のchain設定やsolver実装の反復回数は変えていません。ログはhumanoid-ik-scale-red-*、humanoid-ik-scale-green-*、最終のhumanoid-ik-geometry-{debug,release}-build.logです。

CPU全56件はDebug4.71秒、Release4.43秒で成功しました。全行列検査を追加した後のfocused IKも両構成で成功。実モデルはSci-Fi Trooper、Clown Monster、Cesium Man、YUMEKAのstatic/IK/chainをDebug/Releaseで計24回実行し、12画像ペアが全画素一致、タイトル以外のモデル領域もIK/chainで変わりました。FBX3モデルは全位置・法線のGPU readbackで位置差0、法線最大差約1.19e-7、許容0.0005で成功。GLBは既存CPU skinningで描画し、GPU-only readbackは要求していません。実行環境はWindows 11 build26200、RTX4070 SUPER driver617.42、MSVC19.29/v14214.29、SDK10.0.22621.0です。commandはgkcore_real_model_capture_tests <model> <scale> <centerX> <centerY> <centerZ> <static|ik|chain>に材質/役割表optionを追加、GKCORE_TEST_CAPTURE_PATH・GKCORE_TEST_CAPTURE_FRAMES=1・FBXのIK/chainでGKCORE_VERIFY_GPU_SKINNING=1を指定しました。capture専用buildは非表示windowです。関連ログはhumanoid-ik-{debug,release}-<model>-<mode>.log、比較結果はhumanoid-ik-image-comparison.jsonです。

公開用308ファイルの構成からRuntimeと5サンプルのRelease build、SDK install、新API宣言のinstall確認、install済みDLL/shaderを使う既存consumerのGPU起動・描画・終了に成功しました。CMake初回はPowerShellの未引用バージョン引数が分割されてSDK選択に失敗し、引数を引用して同じbuildへ再configureしました。ログはhumanoid-ik-game-{config,build,install,consumer-run}.logです。tests・開発記録・私有モデルはmainへ含めません。

## 腕IKの目標位置と見た目の確認

ユーザーの「ピエロ以外の手が埋まっている」指摘を受け、前回の画像を見直しました。旧sampleはmodel中心から+Xへ一定距離という目標を使い、右肩が-X側にあるモデルでは胴体を横切る動きを要求していました。GPU/CPU一致と画像の構成間一致を、見た目の合格に使ったのは不十分です。以前のhumanoid-ik画像は、貫通のない姿勢の基準画像としては扱いません。

現在の骨原点を描画と共通のclip/blend/IK評価で取得するGetModelBonePositionを追加しました。モデル空間で返し、表示SRTを含めず時刻を進めず、失敗時はoutputを保持します。API未宣言のcompile REDをbuild/dev-windows/model_animation_rig_api_red.logへ保存し、初期位置・ブレンド・IK到達点・描画頂点との一致・時刻と命令の保持・無効照会を確認しました。

FModelArmIkPreviewは肩と胴体のXZ方向、上腕と前腕の長さから肩より外側の同じ目標を両IK modeへ作ります。到達不能な極端な節長比と非有限入力は拒否します。目標をオレンジの中抜きマーカーとしてUIへ表示し、modelとマーカーは表示中心を軸に同じ変換で回転します。線だけの3D三角形がrenderer未対応であることを実装確認で見つけ、8枚のfilled triangleへ変更しました。pure geometryの試験ではMakeがbone番号を推定するという誤った期待を削除し、Buildだけが番号を割り当てる契約に合わせています。

実Cesium GLBで、UpperChest未設定でもChestがあるのにfallbackが失敗しました。役割が未登録と検索失敗を混同していたため、4骨のChest->腕fixtureを先にREDにし、正常な未登録を区別してGREENへ修正。ログはik-target-torso-fallback-{red,green}*です。UI markerの8 packet・filled flag・world変換もCPUで独立に検査します。

最終CPU56件はDebug4.49秒、Release4.20秒で成功。実モデル4種のReleaseはstatic正面とIK/chainの正面・45度・90度、Debugはstatic/IK/chain正面、合計40回が成功しました。正面12画像ペアが全画素一致。32姿勢照会で手首到達・骨長・肩より外側の関節位置を確認し、chainの最大到達誤差4.36112567e-5、2ボーンは約1.19e-7以下です。FBX3種の24 GPU readbackでは位置差0、法線最大差1.1920929e-7。GLBは既存CPU skinningの描画で、GPU-only検証は要求していません。追加のYUMEKA 2560x1920拡大capture3方向も同じ到達確認とGPU readbackで成功しました。

画像の別レビューでも、旧画像のような手を胴体中央へ引き込む表示は4モデルの3方向で見られませんでした。YUMEKAの拡大像では、正面で大きな袖に手首が隠れ、斜め・側面で指が袖口から出ていることを確認しました。体・衣服・髪・持ち物のmesh交差と動作中の貫通は検査していません。IKに衝突回避や関節角度制限を追加したわけではありません。

実行環境はWindows11 build26200、RTX4070 SUPER driver617.42、v142/MSVC19.29、SDK10.0.22621.0。gkcore_real_model_capture_testsの既存model/scale/center/modeと材質/役割表引数、GKCORE_TEST_VIEW_ROTATION=0/0.7853981634/1.5707963268を指定。FBXのIK/chainはGKCORE_VERIFY_GPU_SKINNING=1、拡大像はGKCORE_TEST_CAPTURE_HIGH_RES=1。capture専用windowは非表示です。画像はbuild/real-model-captures/{Release,Debug}/ik-outside-body、数値はik-target-comparison.json、ログはik-target-*です。

公開用310ファイルの構成からRuntime・5sampleをbuild/installし、新API宣言のinstallと既存consumerのGPU起動・描画・終了を確認しました。fallback修正後のhelperは公開用sourceへ同期し、再build/installしています。tests・検証記録・私有モデルはmainへ含めません。関連ログはik-target-final-game-*、ik-target-game-consumer-run.logです。

## 動作中の腕IKと袖の変形の確認

ユーザーの腕が潰れて見える指摘を受け、同じ主・副モーション時刻1.0409090909秒でIKなし、従来の固定目標、修正後を比較しました。動く腕を肩基準の一定形状へ強く移すsampleの目標が、元の腕振りと肘の曲げ方向を壊していました。external-ik / external-blend-ikは元の手首から胴体の前方へ腕長の最大5%だけ補正し、到達範囲へ制限します。poleは元の曲げ面から作り、直線姿勢では胴体の基底へ退避します。前方と腕が平行な有効姿勢を拒否する契約をREDにしてから、横・縦方向の第2fallbackを追加してGREENにしました。

GetModelBonePositionsはclip/blend/IKと骨原点の評価を一度ずつ行い、候補配列の全件成功後にcaller出力を一括更新します。重複番号、全失敗出力保持、時刻と表示SRTの不変、各clipの評価回数、IK後の座標をCPU契約で確認しました。単体照会も同じ処理へ委譲します。sampleは毎frame、時刻更新→IK解除→現在位置取得→IK設定→描画の順で、前frameの補正を目標へ戻しません。

試したrootの曲げ面回転補正は、一般のpole変更で肘角を保つ契約を満たした一方、従来のYUMEKA静止IKに肩の裂けを追加しました。ほぼ直線の法線閾値前後でも約55.8度の回転差を観測したため、core補正と専用契約は採用せず元のsolverへ戻しました。最終静止IKの2560x1920画像は前commitの全画素と一致しました。新しいsource-goal/source-poleの3D任意軸回転・平行移動・1.5/0.7不均衡chainでは全3関節のlocal quaternionが保持されます。未利用の固定goal helperとUnitypackageのstdin解析分岐も除去しました。twist補助骨のweight調査だけを開発用inspectorに残しています。

最終CPUはDebug/Release各56件成功（4.87秒/4.96秒）。実モデルはYUMEKA+2motion blend、Trooper+Idle、Clown+Walk、Cesium GLB+Capoeiraを各12姿勢・両構成で描画し、96frameすべてで手先到達・骨長・pole側と5%補正範囲を確認しました。48画像ペアはDebug/Releaseで全画素一致。FBXの72frameはGPU位置・法線readback成功、CesiumはCPU skinningの描画を確認しています。YUMEKAは同時刻の正面・側面拡大像と、3.35秒開始・1/60秒刻みの120frame連続再生でも成功し、loop境界付近の16枚を保存しました。新目標で元の腕振りと袖口の手が残ることを画像で確認しました。大目標の肩品質、関節制限、twist補助骨の自動回転配分、体や衣服の衝突回避は未対応です。

環境はWindows11 build26200、RTX4070 SUPER driver617.42、Runtime v142/MSVC19.29・SDK10.0.22621.0。CPUはMSVC19.51。cmake --build build/dev-windows --config Debug/Release、ctest --test-dir build/dev-windows -C Debug/Release --output-on-failureを実行しました。nativeはgkcore_real_model_capture_testsへmodel/scale/center/external-ikまたはexternal-blend-ik、motion pathsと材質または役割表を渡し、GKCORE_VERIFY_GPU_SKINNING=1（GLBは0）を使用しました。capture専用windowは非表示。記録はbuild/native-validation/arm-final-*とarm-quality-motion-following*、画像はbuild/real-model-captures/{Release,Debug}/motion-followingとRelease/arm-qualityです。

通常Runtimeの表示あり1280x720、GPU検証OFF、warmup300・計測3000frameの最新sampleは269.801FPS、p95 4.038ms、DrawModel平均0.825ms、Present平均2.307msでした。300FPSの要求はこのmodeでは未達です。START.batの7番で修正後のYUMEKA blend+IKを起動でき、CheckOnlyによる必要file/引数検査も成功しました。未計測GPUや全骨格の自然さは保証しません。

ゲーム構築用310fileのallowlistは変えず、Runtime・5sampleを既存の配布検証buildでRelease build/installしました。インストール先の新API宣言を確認し、install済みDLL/shaderを既存consumerへ渡したGPUのInit→Present→Shutdownも成功しました。mainへtests・検証tool・私有asset・開発記録は含めません。

## 骨番号の事前解決、新しいモーションと追従表示

動くIK sampleの腕3本・腰・胸・反対肩を起動時に解決して保存し、毎frameの役割検索を省きました。役割表を変更しないsampleが所有する6番号で、一般呼出の2引数Buildは従来の再検索を維持します。Resolve失敗時の配列保持、無効番号とnullでのpreview保持、役割の解除/再設定で再Resolveが必要なこと、clipと時刻への追随をTDDで確認しました。公開宣言未追加のREDを先に取得しています。Clear→現在位置照会→同じbone番号でIK設定→Drawの順と、すべてのsolver検査は維持しました。従来YUMEKA blend+IKの12姿勢は全画素一致しています。

途中でユーザーが公開header4個を名称変更しました。新名へ合わせる明示回答を受け、型名は変えずHumanoidBoneTypes.h、ModelAlphaModeTypes.h、ModelAnimationMappingInfo.h、ModelMaterialSettings.hへinclude・guard・install・配布allowlistを更新しました。参照未更新のnative build failureを記録し、更新後のRuntimeとsample buildを確認しました。既存のaggregate API headerから各型を引き続き使えます。

新たなユーザー提供Skinning Test.fbxとSwinging.fbxをanimation loaderで読み、各66骨・1 clip、2.25秒/2.43333333秒を確認しました。通常model inspectはsupported static trianglesなしで失敗するため、motionとして使っています。YUMEKAへ単独6姿勢ずつ、blend+IK12姿勢、TrooperへSkinning Test+IK12姿勢、ClownへSwinging+IK12姿勢を描画しました。人型対応はYUMEKA各51/53（左右の目不足）、TrooperとClown各52/52です。全48frameのGPU位置・法線readbackと36frameのIK到達・骨長・補正量の検査が成功しました。検査素材はDownloadsを読み取りだけで使い、main/SDKへ含めません。

Swingingは腰が大きく移動し、固定cameraではframe2が画面外に出ました。sample所有のFModelMotionViewをpaired header/cppへ追加し、--follow-motionで初期の腰からの差を表示中心へ加えます。clipの位置・回転・IK目標はそのままです。骨役割設定後・再生前に初期化し、同じ中心をモデル変換とtarget markerへ渡します。GetCenterの表示SRTからの独立、Init失敗時の状態保持、モデル解放と大きな有限差分での出力保持、flag除去/重複/材質path値をCPU契約で確認しました。小さな差分をFLT_MAXへ足す誤ったoverflow期待は丸めで消えるため、大きな有限差分で検査しています。

追従を指定して同じ5case・48frameを再描画し、モデル領域の画素は全frameで存在しました（最小3533画素）。元の腕振り、頭上へ伸ばす腕、体の回転と両手の動きを画像で確認しました。衣服の貫通や全任意bone rigの変形品質は合格条件に含めていません。STARTの8/9は各単独motion、10は2motion blend+右腕IKで、追従はこの3項目だけに指定します。ファイルがない環境ではCheckOnlyの新項目だけを省略します。CheckOnlyは成功しました。

最終CPUはDebug/Release各56件成功（7.95秒/7.60秒）。nativeはWindows11 build26200、RTX4070 SUPER driver617.42、v142/MSVC19.29・SDK10.0.22621.0。gkcore_real_model_capture_testsへmodel/scale/center、externalまたはexternal-blend-ik/external-ik、motionと--follow-motionと材質を渡し、GKCORE_VERIFY_GPU_SKINNING=1で検査。captureは非表示windowを使用しました。成果物はbuild/real-model-captures/Release/new-motions-follow、数値/画像比較/解析はbuild/native-validation/new-motion-*とik-binding-image-regression.*です。

通常Runtimeの可視benchmark・1280x720・検証OFF・3000/warmup300は今回210.209FPS、p95 5.588msでした。DrawModel平均1.100ms、Present平均3.059ms。変更前を同時刻で取ろうとしたrunはwindowが途中で閉じられ完了せず、因果的な速度改善は判定できません。300FPS要求は引き続き未達です。骨番号検索の削減をwall FPS改善と取り違えず、計測と姿勢評価/描画の重複処理を次に調べます。

ゲーム構築用treeは新しいsample補助2fileを含む312fileで、Runtimeと5sampleをRelease buildしました。新header名を含む52fileのRuntime SDK配布allowlistと、install済みDLL/shaderを使うconsumerのGPU Init→Present→Shutdownも成功。旧SDK内の旧名4headerを消す操作は自動承認reviewがポリシーで拒否したため実行していません。旧SDKを残し、build/motion-view-sdkへ新規installして旧名を混ぜない配布物を検証しました。tests・検証tool・私有asset・開発記録はmainへ含めません。

## 2026-10-11 アニメーションに連動する揺れもの

- 要求: 髪・スカート・アクセサリーをアニメーションと連動して揺らす。model instanceのPlaybackが各鎖のbone列・設定・節位置/速度・確定回転を所有し、clip/blend/IKの後に明示更新する。
- RED: solver stub、姿勢の方向合わせstub、祖先回転の重複積算budget、実モデルと同じ小軸差のfixture、180度反転の中間ゼロ長をそれぞれ検出。既存pose検査の正規化回数と339骨IKの余分な積算も検出した。
- GREEN: ばね・減衰・重力・風、骨長/曲げ角、仮想末端、teleport/0.25秒以上の停止/中間ゼロ長時のリセットを実装。30/60/300FPSの収束、反復照会の無更新、失敗原子性、instance分離、IK後の姿勢、CPU/GPU予約とCPU fallbackの凍結を検査した。
- Windows CPU MSVC19.51: Debug/Release各57件成功。native v142/MSVC19.29のDebug関連4件も成功。
- Win11 build26200 / RTX4070SUPER driver617.42 / v142 / SDK10.0.22621.0: YUMEKA＋Silly Dancing/Capoeira blend＋右腕IK＋13鎖を各構成120frame実行。GPU変形validationはDebug/Release各120件valid=true。保存12画像は構成間でbyte一致し、祖先積算変更前後の12画像もbyte一致。髪・衣服・尻尾の変化を目視し、身体との接触品質は未判定。
- SDK Runtime54ファイルがallowlistに一致。配布header/DLLの4 API exportを外部consumerから呼び、GPU起動からPresent/Shutdownまで成功。再利用したconsumer出力の古いgkcore.dllでentrypoint未検出が一度発生したため、現SDKのDLLへ更新して成功を確認した。
- 計測: 1280x720通常Release、VSyncなし、標準効果、3000frame/warmup300。最新188.150FPS、p95 5.610ms、揺れ更新1.045ms。300FPS、身体sphere/capsule、スカート隣接制約、world SRT慣性、近接時の品質は未完。
- 最新記録: build/native-validation/secondary-{full-debug,full-release}-test.log、secondary-final-{release,debug}-capture.log、secondary-motion-final-benchmark.log、secondary-sdk-consumer-run.log。STARTの11へ設定を追加し、CheckOnlyで起動pathを確認した。

## 2026-10-11 揺れものの身体接触

身体boneへlocal球・カプセルを最大64個設定するFModelSecondaryMotionColliderとSetModelSecondaryMotionCollidersを追加しました。古いFModelSecondaryMotionSettingsのlayoutは変えません。instance所有の設定・前回形状を候補状態で全置換し、count0は鎖を残して接触だけ解除します。clip/blend/IK後の身体を小刻み時刻で補間し、節と線分を離散検査します。root内部の最初の線分だけを免除し、自由節は常に検査します。Resetは元poseへ戻し、次のUpdateで接触を解きます。

REDから修正した問題は、球中心で線分方向へ押していた退化normal、pointの表面吸着によるspan押出との循環、長capsuleを親からの最近球だけで避ける誤判定、骨長・角度の復元中に一時侵入した親節で反復を止める処理です。実frameのroot44/shape5/span3をCPU fixtureへ移し、root例外・短/長capsule・複数重複・不可能配置・float境界を確認しました。長capsule側面は軸平行成分を保って直交面の接線方向を解きます。

全拘束後・float保存後・実際の回転から再構成したFK位置を検算します。最後のFK境界で途中終了した605/127frameのbenchmarkは完走結果へ数えません。早期終了だけの導入前後は48画像がbyte一致しました。最終実装は許容幅へ頼らず解けたときに反復を止め、solver内だけ半径へ0.1%の数値余白を加えてFK丸め差に備えます。登録された半径とmarginは保持します。失敗は診断を返し、前回状態を保ちます。

Previewは--secondary-collidersとSTART12を追加しました。鎖設定の既存9数値形式は既定8回を保ち、任意10個目で反復数1..32を指定します。YUMEKAは髪/尻尾8回、スカート32回、身体8形状。60度境界で8回が未収束だったloopは32回で完走しました。身体形状は初期の概算です。2560×1920で脚のprofileを広げた試行も行いましたが、一部スカートの埋まりは解消しませんでした。mesh面の接触・隣接鎖・自己衝突は未実装で、骨の非侵入とcloth品質を区別します。

Windows11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK10.0.22621.0で、0秒と3.35秒開始の各120frameをDebug/Releaseで再生しました。最終480frameのGPU位置・法線validationはすべてvalid=true、24画像ペアがbyte一致。接触なしの従来13鎖は以前の12画像とbyte一致しました。CPU MSVC19.51はDebug/Release各58/58件成功（4.62秒/4.33秒）。

commandはcmake --build build/dev-windows --config Debug/Release、ctest --test-dir build/dev-windows -C Debug/Release --output-on-failure。nativeはgkcore_real_model_capture_tests <YUMEKA FBX> 1.1166145684 0 .671678712 -.2517482195 external-blend-ik <Silly Dancing.fbx> <Capoeira.fbx> --follow-motion --secondary-motion samples/config/yumeka-secondary-contact-motion.txt --secondary-colliders samples/config/yumeka-secondary-colliders.txt --materials <material-config.txt>。GKCORE_TEST_ANIMATION_FRAMES=120、START_SECONDS=0/3.35、STEP_SECONDS=1/60、CAPTURE_START_FRAME=108、CAPTURE_FRAMES=12、GKCORE_VERIFY_GPU_SKINNING=1を指定し非表示windowを使いました。

通常Release 1280×720、VSyncなし、標準効果、3000frame/warmup300は、全反復の初回98.748FPS（更新5.883ms）から最終172.024FPS（p95 6.400ms、更新1.670ms）へ改善。最終3000frameは完走しました。300FPSは未達です。記録はbuild/native-validation/secondary-contact-{full-debug,full-release}-test.log、secondary-contact-{Release,Debug}-{start,loop}-capture.log、secondary-contact-comparison.json、secondary-contact-final-benchmark.log。拡大比較はbuild/real-model-captures/Release/secondary-contact/quality-comparison.pngにあります。

ゲーム構築用333ファイルのtreeをprepare_game_repository.pyのallowlist・変換処理で既存stagingへ更新しました。tests・開発記録・私有assetは除外し、同treeからRuntimeと5sampleをRelease buildしました。installしたRuntime SDK55ファイルがallowlistと一致し、新APIを別consumerからリンク・呼び出してGPUのInit→Present→Shutdownが成功しました。ログはsecondary-contact-game-{build,install,consumer-build,consumer-run}.logです。生成した試行configと旧first capture12枚はworkspace内の生成物と確認して削除し、現build・最新画像・必要な失敗記録を残しました。

## 2026-10-11 揺れものの姿勢計算を共有

独立した鎖をFModelPoseChainの借用配列でまとめ、OrientModelPoseChainsが同じ基準FKとpose複製を各1回だけ行います。単一鎖APIは同じkernelを使います。共有親は許可し、同じ根・祖先子孫の重なり、空入力、非finite、無効な別枝は候補側で拒否します。長さ・角度・接触solverや公開ABIは変更しません。

REDは新header/API未実装のbuild失敗として取得しました（secondary-batch-red-build.log）。339骨の2枝fixtureで解析的joint/tip到達、未指定の位置・scale・morph・回転保持、単一鎖結果との一致、source/output alias、失敗時の出力保持を確認しています。FK entry counterはGKCORE_TESTINGだけに含め、一括orient1回、Runtimeの2鎖更新はcontact無し2回/有り3回を契約化しました。13鎖の従来更新は基準1+各鎖13+接触1の15回で、現在は1+1+1の3回です。更新用の点配列は先に全数Reserveし、全鎖のorient完了まで参照先を移動させません。

CPU Debug/Release全58件成功。Win11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK22621.0で、Silly Dancing/Capoeira blend+右腕IK+YUMEKA13鎖/8身体形状の0秒と3.35秒開始各120frameをDebug/Releaseで再生しました。GPU位置/法線検査は480frameすべてvalid=true、保存した48画像が前commitのbyteと一致。command/環境変数は身体接触節のものと同じです。ログはsecondary-batch-{full-debug,full-release}-test.log、secondary-batch-{Release,Debug}-{start,loop}-capture.log、secondary-batch-image-comparison.jsonです。

通常Release/1280×720/標準効果/VSync無し、3000frame/warmup300は188.537FPS、p95 5.827ms、揺れ更新1.199ms。前回は172.024FPS、更新1.670msです。300FPSは未達。記録はsecondary-batch-benchmark.log。

スカートのFBX clusterを読み取り、Circle.051の3039 control points中443がSkirt weightを持ち、その全てに登録骨のweightがあることを確認しました。登録40骨中非ゼロweightは19骨（10root+9 first child）で21骨はweightゼロ。443点中336（75.85%）はHips/Spine/胸との混合、Skirt合計weightが50%超の点は221でした。骨線からの横幅目安はp50 .037832/p95 .076814/max .090467 model unitですが、raw座標変換による概算で接触の厳密検査には使用しません。深い骨を動かしてもcloth meshへ効かないことと、幅・複数骨blendが点/線分contactの外にあることを分けます。

実weight骨まで鎖を短縮し、次の未weight子のrestLocalをvirtual tipにしたtrialも、3.35秒開始120frameで完走しましたが、2560×1920の比較像で埋まりを解消しませんでした。正式presetは変更せず、trial configは使用後に削除します。最新比較像はquality-skin-bones-comparison.png、ログはsecondary-batch-skin-bones-capture.log。衣服の面または実変形点を検査する経路が残作業で、骨の接触成立をclothの合格に置き換えません。

prepare_game_repository.pyのallowlist/変換処理で既存公開treeを334ファイルへ更新し、Runtimeと5sampleのRelease buildが成功しました。Runtime SDK55ファイルがallowlistに一致し、別consumerのGPU Init→Present→Shutdownも成功。tests・開発記録・私有assetはmainへ含めません。記録はsecondary-batch-game-{build,install,consumer-build,consumer-run}.logです。公開ABIとSDK header数は変更していません。


## 2026-10-11 基準姿勢の再利用と実skin点の診断

UpdateModelSecondaryMotionとResetの成功時に、clip・blend・IKを反映し揺れを重ねる前のposeをinstance所有cacheへ保存します。Draw/queryは独立したposeへ複製し、その時点のsecondary回転だけを重ねます。同じ再生条件での反復更新もbaseを再sampleせず、指定dtでphysicsを進めます。model/clip sourceの識別、revision、slotの時刻・speed・loop、blendとIKの値を照合し、cache内poseの数とfinite値も検査します。binding、再生条件、IK、model bone roleの成功した変更はrevisionを進めます。番号上限では旧cacheを破棄し1へ戻します。asset役割変更は既存bindingを変えず、secondary設定変更や表示SRTもbaseの入力を変えません。

更新用cacheはsimulationのCommitより先に候補を作り、確定後に旧cacheを交換します。失敗したsetter/updateは旧状態を保ちます。SetModelAnimationLoopも時計を先に検査し、失敗時にloopを変更しません。REDはcache未実装とskin点stubで契約失敗を取得しました（skin-cache-red-test.log）。成功後のquery/multiple drawのsample数0、予約描画の独立、time/speed/loop/blend/IK/roles/Stop後のmiss、失敗後のhit、source相違・NaN・数破損のfallback、番号上限、別instance所有を確認しています。最終fixtureでStop→Play→Draw後の古いcacheをhitと期待して2件失敗しましたが、Drawはcacheを公開しないため、Updateで再公開してから独立性を検査する順へ修正しました。

ModelSkinPointsはGPU skin cacheの行順3×4行列で、指定IDのbind位置へ全influenceをそのまま加算します。weightを再正規化せず、無weight点はsegment末尾のfallback clusterを使います。要求順・重複ID・最大8192点・空入力を扱い、範囲/finite値/行列を検査して失敗時のoutputを保ちます。Runtime内部の補助処理で公開ABIは追加していません。単一/混合weight、affine変換とZ回転、合計0.5のweight、fallback、出力保持を契約化しました。FMatrixの既存説明が列順となっていたため、実際のproducerとshaderが使う行順へコメントを修正しています。

dev専用capture probeは、正weightのbone名prefixに合う点を選び、全weightで評価した位置をFBX sparse referenceと比較します。全対象数と最大8192の上限を出し、上限超では決定的な等間隔ordinalを抽出します。現caseはSkirtの対象1,314点を全て評価します。nonzero morphや未対応sourceは拒否します。JSONのvalidは参照一致だけで、pointPenCountは重複しない侵入点数、pointShapeContactCountは点と形状の組合せ数です。shapeCount/contactTestedを出し、形状がないときcontactClear=nullとします。接触はinstance共通SRT前のmodel空間で、現在の更新が確定した身体球/capsuleとの点距離です。このprobe自体はCPU参照比較であり、実GPUの全位置/法線readbackは別の既存validationで確認します。probe・環境変数・test用exportは通常DLL、main、SDKに含めません。

CPUはMSVC19.51でDebug/Release各59/59件成功（4.57秒/4.80秒）。nativeはWindows11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / Windows SDK10.0.22621.0で、0秒と3.35秒開始をDebug/Release各120frame実行しました。GPU位置/法線は480frame全てvalid=true。保存48画像は前commitとbyte一致、Debug/Release24組もbyte一致しました。commandと既存capture変数は身体接触節と同じで、追加GKCORE_VERIFY_SKIN_POINTS=Skirtを指定しました。非表示windowを使っています。ログはpose-cache-{Release,Debug}-{start,loop}-capture.log、比較はpose-cache-image-contact-comparison.jsonです。

実skin点の最大参照誤差は3.365e-8 model unit。start区間は22〜65点、loop区間は17〜64点が身体形状に侵入し、両区間120frame全てに残ります。最大深さはstart .0393693 / loop .0394331 model unitです。これは設定した概算身体形状への侵入で、実身体meshとの距離ではありません。三角形内部・隣接鎖・布の面接触やcloth補正は未実装です。骨線分の非侵入と布の品質を区別し、次の補正に必要な厳密位置の測定までを今回の範囲とします。

通常Release1280×720、標準効果、VSync無し、3000frame/warmup300は226.342FPS、p95 4.939msで完走しました。DrawModel平均.421ms、Present2.243ms、IK設定.509ms、secondary1.189msです。前回188.537FPS/Draw .852msからの差には計測時負荷も含まれます。重複sampleの削減は契約で確認し、300FPSは引き続き未達です。probe/reference比較を行うcaptureはこの速度計測に含めません。記録はpose-cache-benchmark.logです。

ゲーム構築用339ファイルをprepare_game_repository.pyのallowlist/変換処理で既存公開treeへ更新し、Runtimeと5sampleのRelease buildが成功しました。Runtime SDK55ファイルがallowlistに一致し、別consumerのGPU Init→Present→Shutdownも成功しました。tests・検証tool・開発記録・私有assetはmainへ含めません。記録はpose-cache-game-{build,install,consumer-build,consumer-run}.log、公開treeのhashはpose-cache-game-manifest.jsonです。


## 2026-10-11 変形後の頂点接触

SetModelSecondaryMotionVertexContactsEnabledを既存ModelSecondaryMotion.hへ追加しました。既定OFFで、鎖と身体形状の設定後、現在の対応zero-morph FBX poseだけ有効化できます。未来のclip時刻で未対応morphへ移る場合は描画時に失敗します。登録鎖rootの枝に正weightを持つunique位置だけを選び、全influenceを混ぜたskin位置を球/capsuleの外へ投影します。骨・IK・secondary simulationの位置と速度は書き換えません。布の面、自己衝突、更新間の連続衝突を解く機能ではありません。

候補Aのskin後の点投影と、候補Bの骨回転Jacobianを別agentが検討しました。低いSkirt weightでは骨回転の感度が弱く、角度/骨接触制約と競合しやすいため、描画頂点の限定した補正を先行しています。projectは点ごとに最大64回で全shapeの外を検査し、外点は保持します。中心の退化方向は決定的、複数sphereの対称往復では直交方向へ逃がし、最終検査未達ならoutputを保って失敗します。全shape・XYZ・finite/float範囲を検査します。projectionの数値余白は最小1e-7、半径の1e-6または座標のfloat丸め分です。

REDは未実装stubでsphere projection契約失敗（vertex-contact-red-test.log）。球内外、移動した中心、capsule側面/endcap/退化軸、複数重なり、妥当な1passでも未収束の場合のoutput保持、入力alias、null/count/NaNを検査しました。capsule契約で基点Xを全軸へ使う実装誤りをrootが発見し、XYZ対応へ修正しました。最初の厳密半径期待は数値余白と一致しなかったため、元外点memcmp保持と半径から1e-5以内の外側を区別しました。fixture側のcopy元/先の逆転、非混合三角形の無効cluster参照、Commit後の旧stateを読むUAF、GPU-only opaque位置placeholderへの誤期待も検出・修正しています。

GPUの補正値はFModelDeferredPoseが位置IDとdouble3を所有し、後の設定変更から独立しています。行列bufferへ3record/補正点を追加し、skin→少数点の上書き→面normal→normal groupの順にdispatchします。constant bufferは96byteのまま、内部dispatchは22uint/88byteです。ID昇順・一意性・範囲・overflowを検査し、段階間のUAV barrierを維持します。OFFは補正dispatchを行いません。透明の並べ替えも補正後の位置を使います。

CPU fallbackの一部が元skinだけを再評価していた問題を独立監査で発見し、EvaluateDeferredModelFallbackGeometryをMaterializeとrendererの両経路で共用しました。参照geometryは同じdouble skin位置からtriangle cross、quad cross(c-a,d-b)、Newell、incidence順の和と正規化で法線を再生成します。全normalごとに全cornerを探す二重ループは事前byte maskへ変更し、位置/面/corner/incidence数に比例する計算にしました。body-only点保持、mixed-weightの解析位置、triangle/quad法線、重複/範囲外correctionの失敗保持を確認しています。

選別IDはSecondaryStateがArrayで所有し、元geometryの借用identityを照合します。enableと鎖登録で候補を作り、Update/Reset/身体形状置換は既存選別を複製します。Drawでは全weight選別を繰り返さず8192点ずつ評価し、対象点を間引きません。disableで選別領域を解放し、Clear/Deleteで状態を破棄します。geometry identity相違時のDraw失敗、旧予約不変、別instance、再選別、setter失敗、Reset/Updateの保持を契約化しました。独立監査がUpdate鎖ごとの再選別を指摘したため、検査sourceのgeometry getter回数を0とするREDを取得（vertex-contact-selection-red-test.log）し、設定時だけの選別へ修正しました。

CPU MSVC19.51はDebug/Release各60/60件成功（4.98秒/4.49秒）。parser契約は既存rig executableへ同居させてCoreの重複buildを増やしていません。--secondary-vertex-contactの省略・単独除去・重複atomic・materials path保護を確認し、START 13を追加してCheckOnlyで全path/引数を確認しました。Runtime shader14個を固定FSL/DXCで再compileし、reflection/artifact検査も成功しました。

nativeはWin11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK10.0.22621.0です。YUMEKA＋Silly Dancing/Capoeira blend＋右腕IK＋13鎖＋身体8形状の0秒/3.35秒開始をDebug/Release各120frame実行。GKCORE_VERIFY_GPU_SKINNING=1、GKCORE_VERIFY_SKIN_POINTS=Skirtを指定し非表示windowで検証しました。最終ON480frameでSkirt全1,314点の形状侵入が0、GPU全45,233位置/55,747normalがCPU参照に一致（最大位置誤差0、normal1.789e-7）。Debug/Release24画像組がbyte一致し、選別/形状前計算の最適化前後48画像もbyte一致しました。OFFも480frame実行し、従来48画像のbyteを保持しました。commandは身体接触節と同じで、ONだけ--secondary-vertex-contactを追加します。記録はvertex-contact-{Release,Debug}-{start,loop}-capture.log、vertex-contact-comparison.jsonです。

2560×1920の同じloop姿勢をON/OFF各4画像で比較しました。拡大した衣服では、概算した身体形状と身体meshの差や三角形内部の問題による面の埋まりが残っています。頂点と8形状の成功を、布全体の品質合格とは扱いません。比較はbuild/real-model-captures/Release/secondary-vertex-contact/quality-comparison.png、低解像度4姿勢比較はsecondary-contact/vertex-contact-comparison.pngです。

通常Release1280×720、標準効果、VSyncなし、3000frame/warmup300をON/OFFとも完走しました。最初ON133.568FPS/Draw3.391msから、選別IDと形状軸の前計算後ON171.985FPS、p95 6.373ms、Draw1.811ms、Present2.243ms、IK .509ms、secondary1.196msです。同batch OFF227.082FPS、p95 4.928ms、Draw .405ms、Present2.246ms、IK .510ms、secondary1.186ms。実行時負荷も含まれるため改善率は全環境への保証ではなく、300FPSも未達です。ログはvertex-contact-final-{on,off}-benchmark.logです。

ゲーム構築用344ファイルをprepare_game_repository.pyのallowlist/変換処理で既存treeへ更新し、Runtimeと5sampleのRelease buildを確認しました。installしたRuntime SDK55ファイルがallowlistに一致し、新APIを参照する外部consumerのlinkとGPU Init→Present→Shutdownが成功しました。tests・検証tool・開発記録・私有assetをmainへ含めません。ログはvertex-contact-game-{build,install,consumer-build,consumer-run}.log、tree hashはvertex-contact-game-manifest.jsonです。比較用の2560×1920画像8枚はpixel完全一致のPNGへ保存し、workspace内の今回生成した大きなPPMだけを削除しました。保存対応はvertex-contact-quality-images.jsonに記録しています。


## 2026-10-11 遠方の頂点接触計算を省く

prepared shapeへ端点の各軸min/maxに半径を足した範囲を保存し、double nextafterで上下を各1ULP外側へ広げます。XYZすべてFLT範囲の点がstrictに外なら、最近距離とsqrtを省きます。境界と内側は旧計算を保ち、巨大doubleは従来の距離/overflow経路へ戻します。初回・投影中・pass後・最終strict検査で同じ保守的な除外を使い、shape順、半径、margin、補正式は変更しません。入力と候補outputの原子性を維持します。

REDは4096遠方点×8形状で65,536回の精密距離計算が発生するbudget失敗（skin-contact-bounds-red-test.log）。GREENは同じ点のbyte保持と計算0回。1点だけ球内・残り7形状が遠方のcaseは補正成功と1〜4回のbudgetで、投影内の除外も確認します。移動した球、xyz軸のcapsule、negative斜めcapsule/端点反転、半径の1ULP内外、FLT_MAX端点/半径、DBL_MAX2成分のoverflow原子性を独立距離fixtureで確認しました。testing counterは通常Runtimeに含めません。

capture専用DLLへGKCORE_PROFILE_SKIN_CONTACTSの区間QPC計測を追加し、通常SDKではcompileしません。YUMEKA同じ3.35秒開始120frame、20frame除外後100sample、4,172選別点・平均63.1補正点です。変更前shape .042343 / skin .312558 / contact1.097285 / total1.462256ms、変更後shape .041025 / skin .291392 / contact .163308 / total .505134msでした。RootShape構築とskin位置の式は維持します。記録はskin-contact-bounds-profile-{before,after}.logとprofile-comparison.json。

CPU MSVC19.51のDebug/Release各60/60件成功（5.02秒/4.43秒）。Win11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK22621.0で、既存ON commandの0秒/3.35秒開始をDebug/Release各120frame実行。480frame全GPU位置/法線valid、Skirt全1,314点は8形状に侵入0、変更前後48画像とDebug/Release24組がbyte一致。最大GPU位置誤差0、normal1.789e-7です。記録はskin-contact-bounds-{Release,Debug}-{start,loop}-capture.logとimage-comparison.json。可視benchmark3000frame/warmup300はON199.698FPS、OFF221.465FPSで完走しました。300FPSと布の面品質は未達です。

この作業中のユーザー指示で、primary検証をスカートbone小球と脚boneカプセルへ変更し、可視化して大きさを調整する方針へ更新しました。頂点接触を新規作業の主手段とせず、既定OFFの任意補正として保持します。次の実装はbone側半径の明示と接触形状の可視化、実脚に合わせたprofileです。


## 2026-10-11 骨の小球と接触形状の表示

ユーザー指示に従い、頂点補正をOFFとしてbone小球対身体球・脚カプセルを主検証へ変更しました。鎖ごとの半径APIを追加し、身体半径へ球半径を加えた形状をsolverと描画FKの最終接触検査で共用します。半径0は従来経路を維持。正の加算はfloatの外側へ丸め、FLT_MAX＋微小値のようにdouble加算でも小さい値が消える場合を拒否します。入力形状と失敗時の出力を保ち、alias入力も扱います。既存Settingsの公開layoutは変更しません。

REDは形状拡張を未実装にした実行で、正の球半径を必要とするcontact契約が失敗することを取得しました（bone-sphere-red-test.log）。球・capsule・別鎖半径・半径0のbyte保持・負値/非有限/overflow・Reset/Update/同root再登録/解放を確認。FLT_MAX＋微小値の丸め漏れも検出し修正しました。9/10列のconfig互換を保ち、11列目を球半径とします。config試験のchain配列順の思い込みを修正し、root IDで設定を確認します。

GetModelSecondaryMotionDebugShapesは必要数照会と容量不足/invalid handleの出力保持、現在animation＋IK＋揺れ姿勢のFK、instance独立と照会による時間不変を契約化しました。身体形状は設定margin込み、節球は鎖半径で返します。空配列の0 byte memcpyは独立監査で指摘を受け、件数があるときだけコピーします。可視化helperはmodelと同じscale/center/yawを使い、UIへ輪郭を重ねます。青body・橙節・白root・灰tip。Cで切替、--collision-debugで初期表示し、設定なしのflagはInit前に拒否します。flag重複の引数保持とmaterials値の保護も確認しました。

CPU MSVC19.51のDebug/Release各60/60件成功（4.74秒/4.78秒）。Win11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK22621.0で、YUMEKA＋Silly Dancing/Capoeira blend＋右腕IK＋13鎖、bone半径Skirt .008・髪/尻尾 .003、脚カプセル上部 .045・下部 .035＋margin .005を実行。0秒/3.35秒開始をDebug/Release各120frame、計480frameでGPU位置/法線がCPU参照に一致し、Debug/Release24画像組がbyte一致。最大位置誤差0、normal1.789e-7。旧形状と新形状のdebug overlayも2560×1920、各120frameで確認しました。ログはbone-sphere-{Release,Debug}-{start,loop}-capture.log、bone-sphere-{original,bone}-shapes-capture.log、結果はbone-sphere-image-comparison.jsonです。

通常Release viewer経路の初期表示flagを30frame完走し、START -CheckOnlyで全path/引数を確認。速度計測は表示OFFで3000frame/warmup300、207.044FPS、p95 5.323ms。300FPS未達です。表示画像では身体proxyの適合とスカートの面品質に残件があり、固定rootと最初の線分の既存例外も維持します。球の接触成功だけをcloth全体の品質合格とは扱いません。


ゲーム構築用348ファイルを既存curated treeへ更新し、Runtimeと5sampleのRelease buildが成功しました。installしたSDK56ファイルがallowlistに一致し、新公開APIを使う外部consumerのlinkとGPU Init→Present→Shutdownが成功。tests・検証tool・開発記録・私有assetはmainへ含めません。記録はbone-sphere-game-{build,install,consumer-build,consumer-run}.log、game/SDK manifestです。独立監査とformatter、no-STLテスト、BOM/CRLF、diff checkを確認しました。高解像度8画像はpixel完全一致のPNGへ保存し、workspace内の今回生成した約118MBのPPMだけを削除。保存対応はbone-sphere-quality-images.jsonに記録しました。


## 2026-10-11 スカート球の幅調整と骨接触の重複計算を省く

骨proxyの非侵入と実skin面の非侵入は別です。既記録の443 cloth control point中336点はHips/Spine/Chestとの混合weightで、Skirt合計weightが50%を超えるのは221点、横幅中央値.0378/p95 .0768です。ユーザー指示のbone球対脚capsuleを維持し、vertex補正はOFF。スカート半径.008と中央値へ合わせた.038を同じ3.35秒開始120frameで比較しました。平均侵入点44.708→16.517、最大79→32、最大深さ.048377→.030013。元FBX/skin weightは変更しません。.0768はframe0/root38で骨長内の外部到達が不可能と診断され、不採用です。JSONのvalidはskin参照との一致で、contactClear=falseのframeは残ります。骨sphere成功をcloth全体の品質合格とは扱いません。記録はbone-radius-trial-comparison.json。前面と左右で.008/.038を比較し、少し広がる裾を確認しましたが側面の面の埋まりは残ります。両側の比較は各120frame/GPU全valid、6枚の高解像度PNGを保存しています。

ClosestPointOnAxisを行う前に、正規化済み方向と長さから同じendを求め、parent XYZとlengthがFLT_MAX内かつend有限のときだけ外向きnextafterのAABBでstrict disjointを除外します。境界/巨大double/overflowは従来経路へ戻し、failureではdirection不変。solverは同じ座標でfraction0のstrict検査が成功した直後だけdefault検査を省きます。float保存後の接触・骨長・角度検査は保持します。半径や反復数を性能調整で下げません。

REDは距離budget0回と接触check budget3回の2契約失敗（bone-contact-early-out-red-test.log）。GREENではfar sphere/capsule・XYZ・diagonal/endpoint反転・FLT_MAX範囲・1ULP・bounds接触・DBL_MAX endoverflowで入力保持を確認。strict成功ケースはcheck4→3回です。counterはGKCORE_TESTINGだけで通常Runtimeに含みません。独立監査で入力検査/巨大値fallback/同位置でstrictがdefaultを包含する条件を確認しました。

CPU MSVC19.51のDebug60/60（5.18秒）、Release60/60（4.80秒）。Win11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK10.0.22621.0で、.038設定の0秒/3.35秒開始をDebug/Release各120、合計480frame実行。全GPU位置/法線とSkirt1,314点のCPU参照がvalid。位置最大9.314e-10、normal1.789e-7。旧solver対新版24画像、Debug/Release24組がbyte一致。start/loopの侵入点平均15.683/16.517、最大23/32、最大深さ.029584/.030013で残存。記録はbone-contact-early-out-{Release,Debug}-{start,loop}.logとimage-comparison.json。

同じ.038設定の通常可視Release、1280×720、VSyncなし、debug表示OFF、3000frame/warmup300を変更前後とも完走。184.749→190.296FPS、変更後p95 5.721ms、Draw .424 / Present2.388 / IK .509 / secondary1.876ms。変更前secondary1.886msとの差は小さいため大きな改善率を主張しません。前の.008設定207.044FPSとは物理条件が異なります。300FPS未達です。benchmarkはproduct buildが始まる17秒前に完了しており、buildと計測の重複はありません。

ゲーム構築用348ファイルを既存curated treeへ更新し、Runtimeと5sampleのRelease buildが成功しました。SDK56ファイルがallowlistに一致し、更新DLLを配置した外部consumerのGPU Init→Present→Shutdownが成功。START CheckOnly、formatter、BOM/CRLF、no-STLテストとdiff checkを確認。tests・検証tool・開発記録・私有assetはmainへ含めません。記録はbone-contact-early-out-game-{build,install,consumer-run}.logとgame/SDK manifestです。

高解像度6画像はpixel完全一致のPNGへ保存し、今回生成したPPM6枚とhash一致を確認した旧solver/Debug比較の重複PPM48枚を片づけました。最新Release24画像、比較hash、quality画像、実行logは保持しています。


## 2026-10-11 接触形状の準備を反復中に再利用する

capture専用QPCをUpdateへ追加し、base/FK、settings/shape/確保、chain clone/targets/solver、orient/cache、rendered FK/contact、commitを区切りました。成功更新のみ出力し、QPC失敗は計測を無効にするだけです。旧state解放前にchain countを保存して、Commit後に参照しません。通常SDKのcompile経路には計測を含みません。固定delta1/60の100sample（初期20除外）で変更前base .470328 / shapes .002680 / solver2.708335 / orient .067046 / check .069766 / commit .008921 / total3.327076ms。solverを主対象に選び、重複source FKの省略は今回行っていません。

Project/Checkは上限64のstack prepared shapesで、start/end/axis/length²/radius/tolerance/旧AABBを1形状1回計算します。Projectでは固定rootの内外を1回保存し、最後のprivate Check coreにも同じprepared dataを渡します。動的candidate位置のValidatePositionsは省かず、全shape/pass順・専用エラー・root例外・既存接触許容条件を保持。ClosestSegmentPairは同じaxis/dot演算を再利用し、raw Avoid経路は余分なbounds準備を加えません。旧output原子性と巨大double/NaN/overflowは維持します。独立監査で検査順、rootが不変である条件、stack寿命とカウンターを確認しました。

REDは2形状・2点の遠方Projectでprepare10 expected2の失敗（secondary-prepared-red-test.log）。GREENはProject終端Checkを含め2回、独立Checkも各形状1回。既存の接触・中点通過・複数形状・境界・float丸め・失敗保持の全契約を通しました。新counterはGKCORE_TESTINGのみです。

CPU MSVC19.51のDebug60/60（4.92秒）、Release60/60（4.81秒）。Win11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK10.0.22621.0で、.038設定・0秒/3.35秒開始をDebug/Release各120、計480frame。全GPU位置/法線とSkirt1314点のCPU参照がvalid、位置最大9.314e-10/normal1.789e-7。保存24姿勢は旧版と両構成でbyte一致し、侵入点平均15.683/16.517、最大深さ.029584/.030013も保持。物理設定や見た目を変える最適化ではありません。記録はsecondary-prepared-image-comparison.jsonです。

同じ固定deltaの区間計測後はsolver1.974252、total2.575593ms。通常可視Release3000frame/warmup300は198.637→208.670FPS、secondary1.866→1.549ms、後p95 5.272ms、Draw .388/Present2.287/IK .508ms。captureは時間刻みと検証条件が違うためFPSと混同しません。300FPSは未達。最新記録はsecondary-stage-profile-comparison.json、secondary-prepared-{before,after}-benchmark.logです。

dev専用skin診断を追加し、選択1frameのみ最大32点のposition/mesh所属/最深身体shape、positive weightのroot/child/bodyShapeAncestor/other合計、top4 bone、現在drawing FKの骨球距離を記録します。JSON stringをescapeし、詳細失敗とlegacy probe結果は分けます。無指定の480frameでは旧aggregate出力を保ち、frame110は11点、最深Spineに.009915、Skirtroot8.3%/body祖先91.7%。最深frame81は30点、Yumeka_Cos_Skirt/local28（global31877）がUpperLeg_Lへ.03001297、Skirt_3_L.002 68.08% / Skirt_4_L.002 31.92%でbody寄与0、骨球外へ.03696/.09170です。sparse pointはfloat変換された参照値なので、aggregateのdouble skin距離との差には丸め誤差があります。骨の小球が実skinの幅を覆わない点を確認しました。過去のCircle.051単独のraw cluster集計を、全GPU geometryの骨寄与へ一般化しません。元asset/weightを変更せず、頂点補正はOFFのままです。次の候補はcloth幅・隣接鎖の間を支える骨側制約です。

ゲーム構築用348ファイルを既存curated treeへ反映し、Runtime+5sampleのRelease build成功。SDK56ファイルがallowlistに一致し、更新DLLを置いた外部consumerのGPU Init→Present→Shutdown成功。test toolと詳細diagnostic export、開発記録、私有assetはmain/SDKへ含めません。通常RuntimeではQPC/counterコードがcompileされません。記録はsecondary-prepared-game-{build,install,consumer-run}.log、game/SDK manifestです。

今回生成したDebug比較24画像、hash一致した旧Release24画像と区間計測2画像を片づけ、詳細81frameはlossless PNGへ保存してPPMを削除しました。最新Release24画像、全検証logと比較hash/診断JSONは保持します。

## 自由bone間の接触補助

FModelSecondaryMotionLinkとSetModelSecondaryMotionLinksを追加し、別々の鎖の自由bone原点を半径付き線分で結び、身体球・カプセルから両端を押し出します。骨長・角度・鎖接触を再適用し、float保存後と描画FKの線分を検査してから候補を確定します。辺の長さやcloth幅を固定する機能ではありません。root/仮想末端・同じ鎖・重複pair・不正半径/反復数は拒否します。最大1024本、反復1〜32、既定32。Resetとcollider置換/解除後も設定を保持します。

REDは自由線分stub、setter stub、collider置換時の設定消失で取得。複数形状・側面/端部・中点・退化線分・大きな有限値・float範囲・別名・失敗出力保持を独立距離計算で確認しました。APIではdeep copy、独立instance、再登録、reset、delta0/正delta/hitch/teleport、非収束時のsecondary state/cache/FK保持を検査しています。animation clockとIKを先に変更した呼出しまで巻き戻す契約ではありません。CLI重複テストの文字列はpointer比較を内容比較へ直しました。監査で見つかった8.5を反復8+半径.5と読む問題もRED後、整数直後の区切りを必須に修正。従来9/10/11数値行は保持します。鎖config helperは行ごとに登録し、後の行の失敗時にはviewer/captureが起動を中止してmodelを破棄します。設定file全体をrollbackするAPIではありません。

CPU MSVC19.51のDebug/Release各60/60件成功。Win11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK10.0.22621.0で、単一接続と10本輪状接続を各0秒/3.35秒開始、Debug/Release各120frame確認。合計960frameのGPU位置/法線とSkirt1314点のCPU参照がvalid、48組の構成間画像がbyte一致。位置最大誤差9.314e-10、normal1.789e-7。接続OFFのRelease240frameもvalidで、従来24姿勢がbyte一致しました。半径.055の試行はframe76で失敗し、完走結果へ数えません。

START14は既存.038骨球/身体8形状に、スカートの2番目の自由boneを輪状に結ぶ10線分を加えます。頂点補正OFF、Cで表示切替。loop120frameの平均侵入点は16.517→15.833、最大25点、最大深さ.030013→.020639 model unit。start最大深さは.017353。2560×1920の同時刻画像で限定的な改善を確認し、面の埋まりは残ります。最大侵入点の骨原点間距離は.052388で半径.038の外、2つのskin寄与位置もbody内でした。weightからの位置再構成誤差3.154e-9。骨原点の接触成功をskin面全体の非侵入と取り違えません。線分の長さ固定、自己衝突、三角形面、連続接触、world SRT慣性は未対応です。

通常表示Release1280×720、VSyncなし、標準効果、3000frame/warmup300を同batchで完走。接続OFF207.058FPS、p95 5.378ms、secondary1.551ms。輪状ON177.136FPS、p95 6.869ms、Draw .397/Present2.232/IK .508/secondary2.450ms。接触条件の違うON/OFFを速度最適化とは扱いません。300FPS未達です。最新記録はsecondary-links-final-{debug,release}-test.log、secondary-links-image-comparison.json、secondary-links-off-image-comparison.json、secondary-links-{off,ring}-benchmark.log、bone-link-coverage-analysis.json、secondary-links-quality-images.json。

最終format後のCPU Debug60/60（6.89秒）、Release60/60（4.72秒）成功。ゲーム構築用351ファイルのcurated treeからRuntimeと5sampleのRelease build成功、Runtime SDK57ファイルがallowlistに一致し、新APIを参照した外部consumerのGPU Init→Present→Shutdownが成功しました。通常viewerの接続debug表示30frameとSTART -CheckOnlyも成功。公開ABIの既存settings layoutは保持し、新しい値型と関数を追加しています。独立read-onlyレビューでもcoreに重大な指摘はありません。tests・開発記録・私有assetをmainへ含めません。最新記録はsecondary-links-game-{build,install,package,consumer-build,consumer-run}.logとgame/SDK manifestです。使用後の生成PPM・重複Debug/旧版画像・調査scriptを片づけ、最新Release画像・lossless拡大PNG・失敗記録・現build・元assetを保持しました。cleanup manifestに削除前hashと試行configを残しています。

## 接続の端点と形状準備を更新内で共有する

ConstrainSecondaryMotionLinksの開始時に、接続端点を一度だけ鎖内の点へ解決し、完全一致するfloat半径ごとに身体形状を拡張・準備します。従来のExpandとPrepareの結果を保存し、投影と反復後Checkで再利用します。更新をまたぐcacheではなく、同じUpdate候補の寿命に限ります。最大1024接続×64形状の確保失敗は候補を確定する前に返します。pass/link/shape順、毎回の鎖Step0、float保存、最終描画FK検査を維持します。既存の準備値型をFPreparedSecondaryMotionCollisionShape.hへ分離し、SDK公開型とAPIのlayoutは変更しません。

同じprevious/current shape pointerを渡す鎖更新では、半径拡張を1配列だけ作りCoreの両入力で共有します。別配列と半径0の経路、Core本体の式と積分順は保持します。prepared shapeは生成関数の出力だけを使う内部契約で、各投影の端点範囲・alias検査は続けます。

REDはprepared API stub、端点/準備回数stub、正radiusの同一pointer拡張2回 expected1で確認。prepared APIは独立線分距離、raw結果byte一致、複数shape、ゼロ長、空/null/count超過/alias/不正入力と失敗出力保持を検査。同半径2接続・2bodyで端点解決2回/shape準備2件、別半径を足した3接続では3回/4件を確認しました。接続なし/shape0では両counter0、確保失敗は位置を保ちます。test counterはGKCORE_TESTINGだけで、通常Runtime/SDKには入りません。準備値は接触計算のmoduleで管理します。

最終CPU MSVC19.51のDebug60/60（4.75秒）、Release60/60（4.30秒）成功。同じbuild directoryでDebug/Releaseの全targetを同時buildしたとき、FBX fixture生成が競合してRelease buildだけ失敗しました。順番に再buildして成功を確認し、最終Release全60testを再実行しました。記録はlink-cache-concurrent-fixture-build-failure.logです。今後同じdirectoryの構成buildは並行実行しません。

Win11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK10.0.22621.0、YUMEKA/Silly Dancing+Capoeira blend/右腕IK/13鎖/8body/接続10本/.038/頂点補正OFFで、0秒・3.35秒開始を各120frame、Debug/Releaseで実行。480frameのGPU位置/法線とSkirt1314点のCPU参照がvalid。24姿勢の両構成48画像は変更前とbyte一致し、構成間24組も一致。Releaseの接続OFF120frameと12画像も旧版と一致。Debug2区間は最終的に順番に実行しました。loop侵入平均15.833/最大深さ.020639 model unitは保持しています。侵入を減らす変更ではありません。

固定delta1/60のRelease区間計測（初期20frame除外後100sample）はsolver3.309927→3.260583ms、全Update3.924160→3.865686ms。通常可視Release1280×720、標準効果、VSyncなし、debug表示OFF、3000frame/warmup300は176.127→174.119FPS、secondary2.462→2.436ms、後p95 6.968ms、Draw .421/Present2.314/IK .512ms。揺れ更新の短縮は小さく、FPS改善は確認できず300FPS未達です。prepared共有だけの中間runは171.800FPSで、最終結果とは分けます。ログはlink-cache-{before,after,final}-benchmark.log、区間と画像はlink-cache-image-comparison.json、OFFはlink-cache-off-image-comparison.jsonです。

ゲーム構築用352ファイルを既存curated treeへ反映し、Runtimeと5sampleのRelease build成功。Runtime SDK57ファイルがallowlistに一致し、payloadを更新した外部consumerのGPU Init→Present→Shutdownも成功しました。公開API/SDKファイル数は保持し、追加headerはソース内の接触計算用です。test counter・tests・開発記録・私有assetはmain/SDKへ含めません。独立read-onlyレビューで重大な問題は見つからず、prepared lifetimeは各Update内に限ること、shape/link/pass順と最終FK検査を維持することを確認しました。全480frameのskin検証JSONも旧版と一致しました。生成画像はhash一致を確認した後、重複するDebug/旧版/中間画像を片づけ、最新Releaseと最新の検証記録、元assetを保持しました。記録はlink-cache-game-{build,install,package,consumer-build,consumer-run}.log、game/SDK manifest、link-cache-cleanup.jsonです。

## 鎖制約の参照値と接触準備をsubstep内で共有する

固定されたstepTargetsから求める長さと正規化方向を、各substepの最初のconstraint passで骨順に保存し、後のpassで再利用します。Projectが先に失敗する場合やゼロ長でrestartする場所は保持。substepごとにcacheをClearし、動くpositionsは再利用しません。通常鎖のProject/Checkも準備済み形状を受ける内部関数へ分け、半径拡張→補間→1.001ガード後の形状をsubstepごとに一度準備します。rawとpreparedは共通projection kernelを使い、root例外・PBD・shape/pass順・許容幅・失敗output保持を維持。RawAvoidとfloat保存後の最終RawCheckは保持し、raw投影/検査の固定領域も保ちます。公開APIとsettings layoutは変更しません。

REDは反復ごとの参照計算、prepared Contacts stub、solverの形状準備3回 expected2で確認。2 segment×2 substepではiterations8/32とも参照4回、bodyありdelta0では1回。overlap sphereのProject非収束は参照0回のままstateを保持。prepared投影/検査はrawとbyte一致、独立距離・root内部免除・別名・空形状・不正入力・失敗atomicityを検査。接触形状の準備型契約を汎用builderにも合わせました。追加counterとJSONはCPU tests/frame-captureだけ、直列Update用で、通常Runtimeには入れません。

最終CPU MSVC19.51 Debug60/60（4.85秒）、Release60/60（4.38秒）成功。Win11 build26200 / RTX4070SUPER driver617.42 / v14219.29 / SDK22621.0で、YUMEKA＋Silly Dancing/Capoeira blend＋右腕IK＋13鎖＋8body＋輪状10接続・.038・頂点補正OFFを0秒/3.35秒からDebug/Release各120frame、順番に実行。480frameのGPU位置/法線とSkirt1314点の参照がvalid、全480skin JSONが旧版と一致。両構成48画像は旧24姿勢とbyte一致、構成間24組も一致。接続OFF Release120frame/12画像も旧版と一致。侵入平均15.833/最大深さ.020639 model unitは保持します。

固定delta1/60のRelease、初期20frame除外後100sampleは参照計算平均2216.88→241.20（最大5888→472）、solver3.292944→2.967918ms、全Update3.898166→3.572878ms。参照共有だけの中間solver3.246410msから、形状準備の共有でさらに削減できました。可視Release1280×720・標準効果・VSyncなし・3000frame/warmup300は前171.990FPS/secondary2.437ms、完走した再計測173.519FPS/secondary2.288ms、p95 6.872ms、Draw .523/Present2.378/IK .513ms。FPS改善は小さく、300FPSには未達です。

最初の可視計測はwarmup中に形状5/接続8の自由線分接触で失敗し、計測0frame・completed=false。原因は未確定で、成功扱いにしません。失敗時点のdeltaが未保存だったため、benchmark専用のdelta trace/replayを追加。更新前に各deltaを保存し、replayは有限非負値/件数をInit前に検査します。診断modeはFPSを出しません。空白だけの行の受理をRED後に修正し、6種類の不正入力をInit前に拒否しました。通常viewerはこの診断機構をcompileしません。

実測3300deltaと乱数seed7の1200delta（.001〜.019秒、113frameごと.075秒）を、変更前HEADのSolver/Collisionから作った一時capture DLLと現在版で各4500frame再生し、双方が完走。新側は初回delta .001/.004/.008/.012/.018/.025/.040/.075/.120/.220/.300、その後.006秒の各300frameも完走。可視trace2回も各3300frame完走しましたが、元の失敗の再現・原因確定ではありません。接続投影と描画FKの失敗に診断contextを追加し、次の失敗時に区別できるようにしました。

SDK57ファイルがallowlistに一致し、現在DLLを置いた外部consumerのGPU Init→Present→Shutdownも成功。START CheckOnly成功。devへ検証済みの計算共有と再現診断を保存しますが、この区切りのmain反映は未確定の実時間接触失敗のため保留します。全SDKの完成待ちや、全変更に対するGPU検証待ちの禁止条件ではありません。記録はreference-cache-{red,final-debug-test,final-release-test,before-benchmark,retry-benchmark}.log、reference-cache-image-comparison.json、reference-cache-off-image-comparison.json、constraint-delta-{trace-1,jitter}-{replay,legacy}.log、reference-cache-final-benchmark-failed.log、reference-cache-dev-{install,package,consumer-run}.logです。

重複した旧版/中間/Debug画像と、一時legacy captureのソース・project・object・DLLをworkspace内の生成物と確認して片づけました。240ファイル、110,587,791byteを削除し、最新Release画像・元asset・現build・SDK・成功/失敗ログ・再現用trace1/2とjitter入力・削除前hashを保持しています。整理記録はreference-cache-cleanup.jsonです。

最後のread-onlyレビューで見つかったreplay行内NULの見落としもRED後に修正。binary読取のftell差と文字列長を照合し、NUL入りEOF行をInit前exit2で拒否しました。通常のCRLF10行は実モデルで完走。ログはconstraint-invalid-nul-{red,green}.log、constraint-valid-crlf-green.logです。

## 接続候補に姿勢変換の誤差分を残す

2026-10-12。以前の実時間warmup失敗は、その時のdeltaと段階を記録できておらず、原因は未確定です。変更前の可視trace 4回×1200frameと既存trace 2回×3300frameでは失敗しませんでした。自由線分の球・カプセル投影を独立な距離計算で50000ケース調べても失敗せず、純粋な5骨姿勢をOrient→FKへ戻す調査では境界跨ぎを再現しました。候補距離.000999011700787、最終閾値.000999011561042、FK後.000999011337969です。差は約3.63e-10ですが、候補を許容帯の端まで受け入れると最終検査だけが失敗します。このfixtureと元のshape5/link8失敗を同一原因とは扱いません。

prepared自由線分Checkへ有限な0〜1の許容割合を追加し、接続候補を0.5、最終FKは従来の1.0で検査します。候補側で全許容幅を使わず、半分を姿勢変換の丸め用に残します。投影式、身体半径、骨長・角度、32回上限、最終許容幅、失敗時の確定状態保持は変更しません。折半は誤差予算の配分であり、任意の座標尺度・骨深度に対する数学的保証値ではありません。最終FK検査を省略せず、超過時は失敗を返します。

REDは許容割合を渡さないprepared Checkと、既存APIの候補が許容帯の後半まで入る独立距離oracleで確認しました。割合0では有効なsphere設定でも鎖長復元との交互補正が32回以内に収束せず、割合0.5でGREENとなりました。純Orient→Evaluate fixtureで丸めによる閾値跨ぎ、割合0.5による危険候補の拒否、半幅以内の候補が内向き半幅移動後も最終幅を満たす条件、不正割合を確認しています。固定の距離ログへの狭い一致assertは外し、独立計算の境界不等式で確認します。最終MSVC19.51 CPU Debug60/60（5.03秒）、Release60/60（4.63秒）が成功しました。

Win11 build26200 / RTX4070SUPER driver617.42 / v142 MSVC19.29 / SDK22621.0で、YUMEKA・2motion blend・右腕IK・13鎖・身体8形状・輪状接続10本、頂点補正OFFの0秒/3.35秒開始をDebug/Release各120frame、計480frame実行しました。GPU位置/法線と全Skirt1314点のCPU参照が有効です。最大位置誤差9.314e-10、法線1.789e-7。構成間24組の画像と各120件のskin記録は一致しました。変更前48画像とは異なり、差は24〜96pixel/640×480で、鎖の確定位置が少し変わっています。loop侵入点平均15.833・最大深さ.020639は残り、布面の品質改善とは扱いません。

新しい候補判定は追加反復を要し、固定120frameの参照計算総数はstart25880→27480、loop28160→29920でした。通常可視Release1280×720・VSyncなし・標準効果・3000frame/warmup300は完走し、162.817FPS、p95 7.897ms、secondary2.687msです。300FPSは未達で、前回173.519FPSからの改善も確認できません。既存実測3300frame＋変動1200frameの可視replay、新しい実時間trace1200frameも完走し、今回の通常計測を含む9000frameで接触失敗は出ていません。これを元のwarmup失敗の原因同定とは扱わず、この区切りのmain反映は引き続き保留します。

Runtime SDK57ファイルがallowlistに一致し、更新DLLを使ったinstall済みSDK consumerのGPU Init→Present→Shutdownも成功しました。記録はlink-roundtrip-final-{debug,release}-test.log、link-roundtrip-{Release,Debug}-{start,loop}.log、link-roundtrip-image-comparison.json、link-roundtrip-benchmark.log、link-roundtrip-realtime-trace.log、constraint-delta-{trace-1,jitter}-visible.logです。純pose/projection調査の生成buildと一時script、重複Debug画像はhashを残して片づけ、最新Release画像、比較PNG、delta、失敗記録、現build、元assetを保持します。次の削減候補は、接続投影で位置が変わらない鎖への再制約です。floatの再正規化は再実行で値が変わり得るため、更新順と接触結果を比較してから採用します。

## 位置が変わらない接続鎖の再制約を省く

2026-10-12。delta0の再実行が同じfloat状態を返すか、独立MSVC19.51 Debug CPU probeで4配置を各6回調べました。無接触短鎖とsphere短鎖は初回後に安定しましたが、capsule6点鎖では以後も3成分が最大7.7e-10、sphere24度制限8点鎖では4成分が最大4.7e-10ずつ変化しました。速度とpreviousTargetsは不変です。したがって全結果のbit同一を前提にはせず、初回で保存状態の長さ・角度・接触を必ず検査した上で、省く処理を決めます。fixtureとtoolchainはconstraint-repeat-probe/probe-manifest.txtとdebug-repeat-results.logへ記録しました。

ConstrainSecondaryMotionLinksに更新内だけのrestorePending配列を加えました。投影→float保存後に端点の12byteが変わった鎖を予約し、初回は従来どおり全接続鎖へStep0します。後続は予約がある鎖だけ復元します。全linkの投影を終えてから鎖順にStepし、同じ鎖が複数linkに出ても1回/passです。Step成功時に予約を消し、CheckCandidateで全linkを調べて、全不成立linkの両端鎖を次passへ再予約します。投影のfloat丸めで端点が変わらない場合も、未解決成分の再Stepを維持します。最初の診断を保持し、後続判定の診断は別Stringへ出します。速度用のaffectedは従来の累積対象のままです。target/settings/body/radius、半幅の候補判定、32回上限、最終float/FK検査、API失敗時の確定状態保持は維持します。

REDは接触だけの2鎖で復元4回、遠い接続鎖を追加した4鎖で8回となることを確認。期待値は初回だけを加えた6回です。GREENでは6回となり、接触鎖の全物理状態が追加鎖の有無で一致しました。独立距離・骨長/角度・root/targets/有限値、初回の崩れた長さ修復、初回invalid settings/state拒否、正deltaでの遠い鎖の非零速度維持と -0 + 0 の符号、共有鎖3本の1回/pass、同時sphere2成分の接触成立も検査しました。counterはGKCORE_TESTINGだけで、通常Runtime/SDKには入りません。全不成立linkの予約とscratch String失敗時の制御は独立read-onlyレビューでも確認し、重大な問題は見つかりませんでした。丸め停滞を直接起こす専用の片側端点fixtureは未確立です。

最終CPU MSVC19.51 Debug60/60（4.96秒）、Release60/60（5.21秒）成功。Win11 build26200 / RTX4070SUPER driver617.42 / v14219.29 / SDK22621.0、YUMEKA・Silly Dancing/Capoeira blend・右腕IK・13鎖・身体8形状・輪状接続10本、頂点補正OFFの0秒/3.35秒開始をDebug/Release各120frame、計480frame実行しました。GPU位置/法線とSkirt1314点の全参照が有効で、最大位置誤差9.314e-10、法線1.789e-7です。構成間24組の画像と240件のskin記録が一致。変更前48画像のうち34枚がbyte一致し、残りも各640×480の1〜3pixel差です。skin記録自体は完全一致ではありませんが、各frameの侵入点数は同一、深さ差の最大はstart8.36e-8/loop6.12e-8 model unitです。loop平均15.833・最大深さ.020639の侵入は残り、画質改善とは扱いません。

固定120frameの参照計算総数はstart27480→24616、loop29920→25332。初期20frame除外後の平均は235.20→207.36、254.00→212.76です。通常可視Release1280×720・VSyncなし・標準効果・3000frame/warmup300を同batchで前後とも完走し、168.090→168.347FPS、secondary2.499→2.382ms、後p95 6.879ms、Draw .526/Present2.458/IK .517msでした。更新の削減は確認しましたが全体FPSの改善は小さく、300FPS未達です。実測3300＋変動1200deltaの可視replayも4500frame完走しました。元のwarmup停止の原因同定とは扱わず、この区切りのmain反映は保留を続けます。

Runtime SDK57ファイルがallowlistに一致し、更新DLLを置いた外部consumerのGPU Init→Present→Shutdownも成功。記録はconstraint-dirty-{red-debug-test,green-debug-test,final-debug-test,final-release-test,before-benchmark,after-benchmark,trace-1-replay,jitter-replay}.log、constraint-dirty-{Release,Debug}-{start,loop}.log、constraint-dirty-image-comparison.json、constraint-dirty-skin-config-comparison.jsonです。現在のRelease画像とlossless PNG、最新検証記録、元assetを保持し、重複画像・調査用build/scriptはhashとfixture仕様を残して片づけます。次は、同じ更新内の鎖Step0が繰り返し準備する一定の半径拡張・形状・目標線分を、既存の計算順と検査を保って共有する範囲を調べます。

## 接続更新内で鎖の身体形状準備を共有する

2026-10-12。ユーザー指定のKawaiiPhysics公式source feee5fe727261ba9cfe725c4af280aa9e381c31dを読み、シミュレーションstepの前に接触形状の派生値をまとめる構成と骨間距離XPBD制約を確認しました。参照元とMIT本文はTHIRD_PARTY_NOTICES.mdへ記録。今回の計算共有は既存gkcoreの数学とfoundationの配列で実装し、Unreal Engineの型や依存物は導入していません。公式の最後に骨長を1度戻す処理へ全置換せず、gkcoreの接触再検査と失敗時の確定状態保持を続けます。公式の骨間距離制約はスカート鎖の間隔を保つ機能であり、現在の身体接触線分とは別です。距離制約は次の候補にします。

ConstrainSecondaryMotionLinksの寿命内にFModelSecondaryMotionStepShapeCacheを置き、同じfloat節半径ごとに、半径拡張済みraw body、delta0補間/1.001 guard済みraw、prepared shapeを一度作ります。flat arraysとgroup offsetはfoundation Arrayで所有し、source bodyは更新中不変の借用です。FModelSecondaryMotionStepShapesは次のGetまでに使い切る参照descriptorで、物理stateや次Updateには保存しません。Get失敗時は出力descriptorと登録済み内容を保ち、確保で内部配列位置が変わり得ることをheaderへ明記しました。

新internal StepSecondaryMotionChainWithPreparedCollisionsはdelta0だけを既存Coreで処理します。Step shapeの生成・準備だけを共有し、raw Avoidにはguarded、Project/Checkにはprepared、保存後のraw Checkにはexpandedを使います。参照方向/長さ計算、投影・親子順・角度制限・float検算・最終FK検査は維持し、通常delta pathも従来の構築順です。GetはCoreより先に形状を準備するため、形状とsettings/targetsがともに不正な場合の診断優先順位は変わり得ます。このprivate経路の失敗時state保持は確認しています。

REDはcache stubで初回Get失敗、cache実装後のraw-delegate prepared stubでguard形状の再準備2件が残る失敗、リンク統合前の準備14件 expected10を順に取得。GREENでは同半径3鎖・2bodyの接続用2件＋共有guard2件＋保存後raw検査6件、計10件となりました。独立oracleで拡張の最小外向きfloat丸めとguardのnearest float cast、signed zeroのraw保持/補間後+0、preparedとrawの3回連続state byte一致を確認。異なる半径/再Get/空body/null/count65/NaN/guard overflow、preparedで投影非収束時の参照計算0回とstate保持、allocation failure allow0〜12で出力保持・既存group再取得・retry成功を確認しました。保存後raw Check/RawAvoidの計数は残るため、prepared stepのprepare数0という期待は使いません。独立read-onlyレビューでも重大な不具合は見つかりませんでした。

最終CPU MSVC19.51 Debug60/60（5.10秒）、Release60/60（4.82秒）成功。Win11 build26200 / RTX4070SUPER driver617.42 / v14219.29 / SDK22621.0、YUMEKA・2motion blend・右腕IK・13鎖・身体8形状・輪状接続10本、頂点補正OFFの0秒/3.35秒開始をDebug/Release各120frame、計480frame実行。GPU位置/法線とSkirt1314点が全frame有効、最大位置誤差9.314e-10、法線1.789e-7。変更前と全480skin記録/48画像が一致し、構成間24組もbyte一致しました。loopの侵入平均15.833/最大深さ.020639は保持しており、布面の品質改善とは扱いません。

通常可視Release1280×720・VSyncなし・標準効果・3000frame/warmup300は変更前170.854FPS/secondary2.384ms、後175.739FPS/secondary2.317ms、後p95 6.643ms、Draw .430/Present2.370/IK .516msで双方完走。差は小さく300FPS未達です。delta replayは実測3300frameを隠しcapture DLL、変動1200frameを通常可視DLLで完走しました。最初の可視実測replayは295frame・completed=false・exit0で途中終了。APIエラーはなく、イベント/終了入力の経路と判別できるため、完走成功にも接触失敗の再現にも数えません。未完走logを残して隠しwindowで再実行しました。以前のwarmup接触失敗の原因同定とは区別し、今回もmain反映は保留します。

Runtime SDK57ファイルがallowlistに一致し、更新DLLを置いた外部consumerのGPU Init→Present→Shutdownも成功。記録はstep-shapes-{red-debug-test,prepared-red-test,links-red-test,links-green-test,debug-test,release-test,before-benchmark,after-benchmark,trace-1-replay,jitter-replay}.log、step-shapes-{Release,Debug}-{start,loop}.log、step-shapes-image-comparison.json、step-shapes-native-results.jsonです。現在のRelease画像とlossless PNG、delta、失敗記録、現build、元assetを保持し、一時検証scriptと重複画像はhashを残して片づけます。公式reference checkoutは次の骨間距離制約の調査にも使うため.devtools内へ固定revisionのまま保持します。

## 隣接する自由boneの距離制約と実モデル検証

ユーザー指定の公式参照資料を調べ、身体接触の補助線分と、隣接鎖の間隔を保つ距離制約を別の役割として実装しました。公開値型FModelSecondaryMotionDistanceConstraintとSetModelSecondaryMotionDistanceConstraintsを追加し、既存settings layoutは保持しています。restDistance=0は登録時のclip・blend・IK反映済みで揺れ前の姿勢から測ります。最大1024組、別鎖の自由bone限定、重複ペア禁止です。設定・状態をinstanceで所有し、Reset・同先頭boneの再登録後も設定を保ちます。固定rootを動かさず、最終FKのsoft残差も検査し、失敗時は以前の全鎖状態を保ちます。

REDは未実装距離投影と公開setterで捕捉しました（distance-red-debug-{build,test}.log）。GREEN後に既存テスト2件が失敗しましたが、新しい距離併用fixtureの判定を後続の距離なしfixtureへ混ぜていたことが原因です。既存判定を戻し、距離併用中だけ別の判定を行うよう分離しました。数式は剛体の圧縮・伸長、重心保持、softの解析残差、再投影の不変性、同一点の方向選択、不正値・alias・overflowと出力保持を確認しています。APIは自動基準距離、入力のコピー、別鎖自由点、距離だけの形状なし更新、身体接触との両立、未収束時の状態/FK保持、解除、設定の寿命とdebug接続線を検査します。

独立レビューで正のdeltaを1e-4へ持ち上げる処理を指摘され、実際のdeltaへ修正しました。1e-5秒・softのlambda込み残差が成立し、hが1e-5のまま保持される契約を追加しています。XPBDの両端は逆質量1、hは正のframe delta、時間0/hitchは1/120秒です。各Updateでlambdaを0に戻し、反復内では蓄積します。これは距離をframe末尾で解く処理で、各鎖のsubstepと全制約を同時積分する構成ではありません。Resetは元の姿勢へ戻し、次のUpdateで制約を解きます。相対精度を満たせない短い距離・大きいfloat座標は失敗する場合があります。

最終sourceのCPUテストはDebug60/60（5.16秒）、Release60/60（4.89秒）成功。MSVC19.51/SDK10.0.28000.0、cmake --build build/dev-windows --config Debug/Release --parallel 4、ctest --test-dir build/dev-windows -C Debug/Release --output-on-failureを実行しました。ログはdistance-final-{debug,release}-{build,test}.logです。

実モデルはYUMEKA FBX、Silly DancingとCapoeiraの外部blend・右腕IK・follow-motion、13鎖・身体8形状・接触線分10本・距離制約10組、頂点補正とdebug表示OFFを使用。骨球半径・脚カプセル・最大角度は変更していません。柔らかさ1e-5はframe0で未収束になり採用せず、0.001をSTART15の試験設定にしました。0秒・3.35秒から各120frame、固定delta1/60のDebug/Release計480frameで更新・GPU位置/法線・Skirt1314点の参照が有効で、保存24組の画像が一致しました。開始区間の平均侵入点数は15.908→16.008、最大深度.017340→.017313。loopは15.833→15.483、最大深度.020639→.023409。布面の改善とは判定していません。30/60/120Hzで同motion時刻4.25秒まで追加192frameも成立しましたが、更新頻度で姿勢差は残ります。

通常可視Release1280×720、VSyncなし、標準効果、3000frame/warmup300の計測は、warmup後340frameで接続線分1・身体形状4の未収束により停止しました。completed=falseの151.599FPSを完走性能として採用しません。delta記録付き再試行3300frameは完走しましたが、元の停止は再現できていません。距離→接触線分→鎖全体復元という反復では互いの補正を戻す場合があり、次は投影/復元前後の残差を記録して切り分けます。300FPSと布面の品質、実時間安定性は未達です。この区切りのmain反映を保留します。これはSDK全体の完成待ちや全変更の一律禁止ではありません。

検証PCはWindows11 build26200、RTX4070 SUPER driver617.42、Runtime v142/MSVC19.29・SDK10.0.22621.0です。native commandはgkcore_real_model_capture_tests.exe <YUMEKA FBX> 1.1166145684 0 .671678712 -.2517482195 external-blend-ik <Silly Dancing.fbx> <Capoeira.fbx> --follow-motion --secondary-motion samples/config/yumeka-bone-contact-motion.txt --secondary-colliders samples/config/yumeka-bone-colliders.txt --secondary-links samples/config/yumeka-secondary-links.txt --secondary-distances samples/config/yumeka-secondary-distances.txt --materials <material-config.txt>。GKCORE_TEST_WINDOW_VISIBLE=0、GKCORE_VERIFY_GPU_SKINNING=1、GKCORE_VERIFY_SKIN_POINTS=Skirt、ANIMATION_FRAMES=120、START_SECONDS=0/3.35、STEP_SECONDS=1/60、CAPTURE_START_FRAME=108、CAPTURE_FRAMES=12です。

Runtime SDK58ファイルがallowlistに一致し、追加APIを参照した外部consumerのビルドとGPU Init→Present→Shutdownも成功しました。START -CheckOnlyは設定ファイルと起動引数を確認し、sample15は試験設定と明示します。ゲーム用sourceのcurator計画は358ファイルで、新しい公開headerと距離kernelを含み、tests・開発記録・私有assetを除外します。今回は計画の確認で、curated sourceの別ビルドやmainへの公開は行っていません。sourceのBOM/CRLF、formatterとdiff checkを確認します。

記録した今回の3300 deltaを距離制約ON/OFFで再生し、両方completed=trueを確認しました。距離OFFの現在sourceで追加240frameを描画し、前commitの24画像と全Skirt点参照JSONが一致しています。保存画像は画素一致を確認してlossless PNGへ変換し、重複したDebug/旧版のPPM、今回使い終えたtrial設定を片づけます。最新Release ON/OFF画像、元asset、現在build、成功/失敗ログとdelta traceは保持します。再生ログはdistance-real-delta-replay-{on,off}.log、画像/skin比較はdistance-image-comparison.json、distance-off-and-rate-comparison.json、整理記録はdistance-cleanup.jsonです。


## 骨長を戻す処理と接触投影の停滞を修正

まず、benchmarkの毎frameファイル書き込みとfflushが次の実測deltaへ含まれていることを見つけました。再生中はファイルが空という契約を、旧コードのprocessRunning=true/traceBytes=22でREDにしました（constraint-buffer-red-check.json）。全deltaを既存8192要素bufferへ保持し、終了後だけ書くように変え、processRunning=true/traceBytes=0のGREENを取得しました。終了後の611行はtraceWritten=trueです。失敗した更新のdeltaも保存されます。バッファと一括書き出しはbenchmark定義でだけ有効です。

この記録でshape4/link1の停止を611番目に再現しました。修正前の距離併用は610frame完了/611番目失敗、距離なしは611完走です。capture/test限定の侵入深さ測定をstubでREDにしてから実装し、球・カプセルの独立segment-distance oracle、退化、接線、外側の負深度、不正値とaliasの出力保持を検査しました。診断は最後2反復を3段階で採取します。pass30/31のshape4/link1は投影後depth=-8.206e-8/-9.195e-8、鎖復元後depth=9.538e-5/9.300e-5で、距離残差は1.308e-5/1.271e-5です。接触補正が骨長復元で削られることを数値で確認しました。

独立監査が提案した小fixtureは、Y軸カプセル半径.2、自由骨(-.5,0,0)/(.5,0,0)、各親をz=-1、骨長1・最大角20度です。旧接触法線+Zは両骨を伸ばすだけで、長さ復元により完全に元へ戻り、32反復で失敗します。しかし両骨をYへ20度回した姿勢は骨長・角度・身体・接続距離を満たします。この既知の可行姿勢を独立に検算してから、距離なし/softありの収束をREDにしました（constraint-cycle-red-test.log）。

反復の前半は旧投影を使い、後半は骨方向uから接触法線nのradial成分を除いたt=n-dot(n,u)uを使います。角度円錐の境界で外向き成分も除き、補正の分母をsum(weight²*|t|²)にして骨長復元が消す成分を見込みます。最終の骨長・角度・身体・距離・float・描画FK検査は保持します。接触を改善する方向が退化する場合は、カプセル全体の外へ出る支持平面への両側の移動を比較します。親は各link補正の直前の現在位置で、複数骨との関係を完全同時に解く手法ではありません。全制約を満たせない場合は以前の確定状態を保って失敗します。半径・角度・許容幅・反復上限32は緩めません。

小fixtureは距離あり/なし、端点順反転、任意軸回転と平行移動で成功し、角度0の不可行対照は失敗します。直接anchor投影は独立PBD値/旧pathとのbyte一致、空body no-op、不正anchor・aliasのatomic出力保持も検査。監査でfloat保存方向に対して角度境界の判定幅が小さい点を指摘され、64*FLT_EPSILONへ修正しました。通常のprepared shapeは生成関数の有効な値だけを使う既存内部契約を保ちます。

最終CPUテストDebug60/60（7.53秒）、Release60/60（5.12秒）成功。RuntimeMSVC19.29/v142・SDK22621.0、CPU MSVC19.51/SDK28000.0、Win11 build26200・RTX4070 SUPER driver617.42を確認しています。実データの611 delta replayはDebug/Releaseとも全611frameを完走。通常可視3000frame/warmup300は距離あり155.443FPS・secondary3.239ms、距離なし178.505FPS・secondary2.275msで双方完走しました。300FPSは未達です。固定delta1/60・0秒/3.35秒開始のDebug/Release各120frame、計480frameのGPU位置/法線とSkirt1314点参照が有効。保存24組の画像が構成間で一致し、修正前の保存画像/Skirt記録も保持しました。start平均16.008/max深さ.017313、loop平均15.483/max深さ.023409で、布面侵入は保持しています。

今回の停止は再現して修正しましたが、記録を残していない以前のshape5/link8のwarmup停止まで同じ原因だとは断定しません。新しい再現・可行/不可行・通常再生の結果を根拠に、この修正を安定した区切りとしてゲーム構築用sourceへ反映します。SDK全体の完成や300FPS達成とは区別します。Runtime SDKの公開API/ABIと58ファイルは維持し、新headerは内部計算用です。更新DLLの外部consumer GPU Init→Present→Shutdownも成功しました。

再現ログはconstraint-buffer-{red,green}-check.json、constraint-depth-{red,green}-test.log、constraint-cycle-red-test.log、constraint-failure-replay-{on,off}.log、constraint-fixed-replay-{Release,Debug}.log、constraint-final-{debug,release}-test.log、constraint-final-benchmark.log、constraint-off-benchmark.log、constraint-{Release,Debug}-{start,loop}.logです。Native入力は前節と同じmodel/motion/configで、replayはGKCORE_BENCHMARK_DELTA_REPLAY_PATH=<constraint-buffer-green-deltas.txt>、FRAMES=611、WARMUP=0、GKCORE_TEST_WINDOW_VISIBLE=0、GKCORE_TRACE_SECONDARY_CONSTRAINTS=1。通常計測はTRACE/REPLAYを指定せずFRAMES=3000/WARMUP=300。診断は通常SDKのbinaryへ含めません。

ゲーム構築用359ファイルをprepare_game_repository.pyから生成し、tests・開発記録・私有assetを除外した独立treeでRuntimeと5sampleのReleaseビルドに成功しました。Visual Studio18/v142、platform x64,version=10.0.22621.0を明示し、固定Forge/DXCと現在のshader artifactを使用。インストールSDK58ファイルがallowlistに一致し、game tree由来DLLの外部consumer GPU Init→Present→Shutdownも成功。記録はconstraint-game-{export,configure,build,install,consumer-run}.log、constraint-game-manifest.json、constraint-sdk-manifest.jsonです。START CheckOnly、formatter、BOM/CRLF、diff checkと関連package/no-STL検査を確認しました。画像は画素一致を確認して最新Release PNGへ保存し、重複Debug/PPM48個、旧game検証build3個、使い終えたexport stagingとPython cacheを削除しました。現在のNative/CPU/game build、元asset、成功/失敗ログと再現deltaを保持します。削除対象の生成物確認はconstraint-cleanup.jsonへ記録しました。

devの修正5ee9067と、ゲーム構築用mainの61bcbccを通常pushしました。両checkoutはcleanです。mainにtests・開発記録・私有assetを含めていません。再現した停止の修正と、この区切りの公開を完了しました。


## 鎖復元用の作業配列を1回の更新内で再利用

距離と接触を解く間に繰り返すゼロ時間復元で、位置・速度・旧位置・step目標・基準線分の5配列を毎回確保していたため、既存foundation Arrayの容量だけを共有しました。FModelSecondaryMotionSolverWorkspaceはConstrain内のlocal ownerで、Core入口で毎回Clearし、従来と同じReserve/Append・演算順で内容を構築します。1つのworkspaceを別の鎖へ直列に渡します。更新間・別threadへ保持しません。candidate stateの2配列は従来通り別に作り、成功時のみstateへ移します。目標・settings・current状態・shape viewの検証、参照方向と長さの再計算、接触と最終FK検査は省きません。旧APIはlocal領域を使い、public API/ABIは変更しません。

REDはworkspace overloadを旧Coreへ委譲するstubで取得し、warm時3allocation予算が不足する失敗を固定しました（solver-workspace-red-test.log）。1領域で別状態の3→5→3点を処理して旧raw経路の全位置・速度・previousTargetsとbyte一致を確認。非zero速度と負のゼロを保持します。warm0〜2allocationは失敗と状態保持、3で成功。cold0〜12allocationも失敗・retry・成功を覆います。Stringとbody shapeは注入前に準備しています。入力NaN、状態NaN速度、不正prepared view、実際のsphere接触、角度5度の接触不能後の同領域retryも確認しました。初期テストの+0期待と重複lambda宣言はテスト側の誤りとして直し、-0保持とraw byte比較を維持しています。

独立read-only監査では、scope内の直列使用、failure時candidate未commit、全配列Countの再構築とstale値の排除を確認しました。ゼロ長再起動の内部再入は、共有領域を再構築後にouterが即returnするため、以前のscratchを読み直しません。並行/任意再入共有をサポートする型ではありません。型layoutは旧基準線分のdouble[3]+doubleを保ちました。

最終CPU MSVC19.51/SDK28000.0でDebug60/60（6.99秒）、Release60/60（5.00秒）成功。追加の実接触/retryケースは両config focused検査で成功しています。NativeはWin11 build26200、RTX4070 SUPER driver617.42、MSVC19.29/v142/SDK22621.0。YUMEKAとSilly Dancing/Capoeira、blend+右腕IK、13鎖・身体8形状・接続10本・距離10組、頂点補正OFFを使用し、固定delta1/60の0秒/3.35秒開始をDebug/Release各120frame、計480frame実行しました。GPU位置/法線とSkirt1314点参照が全frame有効、全Skirt記録と保存24画像が前版と一致し、Debug/Release24組も一致。参照計算の平均start349.2/loop350.8は保持しています。布面侵入も保持しており、品質改善とは扱いません。

通常可視Release1280×720、VSyncなし、標準効果、debug/頂点補正OFFの3000frame/warmup300を前後とも完走。145.974→153.213FPS、secondary3.331→3.260msです。ただしDraw .512→.383/Present2.439→2.314msも変化したため、FPS差の全てを今回の効果とは扱いません。固定deltaのcapture別batchではsolver start4.035067→4.519662ms、loop4.488191→4.930774msで速度改善を確認できていません。同じv142 Releaseプロセスの3点・遠方capsule・1万復元×8batchの交互計測はfresh1.16655µs/shared1.08369µsでした。局所のallocation削減効果と実モデル全体のFPSを分けて扱います。300FPSは未達です。

実機のcommand/envは前節と同じmodel/motion/config/解像度で、GPU確認はGKCORE_VERIFY_GPU_SKINNING=1、SKIN_POINTS=Skirt、TEST_WINDOW_VISIBLE=0、ANIMATION_FRAMES=120、START_SECONDS=0/3.35、STEP_SECONDS=1/60、CAPTURE_START_FRAME=108、CAPTURE_FRAMES=12。通常benchmarkはBENCHMARK_FRAMES=3000/WARMUP=300のみ指定しています。記録はsolver-workspace-{red,green,final-debug,final-release,contact-debug,contact-release}-test.log、solver-workspace-{before,after}-benchmark.log、solver-workspace-probe-results.log、solver-workspace-{Release,Debug}-{start,loop}.log、solver-workspace-image-comparison.jsonです。旧停止の611 delta再生も確認し、実装の単なる宣言やカウンタだけで完了扱いしません。

611 delta再生はDebug/Release双方がcompleted=trueで完走。ゲーム構築用362ファイルの独立treeからRuntimeと5sampleのRelease buildが成功し、install SDK58ファイルがallowlistと一致、game treeのDLLを用いた外部consumer GPU Init→Present→Shutdownも成功しました。START CheckOnly、関連package/no-STL検査、formatter/BOM/CRLF/diffを確認。記録はsolver-workspace-replay-{Release,Debug}.log、solver-workspace-game-{export,build,install,consumer-run}.logとgame/SDK manifestです。最新Release PNGへ画素一致で変換し、PPM/重複Debug48画像、一時probeソース/build、使い終えたexport stagingとPython cacheを片づけました。probeの再現用ソース本文と削除前hashはsolver-workspace-cleanup.jsonへ保存し、元asset・現build・再現delta・検証ログを保持します。

## 方向回避で身体形状の準備を再利用

同じguard済みshapeの軸、二乗長、成分を縮めて桁あふれを避けるLength、保守的AABBを、既存prepared shapeへ保存しました。保守境界は旧式どおりfloat端点をdoubleにしてmin/max±radiusを計算し、nextafterで1 ULP外側へ広げます。後続の通常AABBも別に保持します。固定長方向回避は同じ順の最近点・線分対・cone計算を使い、mutableな親/方向/長さの検査、root例外、失敗時direction保持、最後のraw/FK検査を省きません。Solverは接触投影と同じguard済みprepared配列を使います。内部型は144→200 byte、公開API/ABIとSDK配布ファイルは不変です。contact専用raw wrapperの部分準備では追加Length/nextafterを行いません。public complete generatorを使うlink専用形状も追加準備を払う点は残ります。

REDは新フィールドを0のまま返し、prepared Avoidをrawへ委譲するstubで取得しました。軸長/外向き境界の契約と近傍sphere solverの準備回数が失敗しています（avoid-prepared-red-{build,test}.log）。GREENはcapsule側面/短い軸/斜め軸、1e-38の軸と1e-39半径/零軸球、root内最初と後続、signed zero、1 ULP分離/通常境界、FLT_MAX、double範囲のoverflow、NaN/零方向の出力保持、反復raw/prepared direction byte一致とexact query回数を検査します。raw経路は保持し、preparedの同値比較だけでなく軸/boundsを独立計算しています。独立read-only監査では、partial準備値がAvoidへ流れないこと、guard radiusと配列index/owner寿命、演算順/atomicityを確認しました。

CPU MSVC19.51 / SDK28000.0のDebug60/60（4.99秒）、Release60/60（4.50秒）が成功。cmake --build build/dev-windows --config Debug/Release --parallel 4、ctest --test-dir build/dev-windows -C Debug/Release --output-on-failureを順に実行しました。Native Win11 build26200 / RTX4070 SUPER driver617.42 / v142 MSVC19.29 / SDK22621.0で、YUMEKA FBX・Silly Dancing/Capoeiraのblend・右腕IK・follow-motion、13鎖・身体8形状・接触線分10本・距離10組、頂点補正OFFを使用しました。球/カプセル半径・曲げ角・許容幅・反復上限は変更していません。

固定delta1/60、開始0秒/3.35秒のDebug/Release各120frame、計480frameのGPU位置/法線とSkirt1314点が全valid。全Skirt記録は変更前と一致し、保存24画像のRGBも変更前/Debugで一致しました。最初の20frameを除くsolver平均はstart4.039862→3.438006ms、loop4.477951→3.846947ms、totalは4.675689→4.065038と5.107559→4.486493ms。参照計算平均349.2/350.8は同じです。布面侵入もstart平均16.008/最大深さ.017313、loop平均15.483/.023409を保持します。独立画像確認では大きな全身破綻は見えませんが、640x480では細かな侵入まで判定できず、品質改善や侵入解消とは扱いません。旧停止611 deltaのDebug/Release再生はcompleted=trueで完走しました。

通常可視Release1280×720・VSyncなし・標準効果・debug/頂点補正OFF、3000frame/warmup300を前後とも完走。152.809→157.854FPS、secondary3.269→2.885ms、p95 7.782→7.436msでした。Draw .392→.450、Present2.313→2.426、IK .512→.518msにも差があるため、全FPS差を今回の効果とは断定しません。300FPSは未達です。反復準備の削減、同値結果と固定/通常計測の処理時間減少を今回の検証範囲とします。

Native commandは前節と同じmodel/motion/configです。GPU環境はTEST_WINDOW_VISIBLE=0、VERIFY_GPU_SKINNING=1、VERIFY_SKIN_POINTS=Skirt、ANIMATION_FRAMES=120、START_SECONDS=0/3.35、STEP_SECONDS=1/60、CAPTURE_START_FRAME=108、CAPTURE_FRAMES=12、PROFILE_SECONDARY_MOTION=1（各名前にGKCORE_、animation/captureはGKCORE_TEST_を付与）。通常benchmarkはGKCORE_BENCHMARK_FRAMES=3000、WARMUP=300のみ。replayはGKCORE_BENCHMARK_DELTA_REPLAY_PATH=<constraint-buffer-green-deltas.txt>、FRAMES=611、WARMUP=0、GKCORE_TEST_WINDOW_VISIBLE=0です。記録はavoid-prepared-{before,after}-benchmark.log、avoid-prepared-{Release,Debug}-{start,loop}.log、avoid-prepared-replay-{Release,Debug}.log、avoid-prepared-image-comparison.json、avoid-prepared-final-{debug,release}-{build,test}.logです。

ゲーム構築用362ファイルをprepare_game_repository.pyで生成し、tests・開発記録・私有assetを除いた独立treeがmanifestと一致しました。Runtimeと5sampleのRelease build、SDK58ファイルのallowlist、更新DLLの外部consumer GPU Init→Present→Shutdownが成功。ログはavoid-prepared-game-{export,build,install,consumer-run}.log、avoid-prepared-{game,sdk}-manifest.jsonです。START CheckOnly、formatter/BOM/CRLF/diffを確認し、最後のtest書式とコメント整理後もDebug/Releaseの接触focusedテストを通しました。

最新Release24画像を画素一致でlossless PNGへ変換し、旧画像・重複Debug・PPM、一時比較script、使用済みexport stagingとPython cacheを片づけました。再帰一括削除は自動チェックに拒否されたため、生成物manifestの絶対pathと個々のSHA-256を照合してファイルを1つずつ削除し、空のfolderだけを除去しました。現在のbuild、元asset、最新PNG、RED/GREEN/nativeログと611 deltaを保持します。比較script本文と削除前hashはavoid-prepared-cleanup.jsonに保存しています。この変更を検証済みの区切りとしてdevとゲーム構築用mainへ反映します。300FPSと布面の品質の完成とは区別します。

## 接触投影の候補配列の容量を保持

接触投影は毎solver passでlocal candidateを確保し、成功時にpositionsへMoveFromしていたため、次passにも新しいcandidateが必要でした。既存SolverWorkspaceにprojectionCandidateを加え、Core入口と投影前でCountを空にして現在のpositionsから複製します。共通kernelの計算順と点/線分/有限性の全検査を保ち、共有版だけ全検査成功後にmemcpyでpositionsへ確定し、candidate.Clearで両bufferを保持します。旧raw/旧prepared wrapperはMoveFromを維持。公開API/ABIとSDKファイルは不変です。通常deltaのlocal workspaceと、接続制約更新内で共有するworkspaceの双方が使います。温まった鎖復元の確保3（候補state2＋投影1）→2（候補state2）、温まった投影の追加確保0です。最終raw接触Checkは固定stack arrayなので、この2回には含みません。

REDは新overloadを旧preparedへ委譲するstubと、旧solverのままwarm確保予算2で失敗を取得しました（contact-workspace-red-{build,test}.log）。GREEN契約は3→5→3点・別目標・signed zero、球/カプセル実接触、raw/旧prepared座標byte一致、cold確保失敗とretry、warm確保0、未収束と次の可行入力の同scratch retry、NaN/不正count/null/shape0 no-opです。positionsとcandidateが同じArrayか、referenceTargetsがcandidateのCapacity全域へ重なる場合は、参照読み取りやClearより先に拒否します。uintptrの大小と差÷sizeofだけを使い、終端addressを加算しません。referenceTargetsがpositionsへ重なる使用は許可。Count=0の容量内参照も拒否して両bufferを保つことを検査しました。

最初の新testはArray比較helperへ生配列を渡すcompile errorと、診断Stringの予約長不足を修正しました。単一球の自由端は可行で、不収束を期待するfixtureとして不適切だったため、既存の2球交互投影fixtureへ変更しました。旧preparedでも同じ失敗・出力不変・同じ診断になることを確認しています。これらを実装のREDとは数えません。独立read-only監査はowner寿命、毎回Clear/Count再構築、容量内参照の事前拒否、successのみcommit、旧wrapper保持、再入後のouter即returnを確認し、重大な問題を見つけませんでした。

CPU MSVC19.51/SDK28000.0のDebug60/60（9.06秒）、Release60/60（5.60秒）が成功。cmake --build build/dev-windows --config Debug/Release --parallel 4、ctest --test-dir build/dev-windows -C Debug/Release --output-on-failureを各構成で実行しました。Native Win11 build26200 / RTX4070 SUPER driver617.42 / v142 MSVC19.29 / SDK22621.0で、YUMEKA FBX・Silly Dancing/Capoeiraのblend＋右腕IK＋follow-motion、13鎖・身体8形状・接触線分10本・距離10組、debug表示/頂点補正OFFを使用。球半径・身体カプセル・最大角度・許容幅・反復上限は保持します。

固定delta1/60、開始0秒/3.35秒のDebug/Release各120frame、計480frameでGPU位置/法線とSkirt1314点が全valid。全Skirt記録は変更前と一致し、保存24画像のRGBも変更前/Debugと一致しました。最初の20frameを除くsolver平均はstart3.442380→3.455005ms、loop3.860263→3.871968ms、totalは4.066850→4.071894と4.488426→4.484344msで、固定deltaの速度改善は確認できません。参照計算349.2/350.8と、布面侵入start平均16.008/最大深さ.017313、loop15.483/.023409も同じです。最新loop画像を実際に開いて全身を確認しましたが、640x480の小さな衣服領域から細かな侵入解消は判断しません。停止再現の611 deltaはDebug/Releaseでcompleted=trueを確認しました。

通常可視Release1280×720・VSyncなし・標準効果、3000frame/warmup300を前後とも完走。161.790→162.787FPS、secondary2.903→2.872ms、p95 7.318→7.257msでした。Draw .387→.389、Present2.319→2.329、IK .514→.497msの変化もあり、差は小さく全FPS差を今回の効果とは断定しません。300FPSと布面の品質は未達です。同じv142 Releaseプロセスで3点投影を各10000回×8batch・計測順を交互にすると、遠方capsuleは旧0.217845/共有0.2026625µs、球接触は旧0.5151275/共有0.49355375µsで結果byte一致。限定した配列管理費用の削減と実モデル全体の性能を分けて扱います。

Native argsは前節と同じmodel/motion/config。GPUはGKCORE_TEST_WINDOW_VISIBLE=0、GKCORE_VERIFY_GPU_SKINNING=1、GKCORE_VERIFY_SKIN_POINTS=Skirt、GKCORE_TEST_ANIMATION_FRAMES=120、GKCORE_TEST_ANIMATION_START_SECONDS=0/3.35、GKCORE_TEST_ANIMATION_STEP_SECONDS=1/60、GKCORE_TEST_CAPTURE_START_FRAME=108、GKCORE_TEST_CAPTURE_FRAMES=12、GKCORE_PROFILE_SECONDARY_MOTION=1。通常benchmarkはGKCORE_BENCHMARK_FRAMES=3000、GKCORE_BENCHMARK_WARMUP=300のみ。replayはGKCORE_BENCHMARK_DELTA_REPLAY_PATH=<constraint-buffer-green-deltas.txt>、FRAMES=611、WARMUP=0、TEST_WINDOW_VISIBLE=0です。記録はcontact-workspace-{before,after}-benchmark.log、contact-workspace-{Release,Debug}-{start,loop}.log、contact-workspace-replay-{Release,Debug}.log、contact-workspace-image-comparison.json、contact-workspace-probe-{configure,build,results}.log、contact-workspace-final-{debug,release}-{build,test}.logです。

ゲーム構築用362ファイルをprepare_game_repository.pyで生成し、独立したmain treeの内容がmanifestと一致しました。Runtimeと5sampleのRelease build、SDK58ファイルのallowlist、更新DLLの外部consumer GPU Init→Present→Shutdownが成功。START CheckOnly、formatter/BOM/CRLF/diffも確認済みです。記録はcontact-workspace-game-{export,build,install,consumer-run}.log、contact-workspace-{game,sdk}-manifest.jsonとcontact-workspace-source-format.jsonです。

最新Release24画像を画素一致のlossless PNGとして保持し、旧画像・PPM・重複Debug、一時probe source/build、比較script、使用済みexport stagingとPython cacheを片づけました。生成物の絶対pathと個々のSHA-256を照合してファイルを個別削除し、空folderだけ除去しています。元asset・現Native/CPU/game build・検証ログ・611 deltaを保持。比較/probe source本文と削除前hashはcontact-workspace-cleanup.jsonへ保存しました。この検証済みsliceをdevとゲーム構築用mainへ反映します。次は更新内で変わらない参照線分の再計算費用を調べます。


## 衣服位置に合わせた骨接触と実モデルの連続再生

2026-10-13のユーザー指示により、細かなFPS改善を中断し、実モデルのanimation・blend・IK・揺れ・身体接触を組み合わせた動作へ作業を切り替えました。参照線分cacheの未検証案は現ソースへ含めず、調査記録として分離しています。今回は速度改善を成果に含めません。

FModelSecondaryMotionContactLinkとSetModelSecondaryMotionContactLinksを追加しました。骨ローカルoffsetの球・線分を登録し、取付boneのpivotとscaleを保持したまま後続boneの方向を変えて身体形状から離します。方向は節iからi+1へ対応させ、最後は実際の描画FK行列の端点で全接触を検査します。設定、clone、instance分離、clear、reset、allocation失敗、debug出力、CLI読み込みを検査しました。頂点接触はOFFで、Runtimeがskin頂点を押し出す方式ではありません。診断と設定生成のために開発用bind情報を読む処理は、Runtime SDKへ含めません。

接触点評価の未実装契約でREDを取得後、最小回転と子位置に対する微分を実装し、独立な有限差分・既知の90度回転と照合しました。一次勾配が0でも回転で退避できる入力のREDを追加し、幾何的な脱出を実装しました。実機loop frame81の入力では全量補正後の骨長・曲げ角復元が接触を悪化させるため、補正後に全身体形状の実侵入を測り、改善する縮小候補だけ採用します。frame115と別motion frame30の局所停止入力も単体fixtureに保存し、許容角内の方向候補を探索する補正でGREENになりました。各fixtureには独立方向samplingで得た可行姿勢を厳密照会する前提検査もあります。失敗を成功扱いする経路は追加していません。

229本の2骨capsule試験設定は、個々の接触を解けても後続の接触が同じ骨を動かして侵入を戻し、32回の全体反復が収束しないため採用しません。捕捉記録ではcontact18のown-projection後0、all-projections後0.011387 model unitの侵入へ戻りました。設定生成toolへ主skin寄与骨の球を作るmodeを追加し、weightを偽装せず残りの寄与を記録します。採用設定は355観測点から179球を作ったものです。他boneの寄与やpose変化を包む保証はなく、mesh全体の非貫通とは区別します。

Windows 11 Pro build26200、RTX4070 SUPER / driver617.42（32.0.16.1742）、Runtime v142 / MSVC19.29、SDK10.0.22621.0で実行しました。YUMEKAへSilly Dancing・Capoeiraをexternal-blend-ikで適用し、13鎖、8身体形状、骨原点link10本、距離10組、179球、頂点接触OFFで0秒・3.35秒から各120frameをDebug/Release再生、480frameのGPU位置・法線、実skin1,314点が有効でした。点群はJacket443・Skirt440・Vest_Shirt431へ分類し、全点集計も保持しています。保存最後frameと全mesh集計はDebug/Releaseで一致しました。Skirt平均侵入点数は開始3.583→3.017、loop2.450→2.200ですが、loop最大侵入数は8→9、最大深さ0.023409は残り、品質合格とは判定しません。Vest_Shirtの固定骨寄与部分にも侵入が残ります。

採用設定で以前の停止再現611 deltaをDebug/Release再生し、双方framesCompleted=611 / completed=trueを確認しました。Skinning Test・Swingingでもanimation・blend・IK・13鎖の揺れを身体接触なしでDebug/Release各120frame、計240frameのGPU位置・法線を検証しました。このmotionへ身体接触を加えた試験はframe30でSkirt_1の接触が失敗し、既知の残件です。接触なしの試験を身体接触の成功とは扱いません。START16は179球の接触表示、START17はSkinning Test・Swingingと揺れの接触なし再生です。Cで形状表示を切り替え、Rで揺れをresetできます。

Native commandはgkcore_real_model_capture_tests.exe <YUMEKA FBX> 1.1166145684 0 .671678712 -.2517482195 external-blend-ik <Silly Dancing.fbx> <Capoeira.fbx> --follow-motion --secondary-motion samples/config/yumeka-bone-contact-motion.txt --secondary-colliders samples/config/yumeka-bone-colliders.txt --secondary-links samples/config/yumeka-secondary-links.txt --secondary-distances samples/config/yumeka-secondary-distances.txt --secondary-contact-links samples/config/yumeka-secondary-contact-links.txt --materials <material-config.txt>。GKCORE_TEST_WINDOW_VISIBLE=0、GKCORE_VERIFY_GPU_SKINNING=1、GKCORE_VERIFY_SKIN_POINTS=Skirt、GKCORE_SKIN_POINT_MESH_SUMMARY=1、GKCORE_TEST_ANIMATION_FRAMES=120、START_SECONDS=0/3.35、STEP_SECONDS=1/60、CAPTURE_START_FRAME=119、CAPTURE_FRAMES=1です（後半5項目はGKCORE_TEST_ANIMATION_またはGKCORE_TEST_CAPTURE_を付与）。611再生はGKCORE_BENCHMARK_DELTA_REPLAY_PATH=constraint-buffer-green-deltas.txt、FRAMES=611、WARMUP=0、TEST_WINDOW_VISIBLE=0です。

最終CPU Debug62/62（6.54秒）・Release62/62（5.18秒）成功、設定生成の8契約も含みます。実installで新header漏れのSDK REDを捕捉し、install対象へ追加後59ファイルがRuntime allowlistに一致しました。ゲーム構築用367ファイルからRuntimeと5sampleをビルドし、新APIをリンクする外部consumerでGPU Init・Present・Shutdownが成功。tests、開発記録、私有assetはゲーム用mainへ含めません。実機の高解像度画像で179球と脚capsuleの配置、通常表示を確認し、lossless PNGのpixel一致も検査しました。コメントは日本語、BOM/CRLF/formatter、START CheckOnlyを確認しています。

記録はcontact-links-final-{debug,release}-{build,test}.log、contact-links-final-replay-{debug,release}.log、contact-links-functional-comparison.json、contact-links-quality-images.json、contact-links-{game,sdk}-manifest.json、contact-links-consumer-run.logです。次は複数接触・骨長・曲げ角を同時に満たせず停止する条件と、実際の布面侵入を改善します。細かなFPS計測へは戻りません。

## 身体接触付きの外部motionを連続再生するための鎖補正と形状選択

2026-10-13、Skinning Test・SwingingをYUMEKAへblend + IKで適用した停止入力を調べました。frame30の末端、frame98の許容幅境界、frame33の親位置も動かす必要がある接触を数値fixtureへ保存し、REDを取得しました。固定長の節方向で複数身体形状を同時に避ける計算と、全節の方向を変数にする鎖全体の有限探索を追加しました。候補は骨長・曲げ角を保って再構成し、点・線分の接触をすべて満たす場合だけ採用します。失敗時は状態を確定しません。

計算中の1.001倍の半径guardで固定rootの内外分類が元形状と変わる問題もREDで捕捉しました。準備済み形状へguard前の接触閾値を保持し、投影・方向回避・鎖探索・検査を同じroot例外へ揃えました。浅い外側rootでは、元閾値からrootまでの空きを越えない補正余白を使います。通常shapeや後続節の判定は維持しています。

長い再生では、同じ固定rootの60度制限内で腰の点接触と脚の線分接触が両立しないframe433を確認しました。263,168方向の等角格子と最近格子からの移動距離上限で調べ、通常の元形状許容幅でも全方向に少なくとも約0.0004477 model unitの侵入が残る数値証明を保存しました。90度には全8形状を離れる第一節の候補があります。単体fixtureは60度で失敗と状態保持、90度で全接触・長さ・角度の成功を検査します。未検証の許容幅緩和案は採用していません。

90度設定でも全身体形状を全鎖に適用した3,000frame試験は2,627frameで停止しました。そこでSetModelSecondaryMotionChainColliderMaskを追加しました。登録順の64bitで各鎖の節球・隣接線分が接触する身体を選び、0は身体接触なし、既定は全形状です。未使用上位bitは無視します。初期更新、鎖復元、最終の描画FK検査で同じ選択を使い、二重半径拡張を避けます。骨原点間link・offset接触link・頂点接触の判定対象は変更しません。clone・同root再登録・resetで設定を保持し、clearで破棄します。maskだけの変更でanimation基準姿勢cacheは破棄しません。

mask Getの未実装契約によるbuild REDと、古いinstall済みSDKに新APIがないconsumer REDを記録しました。bit63、mask0、異なるmaskのcache分離、上位bitの正規化、既定経路の準備回数、無効入力、失敗時のview保持を検査しています。APIでは巨大球による不成立、mask変更による更新の可否、無効handle/root、状態保持、ライフサイクルを検査しました。設定の12個目は厳密な十進uint64で、符号・小数・桁あふれ・余剰列を拒否し、9/10/11列の旧形式を保ちます。

START18は13鎖と8身体形状、頂点接触OFFです。スカートは半径0.038・最大角90度・反復32・mask240で脚の配列index4〜7だけを判定し、髪・尻尾は全身体形状を使います。旧presetの60度値や身体の半径は変更していません。179offset球、骨原点間link、距離補助はこのメニューには指定しません。髪とスカートで接触対象を分けた動作確認であり、衣服の面全体の接触成功とは区別します。

Windows 11 Pro build26200、RTX4070 SUPER / driver617.42、Runtime v142 / MSVC19.29、Windows SDK10.0.22621.0で実行しました。START18のSkinning Test・Swingingは1/60秒の3,000 deltaをDebug/Releaseで再生し、双方framesCompleted=3000 / completed=trueです。さらに0秒・3.35秒から各120frameを両構成で再生し、START16のSilly Dancing・Capoeiraも同じ区間で確認しました。計960frameのGPU位置・法線と実skin1,314点が有効で、4組の最後の画像と全mesh集計がDebug/Releaseで一致しています。1314点はJacket443・Skirt440・Vest_Shirt431へ分類し、全8身体形状に対する集計を保持しました。

START18の開始区間は、Jacket平均侵入10.308点・Skirt35.817点・Vest_Shirt41.992点、最大深度はそれぞれ0.067606・0.073506・0.076412 model unitでした。接触選択で連続動作は成立しましたが、元の全身体形状に対する布面の品質改善とは判定しません。高解像度の通常表示も目視し、lossless PNGの画素一致を確認しました。179球をSkinning Test・Swingingへ重ねる別構成の未収束は残ります。次は鎖とoffset接触の同時解決、実際の布面侵入を扱います。FPSの小さな改善には戻りません。

最終CPU Debug62/62（6.11秒）、Release62/62（5.45秒）成功。ゲーム構築用371ファイルのRuntimeと5sampleをRelease buildし、install済みSDK59ファイルのallowlistと、新mask APIをリンクする外部consumerのGPU Init→Present→Shutdownが成功しました。START CheckOnly、独立レビュー、formatter、BOM/CRLF、diff検査も実施しています。mainにはtests・私有asset・開発記録を含めません。

Native commandはgkcore_model_benchmark.exe <YUMEKA FBX> 1.1166145684 0 .671678712 -.2517482195 external-blend-ik <Skinning Test.fbx> <Swinging.fbx> --follow-motion --secondary-motion samples/config/yumeka-skinning-contact-motion.txt --secondary-colliders samples/config/yumeka-bone-colliders.txt --materials <material-config.txt>。GKCORE_TEST_WINDOW_VISIBLE=0、GKCORE_BENCHMARK_FRAMES=3000、GKCORE_BENCHMARK_WARMUP=0、GKCORE_BENCHMARK_DELTA_REPLAY_PATH=chain-joint-fixed-3000-deltas.txtです。描画検査は同じ引数へGKCORE_VERIFY_GPU_SKINNING=1、GKCORE_VERIFY_SKIN_POINTS=Skirt、GKCORE_SKIN_POINT_MESH_SUMMARY=1を付け、120frame・step1/60秒・開始0/3.35秒を指定しました。

記録はchain-contact-{skin,skin-frame98,skin-frame33,skin-frame433}-fixture.json、chain-contact-frame433-{cone-certificate,90deg-witness}.json、chain-collider-mask-{red-build,green-test,sdk-red-build}.log、chain-mask-skin-3000-{debug,release}.log、chain-joint-functional-comparison.json、chain-mask-final-cpu-{debug,release}-test.log、chain-mask-{game,sdk}-manifest.json、chain-mask-consumer-run.logです。一時調査source、PPM、重複Debug画像、使用済みexport staging、重複したconsumerのbinコピーなど、生成809ファイルと空folder80個を片づけました。元asset・現在のbuild・最新PNGと数値記録は保持しています。調査source本文はchain-mask-cleanup.jsonへ保存し、削除前の絶対pathとSHA-256を個別照合しました。

## 衣服位置の接触と鎖全体の姿勢を同時に解く

2026-10-13、START18へ179個のbone-local offset球を重ねた構成は、固定1/60秒の再生で2frame後に停止しました。同じ骨へ複数の接触が付き、個々の回転投影と骨長・身体接触の復元を順番に行うと、先の接触を後の処理が崩す入力です。異なる方向の二つのoffset球、親の回転が必要な接触、同じboneの異なる端点を持つcapsuleを契約テストにし、offsetを無視する旧契約でREDを取得しました。

同じboneに両端を付けた接触だけを使い、骨原点間link・距離制約を併用しない場合、対象鎖の全節方向と各offset接触をまとめて探索します。各節の単位方向から固定root・基準骨長・最大曲げ角を保つ位置列を再構成し、身体の節球・線分とすべての添付接触の侵入量を同時に減らします。offsetには最小回転による位置の変化率、祖先方向には両端点の平行移動の変化率を使います。候補探索は有限で、数値失敗・確保失敗と候補なしを区別します。正反対方向で回転が一意にならない候補は採用しません。既に接触を満たした探索初期値が、勾配0のため捨てられていた処理も修正しました。

解は全対象鎖を一時配列へ作ってからまとめて反映し、float保存後の骨長・曲げ角・身体・offset接触を検査します。骨ローカルtranslationとscale、固定rootを変えず、自由boneのmodel空間pivotは祖先の回転によって移動できます。offsetは鎖maskの対象にせず、接触半径だけを加えた全身体形状を使います。候補のoffset接触は従来と同じ許容幅の半分、描画FKでは全幅を使います。単一接触では成功する従来のpivot固定投影を維持し、解けない場合だけ鎖全体へ切り替えます。cross-chainの端点や骨原点間link・距離補助を含む構成は既存の連成経路です。

実際の停止入力から5点・4節・17接触を取り出し、独立した400,000候補の方向探索で、骨長・角度・すべての身体と添付接触を満たす姿勢が存在することを確認しました。テスト専用の数値fixtureへ入力と成立姿勢を保存し、同じ入力をRuntime解法で解いた後、独立した点・線分距離と最小回転の計算で再検査します。fixtureにはmesh、texture、animationデータやbone名を含めず、mainには配布しません。親が必要な単一接触はrig統合テストでも、元のlocal translationを保ち、実際の描画FKで接触が成立することを確認しました。巨大な不成立形状、実際の確保失敗、NaN形状、segment範囲外、定義の角度不一致は失敗と入力保持を検査します。

START18へ179球を追加しました。13鎖・身体8形状、スカートの最大角90度・節半径0.038・32反復・mask240、髪と尻尾の全身体接触は前回と同じです。骨原点間link・距離補助・頂点接触は設定しません。Windows 11 Pro build26200、RTX4070 SUPER / driver617.42、Runtime v142 / MSVC19.29、Windows SDK10.0.22621.0で、固定1/60秒の3,000frameをDebug/Releaseとも完走しました。開始0秒・3.35秒の各120frameをbody-only対照と179球付きの両構成で描画し、Debug/Release計960frameのGPU位置・法線と実skin集計が有効でした。全mesh集計と保存4組の画像は構成間で一致しました。

比較は同じ1,314点（Jacket443・Skirt440・Vest_Shirt431）を全8身体形状で検査し、鎖maskで集計を絞りません。開始区間の平均侵入点数はJacket10.308→0.117、Skirt35.817→14.950、Vest_Shirt41.992→18.050、最大深度は0.067606→0.009331、0.073506→0.046561、0.076412→0.053237 model unitでした。loop付近は1.900→0.242、22.525→15.150、22.075→14.875、最大深度0.046666→0.011510、0.065772→0.048603、0.067106→0.049902です。実際の布面侵入は減りましたが、残っています。2560×1920の側面画像も目視し、連続動作と形状の非貫通を同一視しません。

START16の従来のSilly Dancing・Capoeira構成も、開始0秒・3.35秒の各120frameをDebug/Releaseで再生し、計480frameが有効でした。保存2組の画像と全mesh記録が構成間で一致し、前回Releaseの全mesh記録とも一致しています。高解像度のSTART18の120frameは別に記録しました。GPU実行を直列化し、検証ウィンドウは非表示にしています。

Native commandはgkcore_model_benchmark.exe <YUMEKA FBX> 1.1166145684 0 .671678712 -.2517482195 external-blend-ik <Skinning Test.fbx> <Swinging.fbx> --follow-motion --secondary-motion samples/config/yumeka-skinning-contact-motion.txt --secondary-colliders samples/config/yumeka-bone-colliders.txt --secondary-contact-links samples/config/yumeka-secondary-contact-links.txt --materials <material-config.txt>。GKCORE_TEST_WINDOW_VISIBLE=0、GKCORE_BENCHMARK_FRAMES=3000、GKCORE_BENCHMARK_WARMUP=0、GKCORE_BENCHMARK_DELTA_REPLAY_PATH=chain-joint-fixed-3000-deltas.txtです。描画検査は同じ引数へGPU skinと実skin集計の検査を付け、step1/60秒・開始0/3.35秒・120frameを指定しました。

独立した線分距離検査を固定4回の交互投影から、内部停留点と四つの端点境界の最小値へ変更しました。ほぼ平行な場合は外積から分母とパラメータを求め、大きな二乗値の差による桁落ちを避けます。交差・斜交・平行・浅い交角の内部最短点と端点最短点を別契約で確認しました。最終CPU Debug63/63（5.50秒）、Release63/63（5.02秒）成功です。独立したread-onlyレビュー、formatterのbyte照合、UTF-8 BOM/CRLF、noSTLと相対includeの監査、START CheckOnly、diff検査も通過しています。

固定deltaだけでなく、通常の実時間deltaでも同じ179球付き構成をDebug/Release各3,000frame完走しました。delta traceは更新中にメモリーへ保持して終了時だけ書き出し、双方deltasRecorded=3000、completed=true、traceWritten=true、exit0でした。この確認は全てのmotion・delta・配置での収束保証ではありません。GPU出力を直列化し、micro FPS変更はしていません。

ゲーム構築用372ファイルのRuntimeと5sampleをRelease buildしました。install済みSDKは59ファイルで許可リストに一致し、そのSDKをリンクした外部consumerがGPU Init・描画・Present・Shutdownを完了しました。consumerへコピーした全RuntimeファイルのSHA-256もinstall出力と一致しています。mainにはtests、数値fixture、私有asset、モデル固有の接触設定や開発記録を含めません。

記録はjoint-attachment-contract-red-{build,test}.log、joint-attachment-native-contact1-{chain,witness}.json、joint-attachment-native-fixtures.jsonl、joint-attachment-final-cpu-{debug,release}-{build,test}.log、joint-attachment-masked-debug-3000.log、joint-attachment-final-release-3000.log、joint-attachment-realtime-{debug,release}-3000.logとdeltas.txt、joint-attachment-functional-comparison.json、joint-attachment-legacy-regression.json、joint-attachment-skin-comparison.json、joint-attachment-{game,sdk}-manifest.json、joint-attachment-consumer-run.log、joint-attachment-source-audit.jsonです。使用済みexport staging、一時調査sourceとcapture harness、PPMとPython cacheの生成400ファイル、空folder48個を片づけました。削除対象は生成物の絶対path・SHA-256を個別に照合し、調査source本文はjoint-attachment-cleanup.jsonへ保存しました。元asset・現在のbuild・最新PNGと数値記録は保持しています。次は複数boneのskin寄与と衣服幅を考慮し、骨側の接触形状が支えられていない布面の侵入を改善します。

## 複数boneの影響を受ける衣服位置へ接触球を置く

2026-10-13、START18の最深侵入を開始frame51・loop frame87で再現しました。開始のSkirt点31867はSkirt_2_R.002 / Skirt_1.002 / Skirt_2_R.003のweight約0.6158 / 0.3494 / 0.0348だけで、Rootや身体boneの寄与はありません。各骨の変換点と骨球が身体capsuleの外にあっても、その混合位置は右大腿の中心付近へ入り、深さ0.04656になりました。loopの点32086も2本の自由骨だけで同じ不足を再現しました。GPU skin mismatchと固定waistだけを原因とは扱いません。

FModelSecondaryMotionContactAnchorとFModelSecondaryMotionContactPoint、SetModelSecondaryMotionContactPointsを追加しました。骨番号・local offset・実weightをflat配列へコピーし、複数骨の変換位置から接触球の中心を作ります。出力頂点は書き換えません。元の骨長・曲げ角と身体の骨球/線分、既存のsame-bone offset接触と重み付き球を同時に解き、float保存後の全制約と実描画FKを通した候補だけを確定します。Rootの位置は固定、Root上のoffsetは回転で動きます。登録鎖の枝外のboneは固定FK寄与、未登録の鎖子孫は拒否します。候補なし・invalid・numeric・確保失敗を区別し、失敗は前の状態を保持します。

公開APIは最大1024点・全16384寄与、一つの球は1〜64寄与、正の有限weightの合計1±64FLT_EPSILON、有限offset・非負半径、全寄与を重複なく一度参照する範囲を受け付けます。weightを正規化したり残りの骨を捨てたりしません。origin link / distanceと異なるboneを結ぶ従来offset linkは併用拒否です。clone・reset・同root再登録・instance終了のライフサイクルを検査しています。DebugShapesにも同じ描画FKの重み付き球を追加しました。

設定生成は同じbone/weight signatureだけをまとめ、各bone-local代表位置と偏差の重み付き上界を半径にします。骨の線形変換の最大伸長が指定上限以下という条件付きの包絡であり、weightを丸めてグループを統合しません。元1314点から自由なSkirt子骨のweightが99%以上の468点を選び、465球・1365寄与を作りました。除外846点は固定Root/身体などの混合が大きい点です。三角形内部の非貫通や全衣服の包絡を保証するものではありません。Viewerへ--secondary-contact-pointsと設定ファイルの読み込みを追加しました。

REDでは、両boneの点が身体球外でも混合位置だけが内側になる2鎖の契約を使いました。3寄与、親方向を必要とする2節・非平行offset、Root zero-offset、固定寄与のみのclear/不成立、180度反平行の初期方向、並べ替え、NaN/weight/iteration/確保失敗・atomic性を検査します。設定時validatorが未使用/重複rangeを認めて更新時だけ拒否する不一致をREDで捕捉し、共通validatorへ委譲しました。Public headerを単独先頭でincludeしたcompile REDから前方宣言を補いました。positive deltaの実API更新と、same-bone旧contactとの併用も実行しています。

Runtimeで接触球を1点の鎖として検査してしまう誤りを修正し、半幅候補/全幅描画FKをFreeSegmentContacts(center, center)で検査します。複数回のline searchで勾配配列のCountが0になる不具合は半径0.99の独立可行fixtureでREDを得て、毎回全方向数のgradient要素を構築する処理へ修正しました。直接helperへNaN速度・前回状態なし・delta0を渡すREDも保存し、teleportや時間0の分岐を含む有限速度検査を追加しました。

同じ1314点と全8身体形状で、元179球と新465重み付き球をReleaseの開始0秒・3.35秒から各120frameで比較しました。Skirt平均侵入14.950→1.692、15.150→1.800、最大深度0.046561→0.035908、0.048603→0.028556 model unit。Vest_Shirt平均18.050→10.592、14.875→9.350、最大深度0.053237→0.028842、0.049902→0.019126です。Jacketの開始最大深度は0.009331→0.012260へ増えており、全衣服の完全改善とは扱いません。loopのJacket侵入点数は0でした。選別した点数で検査対象を絞らず、全meshの記録を保持しています。Bodyは近似capsuleで、実body mesh内部の深度そのものではありません。


全体探索で接触に無関係な髪・尾の鎖まで基準方向へ傾け、補正速度を注入する不足を修正しました。重み付き点が参照する鎖と、初期の身体・既存offset接触が未解決の鎖だけをseedの対象にし、それ以外は現在方向を保持します。接触勾配が0になる対称配置も、鎖ごとの侵入量で対象へ含めます。旧exportのMathを最新テストへリンクした対照では、第三の無関係鎖が動く契約がREDとなり、新しい実装でGREENです。

微小な侵入にも一律20度から補正候補を試す不足を、正規化前の勾配と侵入量から初期角を選ぶ処理へ修正しました。上限0.35 rad・既存の最小line角を保ち、非線形の候補は厳密な侵入量減少でのみ採用します。半径0.001の球へ微小に侵入した独立fixtureは、近くにclear姿勢があり、補正後の移動量0.001未満・root・骨長・角度・接触を要求します。旧export Mathは過大移動でRED、新実装はGREENです。共通勾配とPR+の探索、同一反復内の通常勾配への再開も有限回で行います。

初期化済みで同じ骨列・teleportでない場合に限り、候補なしの後に前回の確定方向から現在root・骨長へ構築した姿勢を再試行します。半径0.99の対向二鎖・反復1の合成fixtureでは、前回状態なしはatomicに失敗し、前回+Z方向を与えると現在root/長さを保って成功します。delta0.25、骨列変更、teleport、NaN/確保失敗は再試行対象から外します。

90度では701/727/728付近の接触競合が残り、小さい骨球0.003だけでも1,016frameで未解決でした。骨球0.038と脚mask240、身体8形状、接触465球の値を保ち、脚上げで折り返せる最大角度180度へ設定した構成はRelease・Debugの固定1/60秒・3,000frameを完走しました。影響範囲を絞った補正のRelease実時間3,000frameも完走しています。個々の鎖のbody予測を常に省く試作は悪化したため採用していません。

Debug実時間の151 deltaを保存し、Releaseでも150番目にHair root39/span4の身体接触が停止することを再現しました。Hair角度90度だけでも同じ停止が残りました。個々のbody予測の幾何学的未解決をbool statusで区別し、この失敗だけ身体なしの予測から後段の全鎖接触へ渡す処理を追加しました。同じ151 deltaはReleaseで完走しました。入力・数値・確保・float保存の失敗は再試行しません。不可行body、NaN delta、負radius、割当失敗のstatus/atomic契約をDebug/Releaseで実行し、テストtargetのGKCORE_TESTINGも有効にしました。全体solverとfloat/FK/body/weightedの検査を通った候補だけを確定します。

現行180度構成の開始0秒・3.35秒をDebug/Release各120frame、脚上げ16.8秒と従来dance構成をRelease各120frame、計720frameでGPU変形を確認しました。位置45,233・法線55,747のreadbackは位置誤差0、法線最大誤差1.78814e-7で、許容値0.0005以内です。固定2区間の全mesh集計と保存画像はDebug/Releaseで一致しました。現行Skirt平均侵入は3.350/2.675、最大深度0.039625/0.028653です。旧179球・90度構成の14.950/15.150、0.046561/0.048603から軽減しました。Vest_Shirt平均13.400/9.808・最大深度0.035777/0.025264、Jacket開始平均0.267・最大深度0.011524で、Jacket開始は旧構成より増加しています。布面全体の完全改善とは扱いません。

Windows 11 Pro x64 build26200、RTX4070 SUPER / driver617.42、Visual Studio2026のv142/MSVC19.29.30159、WindowsSDK10.0.22621.0でnative実行。CPU構成はMSVC19.51/SDK10.0.28000.0でDebug/Release各65/65成功。記録はbuild/native-validation/skin-coverage-complete-*-test.log、skin-coverage-final-quality.json、skin-coverage-prediction-retry-replay-151.log等に保存。main用ソース381ファイルを生成し、ReleaseのRuntimeとゲーム用5サンプルをビルド、install SDK61ファイルの許可リストと公開新header先頭include・setterのリンクを確認しています。

最終の幾何未解決再試行を含むDebug実時間3,000frameも完走しました。deltaを終了時に一括保存し、固定時間再生とは分けて記録しています（skin-coverage-prediction-retry-debug-real-3000.log、同debug-deltas.txt）。source graph・anchor ownership・body status・raw gradient・seed locality・OOM atomicの独立read-onlyレビューも完了し、P1/P2なしです。

最終ソースではDebug/Releaseの固定各3,000frame、Release/Debugの実時間各3,000frame、保存151deltaを完走。Release実時間は95.079FPS、p95 10.628msで、300FPSは未達です。インストール済みSDK61ファイルだけのconsumer GPU smokeは公開APIリンク、Init、Scene/UI描画、Present、Shutdownまでexit0。完了要約はskin-coverage-completion.json。調査用source/probe build・重複export・PPM等の生成物1,874ファイル約87MBをhashと絶対path確認後に整理し、元asset・現在build・最新ログ/数値/PNGを保持しました。

## スカートの姿勢跳びと接触による人工速度を取り除く

2026-10-13、ユーザーの実画面の指摘から連続性を再検査しました。旧devの600frameではスカート30線分の世界方向が1frameで最大141.238度変わり、固定rootの輪から推定した身体回転を差し引いても最大158.511度、45度超309回・90度超25回でした。停止せず登録接触を満たすことだけでは、見た目の正常動作を判定できません。

重み付き接触の補正量をsubstepで割って速度へ足す処理を、補正に逆行する予測速度成分だけを除く処理へ変更しました。静止・速度0のfixtureで旧処理の速度186.676をREDで確認し、接線速度の保持と速度増大の禁止も検査します。身体の鎖solverも、押し出し後位置と旧位置の差から速度を作る方式を見直しました。1/120秒の静止接触は位置補正0.687071を要しますが、新しい速度は0です。同じ補正を旧位置差方式で速度化すると82.4485になります。root移動、骨長の伸縮、動く球との相対速度、静止・移動する角度境界も契約化しました。動く基準方向は正規化の変化率で計算し、cone速度のREDを修正しています。近接しているだけの身体と固定root付近の線分が、不要な大きな速度を作らない契約も追加しました。速度反応は有限回の投影であり、全接触の速度制約の厳密な同時解を保証するものではありません。

方向探索のseedは現在方向を基準とし、接触を満たす候補を現在方向へ縮約します。保存後の骨長・角度・身体・重み付き接触を検査した候補だけを採用し、縮約できない場合も元の合法候補を保持します。遠い参照方向へ直帰する身体方向探索は、現在方向からの局所改善を先に試すよう変更しました。回転した姿勢の近接侵入と、近い方向があるのに遠い参照方向へ戻る反例を旧処理へ適用してREDを取得しています。数値・確保失敗では一部の点だけを更新しません。

通常の重み付き接触は、前回の状態と現在のanimation目標から既存の鎖solverで作った物理予測を先に使います。個別の身体押し出しを予測へ混ぜず、身体と衣服の接触は全鎖solverが一括で所有します。helperへ別の候補を渡す場合も、positive deltaで有効な前回状態があれば物理予測を優先します。isolated cold-first反例は正しい骨長のfixtureで距離5.85410082の遠い候補を選びREDとなり、現行テストはGREENです。時間0は従来の再試行、hitch・teleport・骨列不一致は冷間処理を保持します。入力・数値・確保失敗は再試行しません。

修正途中の600frameでは身体相対45度超が31回まで減りましたが、大きな跳ねはframe146/292/438/584へ集中しました。Swingingの長さ2.43333333秒に対応するloop境界で、終端と先頭の姿勢が連続ではありません。サンプルに--ping-pong-motionを加え、各clipを実際の長さで独立に往復再生します。元のassetとRuntimeの通常loopは変更しません。Mはanimation位相を止め、揺れは落ち着くまで更新します。Rは時刻0を設定してから揺れをresetし、そのframeは時間を進めず接触を解きます。有限値・境界・巨大duration・option重複/値引数/失敗時不変の契約を実行しました。

clear後も候補を比較し続ける試作は実時間1,000frameを完走したものの、delta平均0.160508秒・250ms以上114回で不採用です。保存後も合法な最初の近い候補を得たら探索を終了する現行構成の600frameは、身体相対最大29.396度、p99 11.072度、45/90度超とも0回でした。元のclipをloopする旧構成との比較には再生方式の変更も含み、Runtimeだけの効果とは扱いません。連続16画像も目視しました。Release固定1/60秒3,000frameと実時間600frameを完走しています。実時間の内訳記録ありでは31.025FPS、p95 31.645ms、揺れ更新26.152msで、300FPSは未達です。

Windows11Pro build26200、RTX4070SUPER driver617.42、v142/MSVC19.29・SDK10.0.22621.0で、往復再生の開始0/2.1秒をDebug/Release各120frame、計480frame実行しました。GPU位置45,233・法線55,747の比較は位置誤差0、法線最大1.78814e-7、許容0.0005以内です。全mesh記録と各区間の保存画像はDebug/Releaseで一致しました。Skirt平均侵入は5.517/2.992、最大深度0.039969/0.018488で、布面侵入は残ります。body capsuleとの近似距離であり、実身体mesh面の完全な非貫通ではありません。

最終CPU構成はMSVC19.51/SDK10.0.28000.0のDebug/Release各65/65成功です。main用383ファイルを生成し、Runtimeとゲーム用5サンプルをビルド、install SDK61ファイルを検査し、同じDLLを使うconsumerのGPU描画もexit0でした。exampleの新しいpaired sourceがexportから漏れる反例を捕捉し、CMakeとincludeが参照する配布依存物の検査を追加しました。独立read-onlyレビューとBOM/CRLF/noSTL監査も実施しています。数値・画像・コマンドの記録はbuild/native-validation/skirt-stability-*、使用済み調査sourceと片づけ対象はskirt-stability-cleanup.jsonへ保存します。

## 先頭の骨に影響されるスカート位置も接触で支える

2026-10-13、開始区間の最深侵入点32259はスカートmesh内410番で、先頭の骨の寄与0.880453、子骨0、固定した腰0.119547でした。骨原点の球は点から0.0661/0.0811離れて接触せず、衣服の混合位置が腰sphereへ0.039969入り込んでいました。既存fitterは子骨の寄与だけを数えるため、この点を接触から外していました。登録鎖の先頭位置は固定でも、bone-local offsetは子方向とともに回転するRuntimeの既存機能を使います。Runtimeの位置更新や元のFBX・skin weightは変更していません。

fit_contact_pointsにinclude_root_offsets=FalseとCLI --include-root-offsetsを追加しました。opt-inでは同じboneの正weightを集約してから、非ゼロのroot offsetのweightを子骨のweightへ加えます。重心offsetが打ち消されてゼロになるrootは数えず、固定骨の寄与も元weightのまま出力します。同bone/weight signatureだけをまとめ、偏差半径の上界を保ちます。root/child/fixed混合の独立回転、ゼロ/相殺offset、group/name矛盾、順序反転、ID選択とCLIの契約を先行追加し、旧実装でRED、現行9件GREENを確認しました。fit報告には移動寄与閾値・最大偏差・最小半径も記録します。

native bind出力へmesh名・node番号・segment番号・mesh内位置IDを加え、実出力に識別子がないREDから修正しました。新dumpはJacket443、Skirt440、Vest431の計1,314点と一意IDを検査しています。Skirt全440点と旧Vest101点のIDを指定し、移動寄与0.5以上でSkirt431+Vest101の532点を529球・1,574 anchorsへまとめました。旧465行は半径・weight・offsetまで全件同一で、新しくSkirt64球だけを追加しています。root主体0.88、隣のroot0.003、固定腰0.117のRuntime Math回帰も、root固定・offset回転・骨長・角度・float保存後の接触を確認しました。

Win11Pro build26200、RTX4070SUPER driver617.42、v142/MSVC19.29、SDK10.0.22621.0で、往復再生の開始0/2.1秒から各120frameをDebug/Releaseで実行しました。計480frameのGPU位置45,233・法線55,747は、位置誤差0、法線最大1.78814e-7で、全mesh記録と保存画像がDebug/Releaseで一致しました。Skirt440点は両区間の全frameで侵入0です。旧設定の平均5.517/2.992点、最大深さ0.039969/0.018488から軽減しました。開始区間のVestも平均23.083→6.300点、最大0.038997→0.013193へ減りました。折り返し区間のVest平均10.258・最大0.021560、Jacket平均0.933・最大0.018619は残ります。身体capsuleとの距離であり、実身体mesh面・三角形内部・全衣服の非貫通を保証しません。

同じframe10の高解像度画像で、腰の露出と衣服の入り込みが軽減したことを確認しました。600frameの身体相対最大変化は29.617度、45度超0回で、旧29.396度/0回から暴れは再発していません。Release固定1/60秒3,000frameと実時間600frameを完走しました。実時間は16.637FPS、p95 41.451ms、揺れ更新52.076msで、300FPSは未達です。接触を追加した品質変更であり、速度改善とは扱いません。MSVC19.51/SDK10.0.28000.0の関連CPU3 targetはDebug/Releaseとも成功し、Root・fitter・実データの独立read-onlyレビューでもblocking issueはありません。

記録はbuild/native-validation/root-coverage-*へ保存しています。元asset・現在のbuild・最新数値とPNGを保持し、使用済みprobe・生成config・比較用PPM・調査script・重複exportはroot-coverage-cleanup.jsonのhashと絶対pathを確認して整理します。次はシャツやジャケットの取りこぼしと、接触が増えた構成の負担を扱います。

## 接触補正の共通計算と衣服接触の長時間確認

EvaluateRotatedOffsetで出力軸・子位置軸に依存しない微分ベクトルを27回から方向軸ごとの3回へまとめました。式と各成分の加算順、位置・速度・探索方法は保ちます。変更前の既存Debug契約は成功し、基準方向・骨長・反対向きに近い方向・ゼロoffsetを含む18ケースの独立有限差分、radial方向で微分0、独立なRodrigues回転位置を追加しました。MSVC19.51/SDK10.0.28000.0の関連CPU7件はDebug/Releaseとも成功しています。529球の旧・新Runtimeで、2.1秒開始の全mesh120frame集計が一致しました。

Windows11Pro26200、RTX4070SUPER/driver617.42、v142/MSVC19.29.30159、SDK10.0.22621.0、1280×720、標準効果・VSyncなし・非表示windowで検査しました。既存529球の実時間600frameは完走し32.587FPS、p95 29.286ms、揺れ更新24.309msでした。前回は16.637FPS/52.076msですが、実時間の軌跡が変わるため厳密な固定負荷比較ではありません。300FPSは未達です。

前回529球では2.1秒開始120frameのジャケット平均侵入0.933点・最大深度0.018619が残っていました。全衣服の移動寄与0.5以上へ広げた981球は同区間のジャケット・スカートを0にできましたが、固定600frameで身体相対最大63.48度・45度超4回、実時間283frameで停止しました。ジャケットを広く加えた750球も最大80.90度・45度超7回、実時間381frameで停止し、どちらも不採用です。

既存529球を全行同一で保ち、移動寄与99%以上のジャケット107点だけを追加した636球も確認しました。639点・1,936 anchorsで、固定600frameの最大29.43度・45度超0回、実時間600frameは29.572FPSで完走しました。開始0秒・2.1秒から各120frameをDebug/Releaseで描画し、45,233位置・55,747法線を480frame照合。位置差0、法線最大差1.78813934e-7で、画像とmesh集計は両構成で一致しました。スカート440点・ジャケット443点は侵入0、シャツ431点は平均6.30/8.82、最大深度0.01319/0.01222でした。しかし、固定3,000frameの755frame目で制約を解けず停止したため採用しません。サンプルは元の529球へ戻しました。636球の失敗姿勢が幾何的に不可能なのか探索不足なのかは未確定です。

残るシャツ8点も追加した989球は初回更新で失敗しました。detail36の全方向root回転を許す到達球を計算し、中心からSpine capsule軸への距離と半径の和が、接触に必要な0.082未満となることを全8点で確認しました。未知pivotでは最大弦長を使う保守的上界です。root位置の固定だけを理由に動かないとは扱いません。local offsetは回転しますが、この姿勢では既存boneの回転だけでは接触半径込みの到達範囲が足りません。これは近似capsuleとその姿勢に限った判定で、636球の別の失敗姿勢や身体mesh全体には一般化しません。

ゲーム用ソース383ファイルのRuntimeを再ビルドし、install SDK61ファイルの許可リストとconsumerへ配置したDLLのhash一致を確認しました。そのSDKをリンクしたconsumerのGPU Init・描画・Present・Shutdownもexit0です。export関連11契約は成功し、独立read-onlyレビュー、BOM/CRLF、formatter、diffも確認しました。mainへはRuntimeの共通計算変更だけを反映し、tests・私有asset・試行config・開発記録を含めません。

記録はbuild/native-validation/cloth-coverage-*へ保存します。試作636球の画像検査はcloth-coverage-trial-636-quality.json、連続動作はcloth-coverage-temporal-summary.jsonです。現在のbuild、元asset、必要な数値・PNGを保持し、使用済みprobe・生成config・PPM・重複exportはcloth-coverage-cleanup.jsonへsourceとhashを保存して整理します。

採用する529球・共通計算削減後のReleaseは、固定1/60秒3,000frameを完走しました。記録はcloth-coverage-stable529-fixed-3000.logです。試作636球が755frameで停止した記録とは区別します。

採用529球の固定600frameでは、記録した全boneの更新前・更新後位置が前回と全frame完全一致しました。cloth-coverage-stable529-trace-parity.jsonへ照合を保存しています。生成物448ファイル・107,738,175 bytesを、絶対pathとSHA-256の一致を確認して整理しました。

## 複数接触の探索停滞とfloat保存の境界を扱う

2026-10-14、試作636球が止まる755frameの全入力を保存し、windowを起動し直さずにCPUへ渡せる再生器を作りました。13鎖・76方向・89位置・1,936 anchorsをそのまま再生すると、旧探索は解を見つけず入力を保持しました。別の距離計算と方向探索で成立姿勢を得て、Runtime自身も変更せず受理することを確認しました。失敗入力は4鎖・16方向・20位置・25 anchors・8接触へ縮小し、匿名の数値fixtureと既知の成立姿勢だけをdevのテストへ保存しています。元mesh・texture・bone名・animationは含めません。

FSecondaryMotionContactLinearSystemは、身体の節球・線分、offset接触、複数boneの重み付き球から、関係する方向の係数だけを収集します。foundationのArrayとStringを使う局所的な計算型であり、subsystemや公開APIは追加していません。重複する方向係数は同じ行で合算し、単位方向の半径成分を除いてから減衰付き連立補正を求めます。補正が長すぎる場合は全方向を一律縮め続けず、減衰を増やして近傍で解き直します。line searchで縮小した場合は減衰を下げません。

最初の試作は755frameの侵入量を1.08e-30まで下げても、float保存後に3.27e-14残って停滞しました。後続757frameでは、右辺だけに余白を加える処理が目的関数と一致せず、反復数を64/128へ増やしても改善しませんでした。探索の侵入量・勾配・連立右辺・line searchへ同じ許容幅内の余白を適用し、保存後の元条件が成立した時点で候補を採用します。固定rootへ続く最初の線分の余白は、rootまでの空きの半分以下に制限します。骨長、曲げ角、身体接触、全weighted接触の最終条件は緩めていません。

方向256・行4096・合算済み係数65536を連立経路の上限にしました。上限に達した場合は空のerrorで従来探索へ戻し、入力不正・数値・確保失敗はerror付きで伝えます。内積系の24方向低rank、128方向の未収束、半径方向除去、重複合算、上限直前/超過、実際のallocation失敗と出力保持をDebug/Releaseで検査しました。257方向のnear-antiparallel入力も既存seedから解決し、無関係な256鎖を変更しません。匿名clusterを272方向へ拡張すると連立上限に達し、候補なし・空のerrorで532位置を変更せず返すことも確認しました。連立補正は既存3方式の探索がすべて失敗した後だけに使い、通常の成立経路を保ちます。

元の755frameと後続757frameは、全入力をCPU再生して保存後の全制約を満たし、weighted侵入は0以下でした。これは個別の探索失敗の修正であり、636球の長時間動作完了とは扱いません。連立を先に試す途中構成のnative固定3,000frameは758frameで停止し、既存探索後だけに限定した最終構成は別の757frame入力で停止しました。758frameの11個の独立した初期値は各500評価で完全な成立姿勢を得られず、最良でも侵入5.06e-5が残りました。探索が失敗したことだけから幾何的不成立とは断定しません。追加ジャケット設定は採用せず、START18は既存529球を維持します。

採用529球はRelease固定1/60秒3,000frameを完走しました。600frameの全boneの更新前・更新後位置は前回と完全一致し、身体相対の最大方向変化29.617度、45/90度超0回です。Windows11Pro26200、RTX4070SUPER driver617.42、v142/MSVC19.29.30159、SDK10.0.22621.0、非表示windowで実行しています。調査用replay・oracle・native probeは元のassetを変更しません。記録はbuild/native-validation/contact-failure-*へ保存します。

最終構成のMSVC19.51/SDK10.0.28000.0はDebug/Release各66/66成功です。採用529球は固定3,000frameを完走。開始0/2.1秒から各120frameのGPU変形をDebug/Release計480frame照合し、位置45,233の誤差0、法線55,747の最大誤差1.78813934e-7で、保存画像と全mesh記録も両構成で一致しました。折り返し区間のジャケット平均侵入0.933点・最大0.018619、シャツ平均10.258点・最大0.021560は残っています。身体capsuleとの近似距離であり、実body面や三角形内部の完全な非貫通ではありません。

385ファイルのゲーム構築用treeをRelease buildし、install SDK61ファイルの許可リストとconsumerへコピーしたRuntimeのhash一致を確認しました。SDKだけをリンクしたconsumerはGPU Init・Scene/UI描画・Present・Shutdownまでexit0です。tests・匿名fixture・私有asset・調査器・試作設定・開発記録はmainへ含めません。sourceのBOM/CRLF・formatter・noSTL・相対includeも確認し、独立read-onlyレビューでmemory上限・借用方向の寿命・保存判定・再試行順を検査しました。

同じ保存入力をnativeと同じv142/SDK10.0.22621.0で作ったCPU再生器にも渡しました。保存した755・757入力は保存後の全制約を満たして解決し、758入力は候補なしを保持しました。異なる再生履歴の最終636球の757frame失敗と、保存済み757入力のCPU成功を同じ結果として扱いません。

最終529球のRelease実時間600frameも完走し、40.246FPS、p95 26.805ms、secondary18.642msでした。途中の早期連立試作は19.635FPSでしたがCPU buildも同時実行していたため比較基準にはしません。最後の単独計測を現状記録とし、300FPSは未達です。完了要約はcontact-failure-completion.jsonです。

上限の最終監査では、Chain評価が空のerrorを数値エラーへ置き換える経路をREDで捕捉しました。数値エラーは侵入量加算helperが設定し、4096行・65536係数の上限は空のerrorを保持します。chain統合テストをDebug/Releaseとnative v142で実行し、通常のinvalid入力はerror付きのままです。


## 接触探索の途中姿勢を引き継ぎ、現在姿勢の近くを先に解く

2026-10-14、636球の最後の757frame停止入力を保存してCPUへ再生しました。成立姿勢があるのに従来seedからは到達できず、探索中の最良姿勢を後続の連立探索へ引き継ぐことで保存後の全制約を満たしました。更新後に得られた別の757入力も、最良の連立候補から一度だけ継続することで解決します。余白なしと保存時余白ありの侵入量は別々に比較し、各seedの反復上限32は保持します。追加seedと一回の継続により、全探索の最大試行数は従来より増えます。collectorの上限で評価が中断した候補は侵入量を無効にし、途中までの数値を最良候補として保存しません。

5鎖・20方向・12接触・36 anchors、13鎖・76方向・35接触・115 anchorsの匿名fixtureを追加しました。旧450a37cの探索ではRED、変更後はGREENです。独立した成立姿勢を先に確認し、入力からの探索結果と既知姿勢の不変性を、float保存後のroot・長さ・角度・身体接触・重み付き接触で検査します。大きい縮小fixtureは継続なしの構成でも成立するため、一回の継続そのものの回帰テストとは扱いません。継続が必要だった全入力の再生ログはcontact-continuation-solverへ保持します。元mesh・bone名・texture・animationはfixtureに含めません。

接触だけを優先して最初の成立seedを採ると、後続に近い成立姿勢があるのに遠い姿勢へ移ることを確認しました。試作035/636の755frame入力では従来の最初の候補の全点二乗変位が1.58547、現在方向から連立補正した候補が0.01598です。同じ方式内のseedを比較するだけではこの差を解消できませんでした。全方式の全seedを毎回比較する案は採用せず、現在方向から一回だけ局所連立探索を先に行い、失敗した場合に従来三方式、途中姿勢の引き継ぎ、最後の一回継続へ進めます。1鎖・1方向・2身体球の小fixtureでは独立した成立姿勢の移動量0.25927に0.001 model unitの余裕を加えた上限を契約にし、局所探索を先に行わない実装の移動量0.46586をREDで捕捉してから修正しました。

636球で節球半径0.038は後続758frameで停止し、0.0375の試作は1575frameで停止しました。0.035の試作は途中姿勢引き継ぎ構成で3000frameを完走しましたが、90度超の方向変化が10回、最大131.21度となり不採用です。局所探索を先に行う035/636も1575frameで停止し、大きな跳ねが残りました。独立探索が解を見つけないことだけから幾何的不成立とは断定しません。START18の529球・1574 anchors・節球半径0.038・身体capsule・骨長・角度・接触許容条件は変更していません。

Windows11Pro build26200、RTX4070SUPER driver617.42、v142/MSVC19.29.30159、SDK10.0.22621.0で、採用529球を固定1/60秒3000frame、50秒再生して完走しました。41骨・10鎖の時系列を独立集計すると、局所探索を先に行わない比較構成から採用構成へ、segment最大方向変化110.808→59.187度、45度超19→2回、90度超4→0回、root相対最大点移動0.16903→0.06994となりました。身体相対では最大110.262→59.397度、45度超19→1回、90度超4→0回です。segmentのp99は14.721→16.372度に増えています。前回の物理結果が次の入力へ戻るため、同一入力poseでのA/B比較ではありません。最終Runtime DLLで3000frameを再記録し、試作記録と全frameの骨名・時刻・更新前後位置が完全一致することも確認しました。

最終MSVC19.51/SDK10.0.28000.0はDebug/Release各66/66成功、全体実行時間3.73/3.37秒です。非表示windowで開始0秒・2.1秒からDebug/Release各120frame、計480frameのGPU変形を確認し、位置45233の誤差0、法線55747の最大誤差1.78813934e-7でした。全mesh記録と保存画像は両構成で一致します。Skirt確認点の侵入は両区間0ですが、折り返し区間のJacket平均1.675点・最大0.011648、Shirt平均11.633点・最大0.014894は残ります。身体capsuleへの近似距離であり、全頂点・三角形面の非貫通を保証しません。最大変化frame2214までの2215frameを再生し、1280×720の画像も確認しました。

最終529球の単独Release実時間600frameは完走し、26.175FPS、p95 45.653ms、secondary31.919ms、Draw0.447/Present5.190/IK0.584msでした。前の記録40.246FPSより遅く、300FPSは未達です。ここでの成果は大きな姿勢跳びの減少と保存入力の探索失敗への対処であり、速度改善ではありません。59度の急変や一部衣服の埋まりも残るため、揺れもの全体の完成とは扱いません。

prepare_game_repository.pyの385ファイルをゲーム構築用checkoutへ反映し、全ファイルの一致とReleaseの5 example buildを確認しました。install SDK61ファイルの許可リスト、consumerへ配置したRuntimeとSDKのhash一致も確認しました。tests・匿名fixture・調査器・私有asset・試作設定・開発記録はmainへ含めません。sourceのBOM/CRLF・formatter・noSTL・相対includeを確認し、別agentがstrategyとseedの順序、quiet budget、candidate確保のatomic性、縮小fixtureの全参照を読み取り専用でレビューしました。

記録はbuild/native-validation/contact-continuation-*へ保存しています。前回、一時生成物の削除は自動承認レビューが「blocked by policy」として拒否したため、今回も別の手段で削除していません。以前のcleanup planは今回再利用したprobeを含むためそのまま実行できません。元asset・現在build・最新ログを保持し、使い終わった調査用buildの整理は未完了です。
