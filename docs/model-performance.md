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

## 変形後の頂点接触

`--secondary-vertex-contact`を加えた初回は133.568FPS（DrawModel3.391ms）でした。登録したbone枝にweightを持つ位置番号を設定時に選別し、球・カプセルの軸を呼出し内で事前計算した後は、171.985FPS、p95 6.373ms、DrawModel1.811ms、Present2.243ms、IK設定.509ms、揺れ更新1.196msです。同じbatchのOFFは227.082FPS、p95 4.928ms、DrawModel.405ms、Present2.246ms、IK設定.510ms、揺れ更新1.186msでした。両方とも通常Release、1280×720、標準効果、VSyncなし、YUMEKA＋Silly Dancing / Capoeira blend＋右腕IK＋13鎖＋身体8形状、3000frame/warmup300を完走しました。

ONの追加費用は変形点の評価と接触判定です。GPUへ渡すのは変わった点だけで、法線はGPU上で補正後の位置から作り直します。CPU参照やGPU readbackを行うcaptureは速度計測に含めません。最適化前後の固定時刻48画像はbyte一致しました。300FPSはON/OFFとも未達で、今後はskin評価と接触候補の絞り込みを確認します。ログは`build/native-validation/vertex-contact-final-{off,on}-benchmark.log`です。平均FPSには実行時の負荷も含まれるため、全環境で同じ改善率になる保証はありません。

球・カプセルを確実に囲む範囲で遠方の点を除外する変更後、頂点接触ONは199.698FPS（p95 5.579ms、Draw .886ms、Present2.345ms、IK .516ms、secondary1.204ms）、同batch OFFは221.465FPSでした。両方3000frame/warmup300を完走しています。capture用の区間計測では4,172点のうち平均63.1点を補正し、接触計算は1.097285msから.163308ms、形状構築・skin評価を含む全体は1.462256msから.505134msへ減りました。captureの数値は通常viewerのFPSとは別の計測です。保存48画像は変更前後でbyte一致しました。

ユーザーの追加指示に従い、次はスカートboneの小球と脚boneのカプセルを明示し、可視化しながら形状の大きさを合わせます。頂点補正は任意の切替として保持し、骨同士の接触検証ではOFFにします。300FPSは未達です。最新記録は`build/native-validation/skin-contact-bounds-{on,off}-benchmark.log`、区間計測は`skin-contact-bounds-profile-comparison.json`です。

## 骨の小球と脚カプセル

START 12は頂点補正を使わず、スカート鎖の各boneと仮想末端へ半径0.008、髪・尻尾へ0.003 model unitの球を設定します。脚の上部カプセルは半径0.045、下部は0.035で、身体形状のmargin 0.005も判定と表示に含みます。鎖ごとの球半径を身体形状へ足して、節と節間の線分を同じ範囲で検査します。固定rootと、そのrootが身体内にあるときの最初の線分には既存の例外が残ります。

通常Release、1280×720、VSyncなし、YUMEKA＋Silly Dancing / Capoeira blend＋右腕IK＋13鎖＋身体8形状、接触表示OFF、3000frame/warmup300を完走しました。平均207.044FPS、p95 5.323ms、DrawModel .398ms、Present2.333ms、IK .512ms、揺れ更新1.530msです。300FPSは未達で、以前の半径0構成とは接触条件が異なるため速度改善の対照にはしません。ログは`build/native-validation/bone-sphere-benchmark.log`です。

接触表示は設定調整用です。START 12は起動時から表示し、Cキーで切り替えます。青は身体、橙は揺れbone、白は固定root、灰は仮想末端です。表示ありの短い30frame起動確認は初期画像生成の時間も含むため、FPSの判定に使いません。

## 接触形状の幅調整と重複検査の削減

START 12のスカート球は、実モデルの幅の中央値.0378 model unitへ合わせて.038としました。髪・尻尾の.003、身体形状、margin、反復数、角度は保持し、頂点補正はOFFです。同じ120frameの比較では身体形状へ侵入するSkirt点が平均44.708から16.517、最大79から32、最大深さ.04838から.03001へ減りました。前面と左右の画像でも確認しましたが、特に側面の面の埋まりが残っています。布面全体の品質合格ではありません。大きな.0768は骨長内で外へ届かず失敗したため不採用です。

