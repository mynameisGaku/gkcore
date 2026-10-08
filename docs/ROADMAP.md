# 機能とサポート状況

この一覧は利用できる API と実際に確認した動作を分けて記載します。関数宣言や CPU 側テストだけでは、GPU に描画できることを意味しません。

| 分野 | 現在の実装・確認範囲 | 視覚的に確認できること / 未対応 |
|---|---|---|
| アプリの基本ループ | `gk::SetWindowSize`、`gk::Init` / `gk::Shutdown`、`gk::ProcessEvents`、`gk::IsKeyDown` / `gk::WasKeyPressed`、`gk::BeginFrame`、`gk::Present` とエラー診断を提供。Windows 描画部には Win32 ウィンドウと Direct3D 12 の表示経路がある | Windows GPU smokeでAPI呼び出し、Present、resize、Shutdown後の再Initを確認 |
| 2D / 3D 描画 | `gk::DrawRect`、`gk::DrawRectOutline`、`gk::DrawImage`、`gk::DrawTriangle3D`、`gk::Vec3`、カメラ、モデル変換 API。Scene/UI の層と層内の命令順、輪郭矩形の API・描画 packet・頂点展開の内側配置・UV・太さを CPU テストで確認 | GPU画像テストでScene/UIの色、3D三角形、PNG画像を確認。輪郭サンプルの細線・太線を目視 |
| ウィンドウ変更 | イベント処理が表示領域の変更を検知し、表示先と深度バッファを作り直す。変更後の寸法は次のフレームへ反映する | GPU smokeで960×540への変更を確認。mixed_sceneの最大化後も表示を目視 |
| 画像 / モデル | PNG / BMP の画像、OBJ / GLB 2.0 / FBX の静的メッシュを CPU 側で読み込む。GLBの基本色画像は材質が指定するUVセットを選び、FLOATまたは正規化した符号なし8bit/16bitの座標を保持する。GLB と FBX の基本色係数・画像を含むモデル描画経路、画像の転送・キャッシュを実装。GLB metallic / roughness係数とMR画像の線形G/B成分、基本色と独立した材質画像UV、法線による方向光・一様環境光の内蔵材質描画を追加。明示NORMAL/TANGENTを持つGLBのnormalTexture、独立UV、scale、node/runtime鏡映にも対応。GLBの5画像それぞれのsampler（Repeat/MirroredRepeat/ClampToEdge・Nearest/Linear）を反映。GLBの自己発光係数・画像・KHR_materials_emissive_strengthを反射光へ加え、SceneのHDRとBloomへ渡す。GLBのocclusionTextureは線形Rとstrengthで一様環境光だけを弱め、方向光・自己発光は維持する。GLBのOPAQUEとMASKを区別し、画像・材質のアルファと境界値を使って透明部分を切り抜く。FBX の ASCII / バイナリ、相対パスと埋め込み PNG、階層・幾何変換、単位変換を CPU テストで確認 | GPU captureでGLB照明サンプルの非金属/金属球の色と90度照明変更による画素差を確認し、Runtime DLLでも表示を目視。GLBのUV0/1/2と正規化U8/U16を6モデルのGPU画像で確認。全モデル形式の実機表示は未確認。FBX BMP、GLBのUV sparse / KHR_texture_transform、影、環境マップ / IBL、接線の自動生成、BLEND は未対応 |
| ポストエフェクト | Scene の HDR 描画から Bloom、露出・トーンマッピング、彩度・コントラスト調整、FXAA を経て UI を合成する経路と設定 API を実装。設定値は `BeginFrame` で取り込む。Bloom、トーンマッピング、FXAA は初期設定で有効。独自ポスト shader API と CPU 契約も統合済み | GPU画像テストでFXAA有効・無効の基本描画と、tintのSceneへの適用・UIの色維持を確認。Bloomの表示も目視。[設定例](effects.md)、[独自シェーダーのガイド](post-effect-shader.md) を参照 |
| カスタムピクセルシェーダー | `include/gkcore/Shader.hlsl` の共通入力、64 個の `gkcoreUserData` 定数、任意の画像・sampler binding を定義。開発用コンパイラーは HLSL を `.frag` FSL/DXIL 形式に変換する。D3D12 描画部には読み込み、reflection 検査、共通頂点シェーダーと Scene/UI・深度・blend 別の pipeline、定数と画像の binding が実装されている。独自 shader 描画は 1 フレーム 4096 件まで。モデルには別の内蔵 PBR 材質 shader を使う | Linux DXC/FSL compileとreflectionに加え、Windows Release GPU smokeでカスタムshaderの読み込み・binding・Presentを確認。独自頂点 shader、利用者が差し替えるモデル材質 shader ABI は未対応。[描画 shader](custom-shader.md) と[ポスト shader](post-effect-shader.md)のガイドを参照 |
| 文字表示 / 入力 | `gk::DrawString` は UTF-8 文字列を Windows のシステム標準フォントで描画し、同じ文字列・色・大きさを最大 64 件、合計 16 MiB まで保持するキャッシュを使う。フォントファイルは不要。Escape、矢印、Space、Enter、Tab、Backspace、Shift/Control、数字、英字のキー問い合わせ、左/右/中央のマウスボタンとカーソル位置取得も実装 | GPU画像テストとサンプル画面で日本語UTF-8文字列の表示を確認。輪郭サンプルでSpaceの切り替えとEscape終了を確認。文字入力イベントとフォントファイルの指定は未実装。入力問い合わせはアプリのメインスレッドから行う |
| SDK 配布 | Runtime のファイル許可リストと、開発用ファイルの混入を検出するテスト | Windows Runtime buildとpackage CTestを確認。install済みSDKだけのconsumer GPU smokeもInit、Scene/UI描画、Present、Shutdownまで成功。依存物の再配布条件とgkcore自身の配布ライセンスは未確定 |

