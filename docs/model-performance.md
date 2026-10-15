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

## 同じ半径の身体形状準備を共有する

接続補正のStep0で使う身体形状を、同じ更新内の節半径ごとに一度準備します。元の半径拡張・補間・guard・float化の順序を保持し、最終raw/FK検査も続けます。同半径3鎖・2bodyの準備は14→10件となりました。

通常可視Release1280×720、VSyncなし、標準効果、YUMEKA・2motion blend・右腕IK・13鎖・身体8形状・接続10本、debug表示/頂点補正OFFの3000frame/warmup300を前後とも完走しました。170.854→175.739FPS、揺れ更新2.384→2.317ms、後p95 6.643ms、Draw .430/Present2.370/IK .516msです。改善は小さく、300FPSには届いていません。ログはstep-shapes-{before,after}-benchmark.logです。

Debug/Release計480frameのGPU・Skirt点参照が有効で、全skin記録と48画像が変更前と一致しました。布面の貫通は保持されています。実測delta3300frameの隠し再生と変動1200frameの可視再生も完走しました。以前のwarmup停止の原因は未確定です。次は隣接鎖の間隔を保つ距離制約を検討します。

## 隣接する自由boneの距離制約

START15の試験設定では13鎖・身体8形状・接触線分10本に、基準距離を登録姿勢から測る距離制約10組を追加します。柔らかさは0.001、接触線分と距離の共通反復上限は32です。既存の球半径・身体形状・最大曲げ角・頂点接触OFFは保持します。

通常可視Release1280×720、標準効果、VSyncなし、debug表示/頂点補正OFF、3000frame/warmup300の計測は、warmup後340frameで接続線分1・身体形状4の接触が未収束となり終了しました。completed=falseの151.599FPSは途中の値で、完走時の性能として採用しません。delta記録付き再試行は3300frameを完走しています。ログはdistance-final-benchmark.log、distance-diagnostic-benchmark.log、記録はdistance-real-delta-trace.txtです。一度の完走で停止原因が解決したとは扱いません。300FPSの条件は未達です。

固定delta1/60では開始0秒とloop境界3.35秒から各120frameをDebug/Releaseで再生し、GPU位置・法線とSkirt点参照を確認しました。計480frameが有効で、保存24組の画像が一致します。loopの平均侵入点数は15.833→15.483ですが、最大深度は.020639→.023409へ増えています。距離制約で布面の品質を改善したとは判定しません。

30/60/120Hzでも3.35秒から同じmotion時刻4.25秒まで28/55/109frameを再生し、計192frameのGPU変形と更新成立を確認しました。鎖を内部substepで積分してから距離をframe末尾で解くため、更新頻度が異なる結果は完全一致を保証しません。実時間停止の再現と、接触・距離・鎖復元の残差の切り分けを続けます。

記録した今回の3300 deltaを距離制約ON/OFFで再生し、両方completed=trueを確認しました。距離OFFの現在sourceで追加240frameを描画し、前commitの24画像と全Skirt点参照JSONが一致しています。保存画像は画素一致を確認してlossless PNGへ変換し、重複したDebug/旧版のPPM、今回使い終えたtrial設定を片づけます。最新Release ON/OFF画像、元asset、現在build、成功/失敗ログとdelta traceは保持します。再生ログはdistance-real-delta-replay-{on,off}.log、画像/skin比較はdistance-image-comparison.json、distance-off-and-rate-comparison.json、整理記録はdistance-cleanup.jsonです。


## 骨長復元で消される接触補正を変える

距離制約付きの実時間停止を611 deltaで再現しました。更新秒数.0102454の最後2反復では、接触線分1の身体形状4への侵入が投影後には解け、鎖復元後に95.384/93.002マイクロmodel unit戻ります。距離残差は13.083/12.712マイクロunitで、主要な残りは線分接触でした。32回反復でも通常法線への補正が骨長復元で小さくなります。反復の後半だけ親位置と曲げ角に沿う方向へ補正し、分母にもその方向の接触改善量を反映しました。半径・角度上限・接触許容幅・反復上限は保持します。

