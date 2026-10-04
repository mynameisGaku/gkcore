# 機能とサポート状況

この一覧は利用できる API と実際に確認した動作を分けて記載します。関数宣言や CPU 側テストだけでは、GPU に描画できることを意味しません。

| 分野 | 現在の実装・確認範囲 | 視覚的に確認できること / 未対応 |
|---|---|---|
| アプリの基本ループ | `gk::SetWindowSize`、`gk::Init` / `gk::Shutdown`、`gk::ProcessEvents`、`gk::IsKeyDown`、`gk::BeginFrame`、`gk::Present` とエラー診断を提供。Windows 描画部には Win32 ウィンドウと Direct3D 12 の表示経路がある | Windows 10/11 x64 の実 GPU では未実行。CPU テストでは Windows Runtime の起動を確認できない |
| 2D / 3D 描画 | `gk::DrawRect`、`gk::DrawRectOutline`、`gk::DrawImage`、`gk::DrawTriangle3D`、`gk::Vec3`、カメラ、モデル変換 API。Scene/UI の層と層内の命令順を CPU テストで確認。輪郭矩形の API・描画 packet・頂点展開の内側配置・UV・太さを CPU テストで確認 | 塗りつぶし矩形、三角形、モデル形状と PNG/BMP 画像の経路は実装済み。輪郭矩形を含む Windows/MSVC のビルドと GPU 表示は未確認。`GKCORE_RUN_BACKEND_SMOKE=ON` で DX12 対応機の確認を有効にできる |
| ウィンドウ変更 | イベント処理が表示領域の変更を検知し、表示先と深度バッファを作り直す。変更後の寸法は次のフレームへ反映する | 処理は実装されているが、実機での表示領域や座標は未確認 |
| 画像 / モデル | PNG / BMP の画像、OBJ / GLB 2.0 / FBX の静的メッシュを CPU 側で読み込む。GLB と FBX の基本色係数・画像を含むモデル描画経路、画像の転送・キャッシュを実装。FBX の ASCII / バイナリ、相対パスと埋め込み PNG、階層・幾何変換、単位変換を CPU テストで確認 | Windows/MSVC でのビルドと GPU 表示は未確認。FBX の BMP 読み込みは未確認。PBR 照明、影、環境マップ、alpha mode は未対応。 |
| ポストエフェクト | Scene の HDR 描画から Bloom、露出・トーンマッピング、彩度・コントラスト調整、FXAA を経て UI を合成する経路と設定 API を実装。設定値は `BeginFrame` で取り込む。Bloom、トーンマッピング、FXAA は初期設定で有効。独自ポスト shader API と CPU 契約も統合済み | Windows/MSVC でのリンクと実 GPU 上の見た目は未確認。[設定例](effects.md)、[独自シェーダーのガイド](post-effect-shader.md) を参照 |
| カスタムピクセルシェーダー | `include/gkcore/Shader.hlsl` の共通入力、64 個の `gkcoreUserData` 定数、任意の画像・sampler binding を定義。開発用コンパイラーは HLSL を `.frag` FSL/DXIL 形式に変換する。D3D12 描画部には読み込み、reflection 検査、共通頂点シェーダーと Scene/UI・深度・blend 別の pipeline、定数と画像の binding が実装されている。独自 shader 描画は 1 フレーム 4096 件まで | Linux DXC による HLSL compile と FSL 形式への変換、shader interface の CPU 正規化テスト、定数 ABI の CPU テストが成功。全体テストは [TDD 検証ログ](TDD_LOG.md) を参照。Windows の COM reflection、MSVC link、実 GPU 表示は未確認。独自頂点 shader、PBR model material shader は未対応。[描画 shader](custom-shader.md) と[ポスト shader](post-effect-shader.md)のガイドを参照 |
| 文字表示 / 入力 | `gk::DrawString` は UTF-8 文字列を Windows のシステム標準フォントで描画し、同じ文字列・色・大きさを最大 64 件、合計 16 MiB まで保持するキャッシュを使う。フォントファイルは不要。Escape、矢印、Space、Enter、Tab、Backspace、Shift/Control、数字、英字のキー問い合わせ、左/右/中央のマウスボタンとカーソル位置取得も実装 | Windows/MSVC でのビルドと画面上の文字表示は未確認。文字入力イベントとフォントファイルの指定は未実装。入力問い合わせはアプリのメインスレッドから行う |
| SDK 配布 | Runtime のファイル許可リストと、開発用ファイルの混入を検出するテスト | Windows Runtime SDK、利用者側アプリの build/run、同梱する第三者ソフトの再配布条件は未完了 |

## テストと再現性

CPU のみで実行する CTest の内容と結果は [TDD 検証ログ](TDD_LOG.md) を参照してください。FSL artifact の検査テストは `python3 tests/shader_contract_tests.py` で実行します。Windows の描画 smoke は `GKCORE_RUN_BACKEND_SMOKE=ON` にし、DX12 対応 GPU を搭載した PC で実行します。固定した The Forge の D3D12 adapter 検索は software adapter を除外するため、WARP は利用できません。

見た目の回帰検査では Windows GPU ごとに基準画像を用意する方針です。同じ GPU、driver、画像で、RGB のいずれかが 2/255 を超えて異なる画素の割合を 1% 以下、RGB 全 channel の平均絶対誤差を 2/255 以下にします。GPU が異なる画像同士は比較しません。

初学者向け導入の確認は未実施です。C++ の基礎があり Visual Studio を導入済みの学生 3 名のうち 2 名以上が、日本語 1 ページの手引きを使い、30 分以内に最初の画像付きサンプルを起動できることを出荷時の目標とします。確認結果は出荷前に記録します。

## 次に進める内容

1. Windows/MSVC で Runtime をビルドし、DX12 実機で 2D・3D・画像・文字・ポスト処理を確認する。Runtime SDK だけを使った新規プロジェクトのビルドと起動も検証する。
2. GLB の PBR 照明、影、環境マップ、alpha mode を実装し、GPU 上で確認する。
3. モデル材質用 shader ABI を追加して確認する。
4. 依存物の再配布条件と gkcore 自身の配布ライセンスを確定し、Runtime manifest を監査する。初学者向けガイドの導入確認も実施する。

Windows/MSVC と実 GPU 上の検証結果はまだありません。各項目の現時点の対応範囲は上の表を参照してください。
