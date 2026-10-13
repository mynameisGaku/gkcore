# モデルviewer

開発用`model_viewer`はOBJ・GLB・FBXモデルの位置、向き、アニメーション、材質を実画面で確認するサンプルです。ウィンドウは1280×720で、モデルの表示位置を引数で調整します。

```text
model_viewer <model-file> <scale> <centerX> <centerY> <centerZ> [mode args] [--materials <config-file>]
```

`scale`は正の有限値、`centerX/Y/Z`はモデル空間の有限値です。指定した中心が画面上の原点に来るように配置し、カメラは原点を向きます。パスに空白がある場合は、シェルの規則に従って引数を引用符で囲んでください。

## 表示mode

- `static`は読み込み時の姿勢を表示します。`front`は正面向きから、`rotate`は初期向きから左右回転を試せます。
- `animate`はモデル内の最初のclip、`blend`は2つ以上の埋め込みclipを再生・合成します。
- `external <animation-file>`は別のGLB/FBXから最初のclipを読み、名前と人型役割からモデルへ対応付けます。対応に失敗した場合は診断を表示します。
- `external-blend <animation-file-1> <animation-file-2>`は2つの外部motionを適用します。`B`キーで2本目の寄与を切り替えます。
- `external-ik <animation-file>`と`external-blend-ik <animation-file-1> <animation-file-2>`は、外部motionを更新した後の姿勢から右腕の2ボーンIK目標を毎frame作ります。右上腕・右前腕・右手に加えて、左上腕・腰、および脊椎・胸・上胸のいずれかの役割が必要です。目標は現在のmotionの手先を起点に、体幹の前方へ腕長の最大5%だけ補正し、届く範囲に合わせて補正量を縮めます。肘のpoleはmotionの曲げ面を可能な範囲で保ちます。前frameのIK姿勢は次の目標に引き継ぎません。2本目のmotionは`B`キーで切り替えられ、`M`キーでmotionを一時停止できます。
- `ik`と`chain`は右上腕・右前腕・右手の役割から、それぞれ2ボーンIKと連続chain IKを設定します。`--model-bones`の対応表、または骨名の自動推定を使います。腕と胴体の役割から肩より外側の目標を作り、オレンジ色のマーカーで示します。両modeは同じ目標を使います。必要な役割がない場合や、3本が直接の親子として連続しない場合は起動時に失敗します。

マーカーは確認用の位置表示です。大きな目標による肩の変形やtwist用補助ボーンへの回転配分、体・衣服との衝突回避、関節角度の制限は扱いません。回転時は指定した中心を画面中央に保ちます。

modeを省略すると`static`になります。`M`は再生を一時停止・再開し、`←`と`→`はモデルを回転します。`R`で回転を戻し、`Space`でBloomを切り替え、`Escape`で終了します。

viewerは`SetVSyncEnabled(false)`で画面更新の待機を外し、実際の経過時間でアニメーションを進めます。画面に表示するFPSは直近約0.5秒の値です。通常のゲームではVSyncは初期状態で有効で、変更は次の`BeginFrame()`から反映します。

実モデルでのFPSと再計測方法は[モデルviewerの処理時間](model-performance.md)に記載します。

## 基本色・alphaの確認

`--materials`を付けると、設定ファイルの各行で材質の基本色、基本色画像、alpha modeを指定できます。行の形式は次のとおりです。

```text
<material-index> <red> <green> <blue> <alpha> <alpha-mode> <cutoff> <image-path>
```

`alpha-mode`は`0`/`OPAQUE`、`1`/`MASK`、`2`/`BLEND`を受け付けます。色とalphaは0〜1の係数、`cutoff`は有限値を指定します。画像pathは行末まで読み取るため空白を含められます。空行と`#`で始まる行は無視します。材質indexは0から始まり、指定したindexに画像を設定します。

```text
# index R G B A mode cutoff image path
0 1.0 1.0 1.0 1.0 OPAQUE 0.5 assets/character base.png
1 1.0 1.0 1.0 0.6 MASK 0.4 assets/character alpha.png
```