同じGPU・Release1280×720・標準効果・VSyncなし・debug表示/頂点補正OFF・YUMEKA2motion blend+右腕IK+13鎖+身体8形状+接触線分10本で、距離10組あり3000frame/warmup300が155.443FPS、p95 7.741ms、Draw .390/Present2.238/IK .509/secondary3.239msで完走しました。距離なしも178.505FPS、p95 6.479ms、Draw .492/Present2.264/IK .513/secondary2.275msで完走。未完走151.599FPSとの速度改善比較は行いません。目的は接触停止の修正で、300FPSは未達です。固定2区間の保存画像とSkirt侵入は変わらず、布面の品質改善とは判定しません。


## 鎖復元の作業配列を更新内で共有する

ゼロ時間の鎖復元で使う位置・速度・旧位置・目標・基準線分の5配列を、1回の接続制約更新中だけ共有します。内容は各呼び出しで空にして再構築し、設定・目標・状態の検証、計算順、参照方向の再計算、全接触/FK検査は保持します。candidate状態2配列と最終raw接触検査1配列は引き続き確保するため、温まった作業領域では8→3 allocationとなります。

通常可視Release1280×720、標準効果、VSyncなし、YUMEKA2motion blend+右腕IK+13鎖+身体8形状+接触線分10本+距離10組、debug表示/頂点補正OFF、3000frame/warmup300を前後とも完走しました。145.974→153.213FPS、secondary3.331→3.260msです。Draw .512→.383/Present2.439→2.314msにも差があるため、全体FPSの差を作業配列共有だけの効果とは断定しません。300FPSは未達です。

同プロセスのv142 Releaseでraw/prepared処理と同じゼロ時間復元をfresh領域/共有領域で交互に各10000回・8batch測ると、fresh平均1.16655µs、shared1.08369µsでした。遠方カプセルと3点の限定した計測で、実モデル全体の性能へ換算しません。固定deltaの別batch比較ではsolverがstart4.035→4.520ms、loop4.488→4.931msと遅く見える結果もあり、captureの速度改善は確認できていません。全480frameのSkirt参照JSONと保存画像は前版と一致しています。次は繰り返しの接触計算とbody形状・基準値の準備費用を測ります。

## prepared Avoid形状の共有

鎖の方向回避では、同じguard済み接触形状の軸、軸長の二乗、桁あふれを避けて求める軸長、nextafterで外向きに広げた境界をshape準備時に作り、反復中に再利用します。通常の半径範囲も別に保持します。親位置・方向・長さの検査、毎回変わる最近点とcone計算、root内の最初の節の例外、接触投影後のraw/FK検査は維持します。接触投影専用のraw wrapperは追加の軸長計算やnextafterを行いません。鎖更新で完全なprepared shapeを作る場合は形状ごとに軸長と外向き境界も準備します。内部prepared値は144 byteから200 byteになります。この変更に公開APIやABIの追加はありません。

Windows 11 build 26200、RTX 4070 SUPER、v142 / MSVC 19.29、Windows SDK 10.0.22621.0のReleaseで、1280×720・VSyncなし・標準効果・YUMEKAの2motion blend・右腕IK・13鎖・身体8形状・接続10本・距離10組、debug表示/頂点補正OFFの3000frame/warmup300を前後とも完走しました。152.809→157.854FPS、揺れ更新3.269→2.885msです。後のp95は7.436ms、Draw .392→.450ms、Present 2.313→2.426ms、IK .512→.518msでした。全体FPSの差をこの変更だけの効果とは断定せず、300FPSも未達です。固定delta1/60の開始0秒・3.35秒からDebug/Release各120frame、計480frameのGPU位置・法線とSkirt1,314点の検査が有効で、全Skirt記録と保存24組の画像が変更前と一致しました。最初の20frameを除くsolver平均は開始区間4.040→3.438ms、loop4.478→3.847msで、参照計算回数も変わりません。以前停止した611 deltaの再生はDebug/Releaseで完走しました。布面侵入は開始区間平均16.008点・最大深さ.017313、loop平均15.483点・最大深さ.023409のまま残り、品質改善とは扱いません。

