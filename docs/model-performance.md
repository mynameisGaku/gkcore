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

動作中の右腕IKを加えたSilly Dancing / Capoeiraの測定は、前回269.801FPS、骨番号を起動時に解決する変更後の今回210.209FPS（p95 5.588ms、DrawModel平均1.100ms、Present平均3.059ms）でした。両方とも3000frame/warmup300ですが、同時刻の有効な変更前対照はありません。今回の変更によるFPS改善や300FPS達成を確認した結果とは扱いません。骨番号検索は毎frameから初期化時へ移し、元の12姿勢画像は全画素一致しました。最新ログはik-perf-binding-benchmark.log、途中で閉じた計測はik-perf-baseline.logで完了していません。追従表示の処理時間はこの測定へ含めていません。

## IKと揺れものの測定

同じ実モデルのブレンド＋右腕IKで、祖先の回転積算を必要な部分へ絞った前後を測定しました。変更前214.655FPS、変更後272.239FPS、入力検査で不要なquaternion正規化を省いた後273.666FPSでした。いずれも3000frame / warmup300です。入力の受入条件と実際の姿勢正規化は保ち、固定時刻の画像は全画素一致しました。旧測定との差には実行時の負荷も含まれるため、全環境で同じ改善率が出ることは保証しません。

髪2鎖・スカート10鎖・尻尾1鎖と追従表示を加えた最新計測は、188.150FPS、p95 5.610msでした。DrawModelは平均0.848ms、Presentは2.428ms、IK目標の設定は0.512ms、揺れもの更新は1.045msです。通常Release DLL、1280×720、標準効果、Silly Dancing / Capoeiraのブレンド＋右腕IK、3000frame / warmup300で完走しました。初回183.064FPS、更新1.094msの測定も完走していますが、この差だけで追加の速度改善を断定しません。揺れものを含む300FPSは未達です。

最新ログは`build/native-validation/secondary-motion-final-benchmark.log`、初回は`secondary-motion-benchmark.log`です。途中で閉じた630frameの測定は完了結果に含めません。追加処理の計測はbenchmarkだけで行い、通常viewerへ計測用配列は含めません。

身体形状8個を加えた構成の最新計測は172.024FPS、p95 6.400msでした。DrawModel平均0.866ms、Present 2.290ms、IK設定0.512ms、揺れもの更新1.670msです。骨長・角度を戻した後も接触が解けていれば反復を終了し、float回転からの再構成に備えて計算中だけ数値上の余白を取ります。既存の半径・余白・最大角度の設定は保持します。初回の全反復構成は98.748FPS、更新5.883msでした。両方とも通常Release、1280×720、3000frame / warmup300で完走しています。300FPSは未達です。

途中で接触境界の再構成に失敗した605/127frameの測定は完走結果へ含めません。最終記録は`build/native-validation/secondary-contact-final-benchmark.log`、初回は`secondary-contact-benchmark.log`です。

複数の鎖の向きを一括で合わせる変更後は188.537FPS、p95 5.827msでした。DrawModel平均0.852ms、Present2.255ms、IK設定0.515ms、揺れもの更新1.199msです。13鎖がそれぞれ行っていた全骨格のFKと全pose複製を一度にまとめました。基準姿勢を作るFKと接触検算のFKは残し、physics solver・半径・角度・反復設定は同じです。前回172.024FPSからの変化には計測時の負荷も含まれますが、揺れ更新の重複計算は契約テストで削減を確認しています。300FPSは未達です。

通常Release 1280×720、3000frame / warmup300で完走しました。記録は`build/native-validation/secondary-batch-benchmark.log`です。

instanceごとの基準姿勢cacheを加えた身体形状8個・13鎖の測定は226.342FPS、p95 4.939msでした。DrawModel平均0.421ms、Present 2.243ms、IK設定0.509ms、揺れもの更新1.189msです。通常Release DLL、VSync無効、1280×720、標準効果、YUMEKAとSilly Dancing / Capoeiraのブレンド＋右腕IK、3000frame / warmup300で完走しました。前回188.537FPSからの差には計測時の負荷も含まれるため、cacheによるFPS改善率や他環境での再現を保証しません。300FPSは未達です。ログは`build/native-validation/pose-cache-benchmark.log`です。

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