この設定は公開`SetModelMaterial` APIを使う材質確認用です。viewerの設定ファイルはmipmap設定を持たず、`FModelMaterialSettings::generateMipmaps`の既定値trueを使います。元モデルのtoon shaderや任意の独自材質を再現するものではありません。

## アニメーション確認と検証素材

GLB/FBX内のclipは`animate`または`blend`で、別ファイルのmotionは`external`または`external-blend`で確認できます。`external-ik`と`external-blend-ik`では、motion更新後にIKを再計算し、前frameのIK姿勢が次の目標へ影響しないことを確認できます。capture検証では`GKCORE_TEST_ANIMATION_FRAMES`で描画回数を2〜120frameに指定し、既定の12frameでclip長に沿った姿勢とloopへの折り返しを確認します。保存する画像枚数は別の`GKCORE_TEST_CAPTURE_FRAMES`で1〜16枚を指定します。animated IK captureだけに使う`GKCORE_TEST_ANIMATION_START_SECONDS`は開始秒（既定0）、`GKCORE_TEST_ANIMATION_STEP_SECONDS`はframeごとの秒数（省略時は主motionのdurationをcapture全体へ割り当て）です。blend時は同じ秒数を両motionへ設定し、それぞれのdurationでloopします。実時間のloop境界を追うcaptureでは開始秒とstepを指定してください。異なる骨名の人型モデルでは、一般的な名前から役割を推定しますが、未知・曖昧な骨名がある場合は自動対応できないことがあります。APIで手動の役割設定と対応結果の照会ができます。詳細は[モデルアニメーション](model-animation.md)を参照してください。

launcherの8〜10ではDownloads内の`Skinning Test.fbx`と`Swinging.fbx`を使い、YUMEKAへの単独適用とblend + 右腕IKを選べます。両motionはanimation-onlyとして読み込め、どちらも66骨・morphなし・`mixamo.com` clip 1本です。durationはSkinning Testが2.25秒、Swingingが2.43333333秒です。通常のmodel inspectではどちらも「FBX contains no supported static triangles」と報告されるため、ここではmotion素材として扱います。

viewerは起動時にモデルと各外部motionの人型役割を推定して対応を設定し、IKで必要な役割は骨番号へ解決してsampleの更新処理で再利用します。メニュー8〜10では任意flag `--follow-motion`を付けます。viewerはmotion適用前に一意なHips役割の骨番号とその時点の位置を初期化し、以後のHips移動分だけ表示中心を更新します。motionの骨位置は変更しません。Hips役割が一意でない場合は開始できません。モデルを読み直すか、`--model-bones`などでHips役割の割当を変更した後は、再生前に追従状態を初期化し直す必要があります。このflagはメニュー8〜10だけに指定し、通常のmotion modeでは従来どおり指定した静止表示中心を使います。YUMEKAでは各motionの52人型役割のうち51本が対応し、左右の目は未対応でした。単独再生は各6姿勢、blend+IKは12姿勢を実GPUで描画しました。TrooperへのSkinning TestとClownへのSwingingは各52本が対応し、IKを重ねた12姿勢も確認しています。追従表示の48画像ではモデルが描画範囲に残りました。これは確認した組合せの結果であり、体や衣服の貫通、補助骨の変形品質まで保証するものではありません。

`START.bat`の17は、Skinning TestとSwingingを外部blend + IKで適用し、`--follow-motion`と接触設定なしの`--secondary-motion`で揺れものを再生します。身体shapeや接触linkは設定せず、身体接触を検証する入口ではありません。以前の身体shapeに骨間linkと距離補助を重ねる構成では停止が残っています。

`START.bat`の18は衣服の身体接触を確認する設定です。検査用motionの終端と先頭は姿勢がつながっていないため、`--ping-pong-motion`で各clipをその長さに合わせて往復再生します。元のmotionデータは変更しません。`M`でmotionを止めても揺れは自然に落ち着くまで更新し、`R`で時刻0と揺れの初期状態へ戻します。このoptionは外部motionのmodeで使え、通常のloop再生も引き続き選べます。