## 接触投影の候補配列を更新内で共有する

鎖復元で共有している作業領域に接触投影の候補位置用配列も加えました。投影の計算順と全検査を保ち、成功した場合だけ候補を確定位置へコピーします。失敗時の位置はそのままです。従来の単独投影wrapperの動作は変更していません。温まったsolverの配列確保は状態用2回と投影用1回から状態用2回になり、投影用の確保は0回です。

Windows 11 build 26200、RTX 4070 SUPER、v142 / MSVC 19.29、Windows SDK 10.0.22621.0の通常可視Release計測です。1280×720、標準効果、VSyncなし、YUMEKAの2motion blend・右腕IK・13鎖・身体8形状・接続10本・距離10組、debug表示/頂点補正OFFで3000frame/warmup300を前後とも完走しました。161.790→162.787FPS、揺れ更新2.903→2.872ms、p95 7.318→7.257msでした。Draw .387→.389ms、Present 2.319→2.329ms、IK .514→.497msです。差は小さく、全体FPSの変化を配列共有だけの効果とは断定しません。300FPSは未達です。

同じv142 processで8batch×10000回を交互に測った投影kernelでは、遠方点が0.217845→0.202663µs、球接触が0.515128→0.493554µsで、どちらも結果byte一致でした。この限定した計測値を実モデル全体へ換算しません。固定delta1/60の開始0秒/3.35秒からDebug/Release各120frame、計480frameのGPU位置・法線とSkirt1,314点が有効で、全Skirt記録と保存24画像は変更前と一致しました。最初の20frameを除くsolver平均は開始区間3.442380→3.455005ms、loop3.860263→3.871968msで、固定delta計測の速度改善は確認できませんでした。参照計算回数349.2/350.8と布面侵入は同じです。以前停止した611 delta再生も両構成で完走しています。


## 動作を優先する作業方針

2026-10-13のユーザー指示に合わせ、微小なFPS改善の追跡を中断しました。次の作業は実モデルのアニメーション、ブレンド、IK、揺れ、接触を組み合わせた動作の成立と、再生が停止する条件の解消を優先します。300FPSは引き続き未達で、今回の骨ローカル接触機能を速度改善とは扱いません。


## 重み付き衣服接触の動作確認

START18はSkinning Test・Swingingの外部blend + IKへ、13鎖・身体8形状・複数boneの実weightで動く465接触球を重ねます。スカートの骨球0.038と脚mask240を保ち、脚上げで折り返せる角度180度を使います。Debug/Releaseの固定1/60秒と実時間で、それぞれ3,000frameを完走しました。骨長・曲げ角・身体接触・描画FKを検査して確定し、頂点位置は補正しません。

Windows11Pro build26200、RTX4070SUPER driver617.42、v142/MSVC19.29、SDK10.0.22621.0、1280×720、VSyncなし・標準効果・debug表示OFF・非表示windowのRelease実時間3,000frame/warmup0は95.079FPS、p95 10.628ms、Draw .411/Present2.884/IK .505/secondary6.665msでした。接触設定・実装・確認範囲が従来の179球構成とは異なるため、速度改善の対照比較には使いません。今回の目的は衣服位置の接触と停止の修正で、300FPSは未達です。記録はbuild/native-validation/skin-coverage-complete-release-real-3000.logへ保存しています。

## スカートの連続動作を確認する構成

接触の位置補正による人工速度を除き、前回からの物理予測を先に一括接触へ渡しました。START18の検査motionは往復再生です。近い合法姿勢が得られた後も候補を比較し続ける試作は、実時間1,000frameのdelta平均0.160508秒、114回の250ms以上の更新間隔となり、通常表示にも不向きでした。保存後の全制約を満たす候補を縮約し、その最初の成功で探索を終了します。

同じWindows/GPU/toolchain、1280×720、非表示・標準効果・VSyncなし、13鎖・身体8形状・465球、Release実時間600frame/warmup0は31.025FPS、p95 31.645msでした。Draw .443、Present5.007、IK .554、揺れ更新26.152msです。更新内訳の記録を有効にした値で、往復再生と探索基準も変えているため、以前の95.079FPSとの単純な速度比較には使いません。動作は継続しましたが、300FPSは未達です。記録はbuild/native-validation/skirt-stability-bounded-search-realtime-600.logです。

