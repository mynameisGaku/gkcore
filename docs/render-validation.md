# GPUで確認した描画

Windows 11 Pro、RTX 4070 SUPER / driver 610.74で取得した実際の描画画像です。2026-10-05に、Release RuntimeをVisual Studio 2026 / v142、Windows SDK 10.0.22621.0でビルドして確認しました。画像は最終描画先から読み戻した640×480のRGBデータを、色を変えずにPNGへ保存しています。

## 基本描画とポストエフェクト

赤いScene矩形、青い3D三角形、マゼンタのPNG画像、緑のUI矩形、日本語文字を同時に描きます。通常設定とFXAAを無効にした設定の両方で、各図形の内部の色と位置、文字領域の白画素を検査します。

![通常設定の2D・3D・画像・日本語UI](images/render-basic.png)

独自のtint処理でSceneの赤成分を下げると、矩形とPNG画像の色が変わります。処理後に重ねるUIの緑は同じ値を保ちます。テストではSceneの変化量とUIの一致を別々に判定します。

![Sceneだけにtintを適用した描画](images/render-tint.png)

## モデルの色と照明

`examples/assets/model_lighting.glb`の2球を使います。左は暖色の非金属、右は寒色の金属です。画像を持たない材質には、1×1の白い2D画像を使って材質の色を保ちます。

![基準方向の光で描画した2球](images/render-model.png)

方向光を90度回すと、明るい側と反射の位置が変わります。左右の領域に十分な有色画素があることと、方向変更によって画素が変わることを検査します。

![方向光を90度回した2球](images/render-model-rotated.png)

## 再実行

```bat
PRE_SETUP.bat --gpu-check
```

ビルド済みの環境で画像テストだけを実行する場合は、次を使います。

```bat
ctest --test-dir build/runtime-windows -C Release -R gkcore.render_capture --output-on-failure
```

PPM画像と判定値のJSONは`build/runtime-windows/render-captures/Release`へ出力します。取得に使うDLLは開発テスト専用で、配布SDKには含めません。インストール済みSDKの検査では、通常のRuntime DLLを使う別アプリをビルドし、初期化・描画・最初のPresent・終了を確認します。

この検査は代表画素と領域の条件を使います。全面画像の一致、全エフェクトの画質、連続フレームのちらつき、PBRの物理的な正確さを判定するテストではありません。公開サンプルは起動して表示を確認し、mixed_sceneでは最大化後の表示も確認しました。Spaceによる切り替えとEscape終了のキー操作、RuntimeのDebug構成、他のGPU、全モデル形式の描画は未確認です。

実装上の原因と修正前後の記録は[TDD検証ログ](TDD_LOG.md)、対応機能と残作業は[機能とサポート状況](ROADMAP.md)を参照してください。