同じ.038設定の3000frame/warmup300で、接触検査を減らす変更前は184.749FPS、変更後は190.296FPSでした。後者はp95 5.721ms、Draw .424ms、Present2.388ms、IK .509ms、揺れ更新1.876msです。通常Release、1280×720、VSyncなし、接触表示OFF、同じYUMEKAと2motion・右腕IK・13鎖・身体8形状を使用しています。前の.008設定207.044FPSとは接触条件が異なり、速度改善の比較にはしません。揺れ更新の変更前1.886msとの差は小さく、FPS差には計測時の負荷も含まれます。300FPSは未達です。

遠方の線分は親軸距離計算より先に保守的な範囲で除外し、厳しい接触検査が通った同じ座標の直後の通常検査だけを省きました。最適化前後24姿勢画像とDebug/Release24組はbyte一致しました。実GPUと画像検査の結果はbone-contact-early-out-image-comparison.json、速度記録はbone-contact-early-out-{before,after}-benchmark.logです。

## 球・カプセルの共通計算を再利用する

接触反復で使う軸、軸長二乗、半径、許容幅と範囲を、各Project/Check呼出しの中で形状ごとに一度だけ求める方式へ変更しました。固定rootの内外も反復前に保存します。半径.038、反復数、角度、margin、shape順、出力は維持し、頂点補正と接触表示はOFFです。2形状の契約例では軸準備が10回から2回になりました。

通常Release、1280×720、VSyncなし、YUMEKA＋Silly Dancing/Capoeira blend＋右腕IK＋13鎖＋8身体形状、3000frame/warmup300を変更前後とも完走。198.637→208.670FPS、変更後p95 5.272ms、Draw .388 / Present2.287 / IK .508 / secondary1.549msです。変更前secondary1.866msから約17%減りました。FPSには実行時負荷も含まれるため改善率を全環境へ保証しません。300FPSは未達です。ログはsecondary-prepared-{before,after}-benchmark.logです。

capture専用DLLの区間計測は固定delta1/60、初期20frame除外後100sampleで、solver2.708335→1.974252ms、全Update3.327076→2.575593msでした。captureは通常表示と時間刻み・検証条件が異なり、FPSの代用にはしません。計測コードは通常DLL/SDKに入りません。Debug/Release480frameのGPU位置・法線がvalid、保存24姿勢は旧版と両構成でbyte一致しました。スカートの侵入率は変わらず、見た目の残件も保っています。

## スカート鎖の間を結ぶ接触線分

START14では既存.038骨球・8身体形状へ、自由bone間の半径.038接触線分10本を加えます。両端を身体から押し出し、既存の骨長・角度・鎖接触を再確認するため、更新時間が増えます。頂点補正とdebug表示は速度計測でOFFです。

通常可視Release、1280×720、VSyncなし、標準効果、YUMEKA＋Silly Dancing/Capoeira blend＋右腕IK＋13鎖、3000frame/warmup300を同batchで完走しました。接続OFF207.058FPS、p95 5.378ms、secondary1.551ms、輪状ON177.136FPS、p95 6.869ms、Draw .397/Present2.232/IK .508/secondary2.450msです。形状の条件が違うため速度最適化の比較ではありません。300FPSは未達です。ログはsecondary-links-{off,ring}-benchmark.log。

loop120frameで最大侵入深さ.030013→.020639、平均侵入点16.517→15.833と限定的に改善しましたが、面の埋まりは残ります。自由bone原点間の接触を支える機能で、リンク長固定や布全体の非貫通は保証しません。debug表示付き30frameの起動確認はFPS判定に含めません。

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

## 接続形状の準備を共有する

START14と同じ10接続・半径.038・身体8形状・13鎖で、同じUpdate内の接続端点検索と、同じ半径の身体形状準備を一度にまとめました。同じ身体列を前後入力へ渡す鎖更新では、半径拡張も一度にします。接触順、骨長・角度・反復数、最終描画FK検査は維持しています。

通常可視Release1280×720、標準効果、VSyncなし、debug表示と頂点補正OFF、3000frame/warmup300を変更前後とも完走しました。176.127→174.119FPS、揺れ更新2.462→2.436ms、後p95 6.968ms、Draw .421/Present2.314/IK .512msです。更新時間の短縮は小さく、FPS改善は確認できませんでした。300FPSは未達です。ログはlink-cache-{before,final}-benchmark.logです。