## 先頭の骨の回転による衣服接触を加えた構成

START18は529球です。既存465球を保ち、先頭のboneから影響されるスカート位置64球を追加しました。骨原点だけでは支えられなかった腰周りを扱い、開始・折り返しの各120frameで検査したスカート440点の侵入は0になりました。これは身体capsuleへの近似判定で、シャツやジャケットの侵入と全衣服面の保証は残ります。

同じWindows/GPU/toolchain、1280×720、非表示・標準効果・VSyncなし、13鎖・身体8形状、Release実時間600frame/warmup0は16.637FPS、p95 41.451msでした。Draw .646、Present6.436、IK .857、揺れ更新52.076msです。固定1/60秒3,000frameと実時間600frameは完走しましたが、接触を増やした構成の負担は残っています。300FPSは未達です。記録はbuild/native-validation/root-coverage-realtime-600.logです。

## 接触補正の重複計算を減らした確認

接触点の微分ベクトルは出力軸や子位置軸によらず方向軸だけで決まります。同じ式を27回評価していた処理を3回へまとめ、微分式、各成分の加算順、位置・速度、接触半径、探索順は保ちました。現行529球・13鎖・身体8形状の実時間600frame/warmup0は32.587FPS、p95 29.286ms、Draw .487/Present5.250/IK .575/secondary24.309msでした。前回は16.637FPS/secondary52.076msですが、実時間の進み方が異なるため固定負荷の比較ではありません。Windows11Pro26200、RTX4070SUPER/driver617.42、v142/MSVC19.29、SDK10.0.22621.0、1280×720、非表示・標準効果・VSyncなしの条件です。300FPSは未達です。

ジャケットの接触追加も試しましたが、981球・750球は固定600frameで大きな跳ねと実時間再生の停止があり、636球も3,000frame検査の755frame目で停止しました。636球は短い描画検査でスカート・ジャケットの侵入0、実時間600frame完走でも、その後の動作を保てなかったため採用していません。START18は既存529球を使います。シャツ・ジャケットの残留侵入は未解決です。755frameの入力には保存後も全制約を満たす姿勢があることを確認し、骨方向を同時に補正する再探索で解決しました。保存した後続757frame入力もCPU再生で解決しましたが、最終の636球の長時間再生は別の757frame入力で停止しました。途中構成の758frame入力では複数の独立した探索も完全な姿勢を得られておらず、いずれも幾何的な不成立とは断定していません。START18は529球を維持します。

記録はbuild/native-validation/cloth-coverage-current529-factored-realtime.log、cloth-coverage-trial-636-quality.json、cloth-coverage-temporal-summary.json、cloth-coverage-final-fixed-3000.logです。新しい接触設定の採用は、短い画像検査に加えて長時間の連続動作を確認して判断します。

採用する529球・共通計算削減後のReleaseは、固定1/60秒3,000frameを完走しています。記録はbuild/native-validation/cloth-coverage-stable529-fixed-3000.logです。

固定600frameでは、記録した全boneの更新前・更新後位置も前回と全frame一致しました。


## 現在姿勢に近い接触解を先に探す

重み付き接触では現在方向から一回だけ局所連立補正を行い、成立しない場合に広い候補探索へ進みます。失敗した探索の最良途中姿勢を次へ引き継ぎ、連立候補もすべて失敗した場合は最良候補から一度だけ継続します。候補ごとの反復上限32、保存後の全骨長・角度・接触条件を保持します。全体の試行上限は追加候補の分だけ増えます。

Windows11Pro26200、RTX4070SUPER driver617.42、v142/MSVC19.29、SDK10.0.22621.0、非表示window・1280×720・VSyncなし・標準効果、YUMEKA2motion blend+右腕IK+13鎖+身体8形状+529球・1574 anchors、頂点補正/debug表示OFFの実時間600frame/warmup0は26.175FPS、p95 45.653ms、secondary31.919ms、Draw0.447/Present5.190/IK0.584msで完走しました。前の600frame記録40.246FPSより遅く、300FPSは未達です。

