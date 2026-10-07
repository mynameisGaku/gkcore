# 機能とサポート状況

この一覧は利用できる API と実際に確認した動作を分けて記載します。関数宣言や CPU 側テストだけでは、GPU に描画できることを意味しません。

| 分野 | 現在の実装・確認範囲 | 視覚的に確認できること / 未対応 |
|---|---|---|
| アプリの基本ループ | `gk::SetWindowSize`、`gk::Init` / `gk::Shutdown`、`gk::ProcessEvents`、`gk::IsKeyDown`、`gk::BeginFrame`、`gk::Present` とエラー診断を提供。Windows 描画部には Win32 ウィンドウと Direct3D 12 の表示経路がある | Windows GPU smokeでAPI呼び出し、Present、resize、Shutdown後の再Initを確認 |
| 2D / 3D 描画 | `gk::DrawRect`、`gk::DrawRectOutline`、`gk::DrawImage`、`gk::DrawTriangle3D`、`gk::Vec3`、カメラ、モデル変換 API。Scene/UI の層と層内の命令順、輪郭矩形の API・描画 packet・頂点展開の内側配置・UV・太さを CPU テストで確認 | GPU画像テストでScene/UIの色、3D三角形、PNG画像を確認。輪郭サンプルの細線・太線を目視 |
| ウィンドウ変更 | イベント処理が表示領域の変更を検知し、表示先と深度バッファを作り直す。変更後の寸法は次のフレームへ反映する | GPU smokeで960×540への変更を確認。mixed_sceneの最大化後も表示を目視 |
| 画像 / モデル | PNG / BMP の画像、OBJ / GLB 2.0 / FBX の静的メッシュを CPU 側で読み込む。GLB と FBX の基本色係数・画像を含むモデル描画経路、画像の転送・キャッシュを実装。GLB metallic / roughness 係数、法線による方向光・一様環境光の内蔵材質描画を追加。FBX の ASCII / バイナリ、相対パスと埋め込み PNG、階層・幾何変換、単位変換を CPU テストで確認 | GPU captureでGLB照明サンプルの非金属/金属球の色と90度照明変更による画素差を確認し、Runtime DLLでも表示を目視。全モデル形式の実機表示は未確認。FBX BMP、影、環境マップ / IBL、metallic-roughness texture、normal map、alpha mode は未対応 |
| ポストエフェクト | Scene の HDR 描画から Bloom、露出・トーンマッピング、彩度・コントラスト調整、FXAA を経て UI を合成する経路と設定 API を実装。設定値は `BeginFrame` で取り込む。Bloom、トーンマッピング、FXAA は初期設定で有効。独自ポスト shader API と CPU 契約も統合済み | GPU画像テストでFXAA有効・無効の基本描画と、tintのSceneへの適用・UIの色維持を確認。Bloomの表示も目視。[設定例](effects.md)、[独自シェーダーのガイド](post-effect-shader.md) を参照 |
| カスタムピクセルシェーダー | `include/gkcore/Shader.hlsl` の共通入力、64 個の `gkcoreUserData` 定数、任意の画像・sampler binding を定義。開発用コンパイラーは HLSL を `.frag` FSL/DXIL 形式に変換する。D3D12 描画部には読み込み、reflection 検査、共通頂点シェーダーと Scene/UI・深度・blend 別の pipeline、定数と画像の binding が実装されている。独自 shader 描画は 1 フレーム 4096 件まで。モデルには別の内蔵 PBR 材質 shader を使う | Linux DXC/FSL compileとreflectionに加え、Windows Release GPU smokeでカスタムshaderの読み込み・binding・Presentを確認。独自頂点 shader、利用者が差し替えるモデル材質 shader ABI は未対応。[描画 shader](custom-shader.md) と[ポスト shader](post-effect-shader.md)のガイドを参照 |
| 文字表示 / 入力 | `gk::DrawString` は UTF-8 文字列を Windows のシステム標準フォントで描画し、同じ文字列・色・大きさを最大 64 件、合計 16 MiB まで保持するキャッシュを使う。フォントファイルは不要。Escape、矢印、Space、Enter、Tab、Backspace、Shift/Control、数字、英字のキー問い合わせ、左/右/中央のマウスボタンとカーソル位置取得も実装 | GPU画像テストとサンプル画面で日本語UTF-8文字列の表示を確認。Space/Escapeの操作は未確認。文字入力イベントとフォントファイルの指定は未実装。入力問い合わせはアプリのメインスレッドから行う |
| SDK 配布 | Runtime のファイル許可リストと、開発用ファイルの混入を検出するテスト | Windows Runtime buildとpackage CTestを確認。install済みSDKだけのconsumer GPU smokeもInit、Scene/UI描画、Present、Shutdownまで成功。依存物の再配布条件とgkcore自身の配布ライセンスは未確定 |