capture専用DLLの固定delta1/60・初期20frame除外後100sampleはsolver3.309927→3.260583ms、全Update3.924160→3.865686ms。通常表示とは更新間隔・検証条件が違うためFPSへ換算しません。Debug/Release480frameが参照と一致し、保存24姿勢の48画像は変更前とbyte一致しました。布面の貫通は保持されており、品質を改善した結果ではありません。次は鎖制約を繰り返す費用を区間ごとに調べます。

## 鎖制約の参照方向と接触準備

devではsubstep内の一定な目標方向・骨長と、半径ガード後の身体形状を反復間で共有しました。投影順、骨長・角度、root例外、float保存後と描画FKの最終検査は維持しています。

固定delta1/60のcaptureでは、参照計算平均2216.88→241.20、solver3.292944→2.967918ms、全Update3.898166→3.572878msでした。通常可視Release1280×720、VSyncなし、標準効果、debug表示/頂点補正OFF、YUMEKAと2motion・右腕IK・13鎖・身体8形状・接続10本、3000frame/warmup300の完走結果は171.990→173.519FPS、secondary2.437→2.288msです。captureを通常FPSへ換算しません。300FPSは未達で、布面の貫通も保持しています。

最初の可視計測はwarmup中の接触失敗で未完走でした。失敗を調べるdelta trace/replayを追加し、同じ実測3300frameと変動1200frameを変更前後で再生して双方完走しましたが、元の失敗は再現できていません。この区切りのmain反映は保留です。ログはreference-cache-{before,retry}-benchmark.log、reference-cache-final-benchmark-failed.log、constraint-delta-{trace-1,jitter}-{replay,legacy}.logです。

## 接続候補の誤差余白

接続候補の接触判定では既存許容幅の半分を使い、残りを姿勢をFKへ戻す丸め用に残します。最終描画の許容幅と反復上限は保持します。追加補正が必要になり、固定120frameの参照計算はstart25880→27480、loop28160→29920でした。接触の安定性のための変更で、処理速度の改善ではありません。

通常可視Release1280×720、VSyncなし、標準効果、YUMEKA・2motion blend・右腕IK・13鎖・身体8形状・接続10本、debug表示と頂点補正OFFの3000frame/warmup300は162.817FPS、p95 7.897ms、Draw .417/Present2.428/IK .550/secondary2.687msで完走しました。前回173.519FPSからの改善は確認できず、300FPSは未達です。別batchの計測なので差をすべて変更の費用とは断定しません。

候補とFKの接触境界を跨ぐ純poseテストを固定しましたが、以前のwarmup失敗の原因同定は未完了です。変更後の実測/変動delta再生4500frame、実時間trace1200frame、通常計測3300frameは完走しました。ログはlink-roundtrip-benchmark.log、link-roundtrip-realtime-trace.log、constraint-delta-{trace-1,jitter}-visible.logです。この区切りのmain反映は保留を続けます。次は接続投影で位置が変わらない鎖への再制約を調べ、丸めや接触結果を保って減らせる処理を選びます。

## 接続の成立後に不要な鎖補正を省く

接続鎖は初回に長さ・角度・接触を必ず検査し、後続は位置が変わった鎖と、接触不成立の線分の両端鎖を補正します。一定の鎖への再計算を減らし、固定120frameの参照計算総数はstart27480→24616、loop29920→25332になりました。

通常可視Release1280×720、VSyncなし、標準効果、YUMEKA・2motion blend・右腕IK・13鎖・身体8形状・接続10本、debug表示/頂点補正OFFの3000frame/warmup300を同batchで前後とも完走しました。168.090→168.347FPS、揺れ更新2.499→2.382ms、後p95 6.879ms、Draw .526/Present2.458/IK .517msです。揺れ更新は短くなりましたが、全体FPSの改善は小さく、300FPSには届いていません。記録はconstraint-dirty-{before,after}-benchmark.logです。

Debug/Release各2区間×120frameのGPU・Skirt点参照はすべて有効でした。構成間24組の画像と全skin記録が一致し、変更前との差も保存画像で最大3pixelでした。布の侵入は残ります。実測と変動deltaの4500frame再生も完走しましたが、以前のwarmup停止の原因は未確定です。次は同じStep0内で繰り返す形状・目標線分の準備を調べます。