18は17と同じ外部blend + IKに、13鎖の骨球、8個の身体球・capsule、複数boneの実weightで位置を決める529個の接触球を加えます。[鎖設定](../samples/config/yumeka-skinning-contact-motion.txt)はスカートの最大角度180度・節球半径0.038・脚4形状のmask240を使い、髪・尻尾は全身体形状を判定します。[衣服の接触球](../samples/config/yumeka-skinning-contact-points.txt)は別の骨の影響も含め、球中心を各boneの変換位置とweightから求めます。接触球は全8身体形状を使い、頂点位置は書き換えません。

原点間link・距離補助・従来の179 offset球・頂点接触は18へ指定しません。デバッグ表示は`C`キーで切り替えられ、重み付き接触球も表示します。先頭のboneの位置は固定でも、そのlocal offsetは回転で動くため、子boneと合わせて移動寄与を数えます。元のbind記録1,314点から、スカート431点と既存の衣服101点を選び、同じbone/weightの組にだけまとめています。スカートでは移動寄与が50%以上の点を対象にし、固定骨のweightも保持します。移動寄与の小さい9点、三角形の内側、布面全体の非貫通は保証しません。

開始0秒・3.35秒の各120frameのRelease描画では、同じ全1,314点を全8身体形状で比較しました。スカートの平均侵入点数は前回14.95・15.15から1.69・1.80へ減りました。身体形状は近似capsuleであり、この点数は実際の身体mesh内の点数と同じとは限りません。この値は角度90度の試作設定での比較です。現行の180度構成では、スカート平均侵入は3.35・2.675点、最大深度は0.039625・0.028653 model unitでした。高い脚上げで衣服を折り返せるようにした180度の現行設定では、Releaseの1/60秒・3,000frameを完走しました。開始時・loop中をDebug/Release各120frame、大きな脚上げをRelease120frameでGPU変形と画像を確認しました。固定2区間の集計と画像はDebug/Releaseで一致しています。16の従来のダンス用設定と、17の身体接触なし設定はそのまま選べます。

317骨のYUMEKA FBXと、66骨のMixamo `Silly Dancing.fbx`・`Capoeira.fbx`を組み合わせた実動作確認を行っています。自動推定で役割が付いた骨はそれぞれ53本と52本、共通して対応した骨は51本でした。左右の太もも・すね・足首・つま先を含め、単独再生とブレンドで確認しました。未対応の役割は初期姿勢のまま残り、必要に応じてAPIで手動設定します。この結果は該当モデルとmotionでの確認であり、モデルの見た目や衣服の重なりを含む画質改善を保証しません。

検証に使ったCesium Manはローカル検証専用です。元データと付属するCC-BY-4.0 / Cesium LegalMarkの条件は[Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/edc7c9e67c639d230715049ee31f9a96a6babbbe/Models/CesiumMan)で確認できます。ユーザー提供のモデル・motionを含め、権利確認のない私有assetをSDKや他の配布物へ含めないでください。

--model-bones、--motion-bones、--blend-bonesで人型の対応表を指定できます。既知の骨名に依存しない適用方法は[人型ボーンの対応表](humanoid-bone-map.md)を参照してください。

外部モーションを適用した後、主・副それぞれの人型対応数を画面へ表示します。未対応役割がある表示はオレンジ色です。起動時の出力には不足した役割を列挙します。指などを含む全役割の件数なので、部分モーションでは不足があっても再生できます。名前だけでの骨対応と、人型役割による対応は別に数えます。

## 髪・スカート・尻尾の揺れ