## テストと再現性

2026-10-08、Windows 11 Pro x64 build 26200、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142 14.29（MSVC 19.29.30159）、Windows SDK 10.0.22621.0、CMake 4.3.1でRelease・Debug Runtimeをビルドし、全CTestが各53/53件成功しました。DebugはD3D12 InfoQueueの取得を画像テストで必須確認しています。両構成のForgeライブラリとRuntime出力は別フォルダーへ保存し、install済みSDK consumerでもInit、描画、Present、Shutdownが成功しました。

RuntimeOFFのCPU構成は同じPCのMSVC 19.51 / Windows SDK 10.0.28000.0で各36/36件が成功しています。公開47キーの対応表と、仮想キー256個の押下・反復・消去をCPUで確認しました。輪郭サンプルではSpaceによるBloom切り替えと短いEscape入力による終了を実際に操作しました。[キー入力](input.md)に使い方をまとめています。

GPU画像検査は基本描画・モデル照明、6フレームの効果切り替え、多数の矩形を123フレーム描いた後の出力、各ポストエフェクトの個別設定を扱います。[効果の実画像](effects.md)も掲載しています。透明画像の合成・拡大・回転と、描画登録後のhandle削除も確認し、132フレームで396画像を読み直してcacheの128枠を超えた後の表示を検査しました。[画像ガイド](images.md)を参照してください。256MiBのbyte上限や転送失敗時のcleanupは未検証です。

独自shaderは定数slot 0と63、登録時の値の保持、2種類のPNGのScene/UI描画、画像なしの描画色、6フレームの更新、使用中shaderの削除拒否を画像とAPIの両方で検査しました。取得6画像はRelease/Debug間でbyte単位に一致しました。[カスタムシェーダー](custom-shader.md)に実画像を掲載しています。全64slotや4096件上限、独自shaderの半透明・3D深度検査はGPUで確認していません。

GLBの基本色画像が指定するUVセットを使うよう修正し、指定省略とUV0、UV1、UV2、正規化した符号なし8bit・16bitの6モデルを実GPUで確認しました。UV0とUV1は45,000画素が異なり、指定省略はUV0と全画素一致しました。6画像はRelease/Debug間でも一致しています。選択先の欠損や不正な座標形式、未対応のUV変換は読み込み時に診断付きで拒否します。[モデルガイド](models.md)に画像を掲載しています。

GLBのMASK材質を内蔵モデルshaderで切り抜く処理を追加しました。Scene11画像・UI4画像・奥から描く面の深度検査1画像で、境界値との等号、材質のアルファ係数、画像なしの材質、OPAQUEのアルファ無視を確認しました。BLENDと独自pixel shaderによるMASKモデルの描画は、診断付きで拒否します。[モデルガイド](models.md)に実画像と制約を掲載しています。