固定1/60秒3000frame・50秒の時系列では、局所探索を先に行わない比較構成から採用構成へ、segment最大変化110.808→59.187度、45度超19→2回、90度超4→0回となり、root相対最大点移動も0.16903→0.06994へ減少しました。segmentのp99は14.721→16.372度に増えています。過去の物理結果が次の入力に戻るため、同一入力pose比較ではありません。最終DLLで全3000frameのtraceが試作と完全一致しました。

開始0秒・2.1秒からDebug/Release各120frameのGPU変形、全mesh記録、保存画像が一致しました。Skirt確認点の侵入は両区間0、Jacketは折り返し区間平均1.675点・最大0.011648、Shirtは平均11.633点・最大0.014894で、一部の埋まりは残ります。最大変化frame2214の画像も確認済みです。636球への拡張と半径縮小の試作は、停止または大きな跳ねが残るため不採用とし、START18の設定は変更していません。次は残る接触点と約59度の急変を切り分け、局所連立補正の計算量も扱います。記録はcontact-continuation-*です。


## 骨長と接触をまとめて速度補正する

重み付き接触の位置が成立した後、骨長を保つ曲げ速度だけを動かし、複数の接触面へ戻る速度をまとめて補正します。身体形状・半径の変化と、アニメーションで変わる接触定義の速度も含めます。固定rootを保ち、32回で全速度条件を満たせない場合は候補を捨てて既存速度を使います。位置の接触条件は保持しています。

Windows11Pro26200、RTX4070SUPER driver617.42、v142/MSVC19.29.30159、SDK10.0.22621.0、非表示window・1280×720・VSyncなし・標準効果、YUMEKA＋Skinning Test/Swinging blend＋右腕IK＋13鎖・身体8形状・529球・1574 anchors、頂点補正/debug表示OFFのcapture用Release実時間600frame/warmup0は19.550FPS、p95 55.261ms、secondary43.144ms、Draw0.592/Present6.605/IK0.730msで完走しました。前の26.175FPSより遅く、300FPSは未達です。実時間の進み方が異なるため、FPS差を固定負荷の増加率とは扱いません。成否を明示enumに変更する前の同じ計算方式の計測であり、最終enumバイナリを再計測した値ではありません。この非表示計測はcapture DLLを使っており、通常windowを使う製品DLLの性能とは区別します。

固定1/60秒3000frameの最大segment方向変化は59.187→51.385度、45度超2→1回、90度超0回、p99 16.372→15.811度となりました。root相対最大点移動は0.069943→0.067748 model unitです。過去の物理結果が次の入力へ戻るため、同一入力pose比較ではありません。最終enumバイナリで759frameを再生し、3000frameの同区間と完全一致しました。最大変化frame757→758の51度は残り、動作の完成とは判定しません。

Debug/Release各2区間×120frame、計480frameのGPU位置・法線、全mesh記録と画像が一致し、最終enumバイナリでも折り返し2frameずつを再確認しました。Skirt確認点の身体capsuleへの侵入は両区間0ですが、折り返し区間のJacketは平均1.916667点・最大0.014362224、Shirtは平均11.858333点・最大0.017666648へ悪化しました。布面全体の非貫通は未確認です。次は身体・鎖・重み付き接触を共通の時間分割で処理し、frame末尾で発生する大きな位置補正を減らす方式を検討します。記録はcontact-nearby、contact-nearby-geometry、contact-velocity-gpu-*、contact-velocity-enum-gpu-*、contact-failure-nearby-velocity-*です。

## 共通の時刻で予測と衣服接触を進める

身体・全鎖・重み付き接触を同じ1/120秒以下の区間で進め、予測の骨長復元を人工速度へ変えないよう修正しました。doubleで成立した速度をfloatの丸めだけで捨てる条件も修正しています。Windows11Pro26200、RTX4070SUPER driver617.42、v142/MSVC19.29.30159、SDK10.0.22621.0、YUMEKA＋Skinning Test/Swinging blend＋右腕IK、13鎖・身体8形状・529球・1574 anchors、頂点補正OFFです。

