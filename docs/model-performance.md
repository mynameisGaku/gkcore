# モデルviewerの処理時間

目標は、実モデルを表示しながら300FPS以上を出すことです。測定には通常のRuntime DLLを使う`gkcore_model_benchmark_visible`を使用します。VSyncを無効にし、1280×720、標準のBloom・トーンマッピング・FXAA、基本色画像付きのYUMEKAで確認します。モデルを省略した描画や、停止したアニメーションの値ではありません。

## 現在の測定結果

Windows 11 Pro build26200、RTX 4070 SUPER / driver617.42、VS2026 / v14214.29.30133（MSVC19.29.30159）、Windows SDK10.0.22621.0、Release構成で測定しました。FPSは起動直後のwarmupを除いた総frame数を実経過時間で割っています。p95は測定frameの95%がその時間以下だったことを表します。

| 表示内容 | 測定frame / warmup | 平均FPS | p95 frame時間 |
| --- | --- | --- | --- |
| YUMEKA 静止 | 3000 / 300 | 340.020 | 2.976ms |
| YUMEKA 毎frame回転 | 3000 / 300 | 340.020 | 2.974ms |
| YUMEKA + Silly Dancing / Capoeiraのブレンド | 3000 / 300 | 324.762 | 3.497ms |

ブレンドは太もも・すねの対応漏れを修正後、両motionを51本の骨へ適用した測定です。再生は計測用コードを含めない通常のRelease Runtimeで、約9秒の連続計測を完走しました。平均300FPS以上を確認した結果で、全frameが3.333ms以下だったことや、全モデル・他GPUで同じFPSが出ることを保証するものではありません。GPU変形追加時点のnative全体検査はDebug/Release各78件が成功しました。脚対応修正後はCPU各53件と、animation・GPU画像・SDK consumerを含む関連統合検査各9件が成功しています。測定ログは`build/native-validation/final-real-model-{static-,rotate-}benchmark.log`、脚対応修正後のブレンドは`mixamo-legs-benchmark.log`です。

## 再計測

ビルド済みReleaseの実行ファイルをPowerShellから起動します。次の例はローカル検証用素材を使用し、素材自体はリポジトリやSDKへ含めません。

```powershell
$env:GKCORE_BENCHMARK_FRAMES = '3000'
$env:GKCORE_BENCHMARK_WARMUP = '300'
& ./build/runtime-windows/Release/gkcore_model_benchmark_visible.exe ./build/local-assets/Yumeka/FBX/Yumeka_v1.0.4.fbx 1.1166145684 0 .671678712 -.2517482195 external-blend 'C:/Users/g0190/Downloads/Silly Dancing.fbx' 'C:/Users/g0190/Downloads/Capoeira.fbx' --materials ./build/local-assets/Yumeka/Prepared/material-config.txt
```

`external-blend`と2つのmotion引数を`static`に替えると静止表示、`rotate`に替えると毎frame回転を測定します。結果のJSONは測定完了、平均FPS、p95 frame時間、`DrawModel`と`Present`の平均時間を記録します。benchmarkではFPS文字列の更新による文字画像生成の負荷を避け、固定のラベルを表示します。普段のviewerはFPSを約0.5秒ごとに更新します。

GPU試験は同時に複数起動せず、比較する際は構成・解像度・素材・効果・driverを揃えてください。非表示のcapture用benchmarkは画像検査と調査に使い、通常表示のFPSの代わりにはしません。

## 修正と検証の考え方

透明三角形を追加するたびに頂点配列を必要数ちょうどへ拡張していた処理を、まとめて追加する方式へ変更しました。静止表示は0.189FPSから24.120FPSになりました。さらに元の形状をGPUへ保持し、モデルとカメラの変換をshaderで行うことで340FPSまで改善しています。透明部分の並び順、深度、材質画像とScene/UIの合成は維持します。

アニメーションは描画時点の姿勢を保持し、後から時刻やhandleを変更しても予約済み描画を変えません。FBXの高速経路は対応できるskinと法線生成に限定し、morphなど対象外の入力は既存の評価へ戻します。変形結果を全頂点で従来のufbx評価と比較し、GPU画像でも姿勢・法線・合成を確認します。具体的な試験と範囲は[TDD検証ログ](TDD_LOG.md)と[描画検証](render-validation.md)に記録します。