金属度・粗さの画像を線形で読み、G・Bの値と材質係数を掛ける描画を追加しました。基本色のsRGB解釈とはcacheの色空間を分け、同一画像を両方に使う材質も確認しました。Scene15・UI8・132フレームの読み直し後2枚の計25画像がRelease/Debugで一致しています。材質係数だけの参照モデルとの比較、R/Aの無視、別UV、異なる画像pairの切り替え、clip後の独立UV保持も検査しました。[モデルガイド](models.md)に実画像を掲載しています。

法線画像はScene20・UI11・runtime鏡映2・2種類の再読込後各2・同frameに50モデルを描く1の38画像で、頂点法線だけの参照モデルと比較しています。38枚はRelease/Debug間でbyte単位に一致しました。NORMAL/TANGENTの自動生成は未対応です。同じimage項目を参照する基本色・MR・法線は、texture indexが異なる場合もモデル内でdecode結果を共有し、sRGB/線形の2資源を使います。画像共有と各役割のUV選択は独立です。詳しくは[モデルガイド](models.md)を参照してください。

samplerは8種類のwrap・整数端・省略値、同一画像を異なる設定で描く材質、基本色/MR/法線の個別設定、拡大・縮小の補間、132 frame再読込後の描画を34画像で検査しました。両構成の全画像がbyte単位に一致しました。minFilterのミップ指定では1×1までの縮小段を生成し、段と画素の補間方法を独立して使います。省略値とミップなし指定は従来の単一段を維持します。[モデルガイド](models.md)を参照してください。

ミップ指定のGLB画像は1×1まで生成して全段転送し、4 filter・sRGB/線形用途・NPOT端画素・ミップ有無の同時描画を28画像で確認しました。28枚はRelease/Debug間でbyte一致しています。平均は色空間を保ち、画像cache上限は全段の論理RGBA byte数で計上します。[モデルガイド](models.md)を参照してください。

自己発光係数・画像・KHR_materials_emissive_strengthはScene/UIで使え、画像Aは無視し、独立UVとsampler・ミップを保持します。照明0での発光、同じimageの材質設定、MASK、HDR強度4とBloom on/off、132回再読込後の描画を45画像で確認し、Release/Debug間でbyte一致しました。周囲を照らす光源や間接光を生成する機能ではありません。[モデルガイド](models.md#glbの自己発光)を参照してください。

環境遮蔽画像は線形Rとstrengthで一様環境光だけを調整し、方向光・自己発光は維持します。UV1、画像共有、sampler・strengthの設定差、MASK、ミップ有無、132回再読込後の描画を43画像で確認し、全参照比較とRelease/Debug間のbyte一致を確認しました。[モデルガイド](models.md#glbの環境遮蔽画像)を参照してください。

FSL artifactは`python tests/shader_contract_tests.py`で検査します。固定したThe ForgeのD3D12 adapter検索はsoftware adapterを除外するため、WARPは利用できません。GPU画像は固定位置・色・領域による回帰判定です。全面画像の画質、連続フレームすべてのちらつき、GPU負荷と目標FPS、全モデル形式、全入力操作、他GPU、GPU-based validation、再配布条件の最終確認は未実施です。実行手順は[描画検証](render-validation.md)、詳しい結果は[TDD検証ログ](TDD_LOG.md)を参照してください。

見た目の回帰検査ではWindows GPUごとに基準画像を用意する方針です。同じGPU、driver、画像で、RGBのいずれかが2/255を超えて異なる画素の割合を1%以下、RGB全channelの平均絶対誤差を2/255以下にします。GPUが異なる画像同士は比較しません。

初学者向け導入の確認は未実施です。C++の基礎がありVisual Studioを導入済みの学生3名のうち2名以上が、日本語1ページの手引きを使い、30分以内に最初の画像付きサンプルを起動できることを出荷時の目標とします。

## 次に進める内容

1. 高負荷での連続描画、入力操作、各エフェクトの個別設定をGPU画像と起動画面で確認し、画質評価を進める。現在の画像テストは代表画素・領域、各効果の個別設定とUI保持、6フレームの設定切り替え、多数の矩形を123フレーム描いた後の出力を検査する。
2. 初学者向け手引きを使った導入確認と、他GPU・全モデル形式の実機表示を検証する。インストールSDK consumerのInit・最初のPresentは確認済み。
3. 依存物の再配布条件と gkcore 自身の配布ライセンスを確定し、必要な notice を整える。
4. 影、環境マップ / IBL、接線の自動生成、BLENDとモデル材質用 shader ABI を設計・実装・検証する。

各項目の現時点の対応範囲と未確認事項は上の表を参照してください。