## テストと再現性

2026-10-05、Windows 11 Pro x64 (build 26200)、Visual Studio 18 2026 / v142 14.29 (MSVC 19.29.30159)、Windows SDK 10.0.22621.0、CMake 4.3.1、GeForce RTX 4070 SUPERで、最終`PRE_SETUP.bat --gpu-check`が成功しRelease RuntimeとCTest 32/32を確認しました。GPU smokeは2.46秒、5 mode画素検査は5.95秒、install済みSDK consumer GPU smokeは3.77秒、合計15.05秒です。CPU RuntimeOFFのDebug/Release CTestは同じPCで各27/27成功しましたが、MSVC 19.51.36260.0とWindows SDK 10.0.28000.0を使った別toolchainでの検証です。標準consumer GPU smokeはInit、描画、Present、Shutdownまで成功しました。詳細は[TDD 検証ログ](TDD_LOG.md)と[GPU描画検証](render-validation.md)を参照してください。

FSL artifact の検査テストは `python3 tests/shader_contract_tests.py` で実行します。固定した The Forge の D3D12 adapter 検索はsoftware adapterを除外するため、WARPは利用できません。GPU画素読み戻しとサンプルの目視は実施済みですが、検査は固定位置・色・領域閾値による回帰判定です。全面画像の画質acceptance、すべてのモデル形式の実機表示、Runtime Debug、他GPU、Space/Escape操作、再配布条件の最終確認は未実施です。

見た目の回帰検査では Windows GPU ごとに基準画像を用意する方針です。同じ GPU、driver、画像で、RGB のいずれかが 2/255 を超えて異なる画素の割合を 1% 以下、RGB 全 channel の平均絶対誤差を 2/255 以下にします。GPU が異なる画像同士は比較しません。

初学者向け導入の確認は未実施です。C++ の基礎があり Visual Studio を導入済みの学生 3 名のうち 2 名以上が、日本語 1 ページの手引きを使い、30 分以内に最初の画像付きサンプルを起動できることを出荷時の目標とします。確認結果は出荷前に記録します。

2026-10-07には、同じアプリで6フレームのポスト効果切り替えとUIの色維持を確認しました。取得枚数の既定値と上限、不正値の拒否も検査し、更新後のRelease全CTestは32/32件成功しています。フレームごとにGPUの完了を待って取得するため、高負荷時のちらつきや同時実行の競合は別に検証します。

## 次に進める内容

1. 高負荷での連続描画、入力操作、各エフェクトの個別設定をGPU画像と起動画面で確認し、画質評価を進める。現在の画像テストは代表画素・領域と6フレームの設定切り替えを検査する。
2. 初学者向け手引きを使った導入確認と、Runtime Debug・他GPU・全モデル形式の実機表示を検証する。インストールSDK consumerのInit・最初のPresentは確認済み。
3. 依存物の再配布条件と gkcore 自身の配布ライセンスを確定し、必要な notice を整える。
4. 影、環境マップ / IBL、metallic-roughness texture、normal map、alpha mode とモデル材質用 shader ABI を設計・実装・検証する。

各項目の現時点の対応範囲と未確認事項は上の表を参照してください。
