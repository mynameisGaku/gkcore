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

## ブランチの管理

開発中の変更はローカルの `dev` に機能単位でコミットします。レビューと必要な検証を通った区切りをローカルの `main` に反映します。現在はローカルで管理し、リモートへの push は保留しています。

## 現在の制限

CMake による開発用 build 設定と、The Forge を使う Windows 描画部があります。Windows 実機での Runtime build と GPU 描画はまだ確認していません。Linux の CPU テストは API の約束を検査しますが、GPU の表示結果は保証しません。

## Windows GPU smoke と画面の目視確認

DX12 対応 GPU を搭載した Windows PC で次を実行すると、通常の build/test に加えて、Windows 描画 smoke を CTest で実行します。

```bat
PRE_SETUP.bat --gpu-check
```

この smoke は初期化、カスタムポスト shader の有効化、960×540 へのサイズ変更、Scene と UI を含む描画命令の `Present`、ポスト shader の無効化・再有効化、無効化後の削除、終了と再初期化を確認します。API の戻り値とウィンドウの client size を検査しますが、画素の読み戻しは行わないため、色や UI が期待どおりに見えることまでは判定しません。

色の変化を目で確かめるには、同じ build が作る `build\runtime-windows\Release\gkcore_custom_post_effect.exe` を起動します。ウィンドウを 960×540 以上に保ち、Space キーでポスト効果を切り替えてください。有効時には Scene の三角形と矩形の色が変わり、無効時には元の色に戻ります。緑の UI 矩形と画面下部の ON/OFF 表示は Scene の効果に影響されず、ウィンドウをリサイズしても表示されることを目で確認します。Escape キーで終了します。

## 開発テストの実行

リポジトリの開発用テストは次のコマンドで構成・ビルド・実行する設計です。

```sh
cmake -S . -B build -DGKCORE_BUILD_RUNTIME=OFF -DGKCORE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows Runtime の build には固定版 The Forge、DXC 1.8.2405、Visual Studio 2022 の C++ 開発環境、v142/MSVC 1929 toolset、CMake 3.21 以降、Windows 10 SDK が必要です。`PRE_SETUP.bat` は前提を確認し、固定依存物を取得して The Forge の library/shader、gkcore Runtime/sample、CTest を build します。GPU smoke は必要な場合にだけ有効にします。Linux では Runtime を無効にして CPU テストを実行します。