固定1/60秒3000frameは完走し、旧6c316a8からsegment最大方向変化51.385105→34.330172度、p99 15.811102→12.628354度、45度超1→0回、root相対点移動p99 22.942065→17.978148mmとなりました。前回の物理結果が次の入力へ戻る時系列比較です。Debug/Release計480frameのGPU変形も有効で、保存画像と全mesh記録が一致しました。Skirt確認点の侵入0に対し、折り返しのJacket平均0.975点・最大0.0126901134、Shirt平均10.75点・最大0.0154593591の侵入は残ります。

この時点のcapture用Release実時間600frameは112frameで接触探索が停止しました。completed=false、4.301FPS、p95 762.927ms、secondary215.432msであり、成功した600frame計測ではありません。非表示window・1280×720・VSyncなし・標準効果・debug表示OFFで、通常製品DLLの表示性能とは区別します。記録付きの再試行も0.4312807秒の更新で停止し、49deltaの再生で同じframe48失敗を再現しました。遅い更新が0.25秒以上のresetを招く連鎖があるため、この構成はmainへ昇格せず、候補評価の重複とreset後の位置探索を調べています。300FPSと揺れもの全体の完成は未達です。記録はshared-step-baselineとshared-step-diagnosticsです。

## 予測の刻みを保って接触の回数を制限する

候補探索と保存後検算で不要なJacobian計算を省き、位置・侵入量の計算順は維持しました。長時間resetの基準姿勢から接触が解けない場合は、以前の速度を使わず、前回成立方向を現在rootと骨長へ写して再試行します。この構成のcapture用Releaseはdelta記録付き600frameを完走しましたが、平均delta0.250073秒、0.25秒以上200回/600、最大5.10150秒でした。同じSDKの通常製品DLLで可視600frameを計測すると174frameで停止、completed=false、3.813FPS、secondary249.961msとなりました。短い固定刻みの成立だけでは実時間の安定性を保証できません。

接触探索の回数が遅延に伴って増え続けることを避け、位置接触を公開Update内で最大2回にしました。予測は1/120秒以下の刻みと各時刻の局所TRS・FKを維持します。速度補正には最後の予測直前の姿勢・身体形状とその刻み幅を使い、macro平均root速度への共通shiftは行いません。1/60秒を超える更新では接触間隔が長くなるため、同じ軌跡を保つ最適化ではなく時間精度の異なる設計です。公開終端の全検査は維持します。

最終b9fe51cの通常製品DLL、可視1280×720・VSyncなし・標準効果・debug表示/頂点補正OFF、同じYUMEKA・2motion blend・右腕IK・13鎖・8身体形状・529球・1574 anchorsの単独実時間600frame/warmup0は完走しました。17.929FPS、p95 63.230ms、secondary49.415ms、Draw0.480/Present5.272/IK0.543msです。SDKのgkcore.libへ既存benchmark objectをリンクし、import tableがgkcore.dllだけを参照することと、配置DLLのSDKとのbyte一致を確認しました。以前のcapture用DLLの19.550FPSとは表示条件・コード・再生履歴が異なるため、速度差を直接比較しません。300FPSは未達です。記録はshared-step-baseline/bounded-product-realtime600.logです。

固定1/60秒3000frameの全traceは、共通時刻の初期最終構成、merit-only、位置検算の置換後、最大2接触構成の4バイナリでbyte一致しました。segment最大34.330172度、p99 12.628354度、45/90度超0回という固定刻みの結果を保っています。停止した49deltaも最終構成で完走しましたが、旧max30との比較ではroot相対移動速度p99 1.2674→1.3527m/s、方向変化p99 108.667→109.297度へ増加しています。長いdeltaでの完全同等や滑らかさは主張しません。固定root差は0、同じ入力のframe48終端位置は一致しました。記録はbounded-fixed3000-parity.jsonとshared-step-diagnostics/bounded-replay49-comparison.jsonです。
