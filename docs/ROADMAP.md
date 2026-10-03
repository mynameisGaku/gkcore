# 機能とサポート状況

この一覧は利用できる API と実際に確認した動作を分けて記載します。関数宣言や CPU 側テストだけでは、GPU に描画できることを意味しません。

| 分野 | 現在の実装・確認範囲 | 視覚的に確認できること / 未対応 |
|---|---|---|
| アプリの基本ループ | `gk::SetWindowSize`、`gk::Init` / `gk::Shutdown`、`gk::ProcessEvents`、`gk::IsKeyDown`、`gk::BeginFrame`、`gk::Present` とエラー診断を提供。Windows 描画部には Win32 ウィンドウと Direct3D 12 の表示経路がある | Windows 10/11 x64 の実 GPU では未実行。CPU テストでは Windows Runtime の起動を確認できない |
| 2D / 3D 描画 | `gk::DrawRect`、`gk::DrawImage`、`gk::DrawTriangle3D`、`gk::Vec3`、カメラ、モデルの位置/回転/拡大率を設定する API。Scene/UI の層と層内の命令順を CPU テストで確認 | 塗りつぶし矩形、三角形、モデル形状と PNG/BMP 画像の描画、画像転送とキャッシュの経路を実装。Windows/MSVC でのビルドと GPU 表示は未確認。輪郭矩形は未対応。`GKCORE_RUN_BACKEND_SMOKE=ON` で DX12 対応機の確認を有効にできる |
| ウィンドウ変更 | イベント処理が表示領域の変更を検知し、表示先と深度バッファを作り直す。変更後の寸法は次のフレームへ反映する | 処理は実装されているが、実機での表示領域や座標は未確認 |
| 画像 / モデル | PNG / BMP の画像を CPU 側で読み込み、OBJ と GLB 2.0 の静的メッシュを取り込む。GLB の埋め込み PNG、基本色画像の参照、PBR 材質の係数も CPU 側のデータへ読み込む。画像の描画と GPU 転送・キャッシュを実装 | Windows/MSVC でのビルドと GPU 表示は未確認。GLB の材質、画像、PBR 照明は GPU 描画に未対応。CPU 読み込みや描画コードの存在は画面表示の確認を意味しない |
| ポストエフェクト | Scene の HDR 描画先から Bloom 抽出・ぼかし、露出・階調変換を通し、その後 UI を合成する経路を実装。設定の初期値は有効で、フレーム開始時の値を適用する | Windows/MSVC でのビルドと実 GPU 上の表示は未確認。色調整と輪郭平滑化は未実装 |
| カスタム shader | shader handle、描画ごとの `gk::Float4` 定数保存を CPU テストで確認。FSL/DXIL parser はテスト用に保存した、固定 The Forge と DXC 1.8.2405 で作成した実際の `.frag` artifact を読み、対応しない `.vert` artifact を拒否する | 利用者 shader の読み込みと GPU pipeline / 描画は未対応。[shader guide](custom-shader.md) の primitive ABI は提案段階。テスト artifact は Linux 上の DXC symlink を使って作成し、Windows shader build 全体や root signature は検証していない。画像 texture、3D material、ポストエフェクト ABI も未確定 |
| 文字表示 / 入力 | `gk::DrawString` は UTF-8 文字列を Windows のシステム標準フォントで描画し、同じ文字列・色・大きさを最大 64 件、合計 16 MiB まで保持するキャッシュを使う。フォントファイルは不要。Escape、矢印、Space、Enter、Tab、Backspace、Shift/Control、数字、英字のキー問い合わせ、左/右/中央のマウスボタンとカーソル位置取得も実装 | Windows/MSVC でのビルドと画面上の文字表示は未確認。文字入力イベントとフォントファイルの指定は未実装。入力問い合わせはアプリのメインスレッドから行う |
| SDK 配布 | Runtime のファイル許可リストと、開発用ファイルの混入を検出するテスト | Windows Runtime SDK、利用者側アプリの build/run、同梱する第三者ソフトの再配布条件は未完了 |

## テストと再現性

CPU のみで実行する CTest の内容と結果は [TDD 検証ログ](TDD_LOG.md) を参照してください。FSL artifact の検査テストは `python3 tests/shader_contract_tests.py` で実行します。Windows の描画 smoke は `GKCORE_RUN_BACKEND_SMOKE=ON` にし、DX12 対応 GPU を搭載した PC で実行します。固定した The Forge の D3D12 adapter 検索は software adapter を除外するため、WARP は利用できません。

見た目の回帰検査では Windows GPU ごとに基準画像を用意する方針です。同じ GPU、driver、画像で、RGB のいずれかが 2/255 を超えて異なる画素の割合を 1% 以下、RGB 全 channel の平均絶対誤差を 2/255 以下にします。GPU が異なる画像同士は比較しません。

初学者向け導入の確認は未実施です。C++ の基礎があり Visual Studio を導入済みの学生 3 名のうち 2 名以上が、日本語 1 ページの手引きを使い、30 分以内に最初の画像付きサンプルを起動できることを出荷時の目標とします。確認結果は出荷前に記録します。
