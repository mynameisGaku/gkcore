# ポストエフェクトの使い方

gkcore は Scene 層の 2D と 3D を HDR 描画先へまとめ、Bloom、露出・トーンマッピング、色調整、FXAA の順に処理してから UI 層を重ねます。たとえばゲームの背景やキャラクターに効果をかけたまま、HUD や文字を読みやすく表示できます。

## まずは少しだけ調整する

設定は初期化前にも変更できます。フレームごとに調整する場合は `gk::BeginFrame()` より前に setter を呼びます。次の値は、少し彩度を上げ、コントラストを加え、FXAA を有効にする例です。

```cpp
if (gk::SetSaturation(1.1f) != 0 ||
    gk::SetContrast(1.05f) != 0 ||
    gk::SetFxaaEnabled(true) != 0) {
    gk::Shutdown();
    return 1;
}
```

設定は `gk::BeginFrame()` で取り込まれます。フレーム途中に値を変えた場合は、次に始めるフレームから反映されます。ゲームの設定画面などから毎フレーム変更する場合も、`BeginFrame()` より前に setter を呼んでください。

## 設定できる値

| 関数 | 範囲・初期値 | 内容 |
|---|---|---|
| `gk::SetBloomEnabled(bool)` | 初期値 `true` | 明るい部分から光のにじみを作ります。 |
| `gk::SetBloomIntensity(float)` | `0` から `4`、初期値 `0.15` | Bloom の強さです。 |
| `gk::SetExposure(float)` | `0` より大きく `16` 以下、初期値 `1` | Scene 全体の明るさを調整します。 |
| `gk::SetToneMappingEnabled(bool)` | 初期値 `true` | HDR の明るさを画面向けの範囲へ変換します。 |
| `gk::SetSaturation(float)` | `0` から `2`、初期値 `1` | `0` で無彩色、`1` で補正なし、`2` で彩度を強めます。 |
| `gk::SetContrast(float)` | `0` から `2`、初期値 `1` | `1` で補正なし。値を上げると明暗の差が強まります。 |
| `gk::SetFxaaEnabled(bool)` | 初期値 `true` | 画面上の輪郭をなめらかにする処理を切り替えます。 |

数値設定へ NaN、無限大、範囲外の値を渡すと失敗します。エラー時は `gk::GetLastErrorMessage()` で理由を確認してください。各関数は有限値と範囲を検査し、失敗した値は適用しません。

## 適用順

![Scene の描画に効果をかけてから UI を重ねる順序](images/render-pipeline.svg)

Scene 層に描いた 2D スプライトや図形と 3D 描画をまとめて処理します。UI 層はこれらの効果の後に合成されます。層内では描画命令の順番を保ちますが、Scene と UI の呼び出し順は層の順序に従います。

`BeginFrame()` を呼ぶ前に効果の値を設定し、フレーム内では `Scene` 層へ 2D/3D のゲーム描画、`UI` 層へ HUD や文字を追加します。最後に `Present()` を呼びます。各関数の戻り値を確認し、失敗したときは `gk::GetLastErrorMessage()` で原因を調べてください。

この順序と設定 API は実装されています。Windows/MSVC でのリンクと実 GPU 上の見た目は未確認です。現在の適用状況と検証範囲は [機能とサポート状況](ROADMAP.md) を参照してください。