`START.bat`の11は、YUMEKAの外部blend・右腕IKに、髪2鎖、スカート10鎖、尻尾1鎖の揺れを重ねます。12「骨の小球と身体カプセル」は同じ13鎖に身体形状8個を加え、髪・尻尾へ`0.003`、スカートへ`0.038` model unitの節球を設定します。脚側面を含む身体capsuleを使い、`--collision-debug`で身体形状を青、自由節を橙、固定rootを白、仮想末端を灰色で表示します。表示は起動時に有効で、`C`キーで切り替えられます。13は従来の身体接触用設定に`--secondary-vertex-contact`を加え、skin後の頂点を補正する任意項目です。設定ファイルは[接触なしの鎖](../samples/config/yumeka-secondary-motion.txt)、[従来の頂点接触用鎖](../samples/config/yumeka-secondary-contact-motion.txt)、[節球付き鎖](../samples/config/yumeka-bone-contact-motion.txt)、[身体形状](../samples/config/yumeka-bone-colliders.txt)です。鎖設定の既存9数値行と10数値行は引き続き使え、11個目は節球半径（model unit）です。髪・尻尾は8回、スカート鎖は32回制約反復します。

14「スカート骨の接続補助」は12と同じ揺れ・身体形状へ、スカート10鎖を輪状に結ぶ半径`0.038` model unitの接続線分を加えます。起動時のdebug表示も有効で、身体形状は青、自由節は橙、固定rootは白、仮想末端は灰色です。接続線分は`--secondary-links`と[設定ファイル](../samples/config/yumeka-secondary-links.txt)で指定し、身体形状を設定した起動に限り使用できます。

16「衣服位置に合わせた骨接触」は15と同じ揺れ、身体形状、骨間接続線分、距離設定に、衣服の主なskin寄与を持つ骨へ、bone-local offsetで179個の球を付けます。`--secondary-contact-links`には[設定ファイル](../samples/config/yumeka-secondary-contact-links.txt)を渡し、debug表示は起動時から有効です。線分はboneのpivotを保ちながら後続boneの向きで位置を合わせます。skin weightや衣服の幅、布面全体の身体非貫通は保証しません。回転軸を一意に決められない特異配置では更新に失敗する場合があります。

過去の0.008 model unit設定で行った実機検証では、DebugとReleaseで0秒・3.35秒の各120frameを描画し、合計480frameがすべて有効でした。対応する24組の画像がbyte単位で一致し、旧形状と節球付き形状のdebug表示も各120frame有効でした。詳細は[比較記録](../build/native-validation/bone-sphere-image-comparison.json)を参照してください。この480frame結果は現在の0.038設定を検証したものではありません。現行設定の0.038は、cloth幅の計測中央値0.0378を基にしています。別の120frame比較では平均侵入頂点数が44.708から16.517、最大侵入数が79から32、最大侵入深度が0.04838から0.03001へ下がりました。比較画像でも限定的な改善を確認しましたが、侵入は残っています。0.0768はsolverが到達可能な外側姿勢を見つけられず失敗したため採用していません。結果は[半径比較記録](../build/native-validation/bone-radius-trial-comparison.json)を参照してください。接続線分を含む実機比較では、DebugとReleaseで0秒・3.35秒を各120frame描画し、480frameすべてが有効で24組の画像がbyte単位で一致しました。輪状接続では平均侵入数が16.517から15.833、最大侵入深度が0.03001から0.02064へ下がりましたが、侵入は残ります。単一接続も完走しましたが開始時の最大深度0.03020が輪状設定より大きく、半径0.055はframe76で未収束のため採用していません。詳細は[接続線分比較記録](../build/native-validation/secondary-links-trial-comparison.json)と[Debug/Release画像比較](../build/native-validation/secondary-links-image-comparison.json)を参照してください。骨・カプセル接触を成立させてもskin weightと布の幅は補正されず、身体mesh全体の非貫通は保証しません。13の頂点接触は既定で無効でzero-morph FBXに限られ、布面全体、cloth meshや鎖どうしの衝突は扱いません。外部viewerでは`--secondary-motion <config-file>`と`--secondary-colliders <config-file>`を指定し、接続線分にはさらに`--secondary-links <config-file>`、13の頂点補正には`--secondary-vertex-contact`を付けます。更新はアニメーションとIKの後に行い、`R`で回転と揺れをリセットします。[モデルの揺れもの](model-secondary-motion.md)に設定形式とAPIを記載しています。
