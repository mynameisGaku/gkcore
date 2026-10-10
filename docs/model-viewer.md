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

317骨のYUMEKA FBXと、66骨のMixamo `Silly Dancing.fbx`・`Capoeira.fbx`を組み合わせた実動作確認を行っています。自動推定で役割が付いた骨はそれぞれ53本と52本、共通して対応した骨は51本でした。左右の太もも・すね・足首・つま先を含め、単独再生とブレンドで確認しました。未対応の役割は初期姿勢のまま残り、必要に応じてAPIで手動設定します。この結果は該当モデルとmotionでの確認であり、モデルの見た目や衣服の重なりを含む画質改善を保証しません。

検証に使ったCesium Manはローカル検証専用です。元データと付属するCC-BY-4.0 / Cesium LegalMarkの条件は[Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/edc7c9e67c639d230715049ee31f9a96a6babbbe/Models/CesiumMan)で確認できます。ユーザー提供のモデル・motionを含め、権利確認のない私有assetをSDKや他の配布物へ含めないでください。

--model-bones、--motion-bones、--blend-bonesで人型の対応表を指定できます。既知の骨名に依存しない適用方法は[人型ボーンの対応表](humanoid-bone-map.md)を参照してください。

外部モーションを適用した後、主・副それぞれの人型対応数を画面へ表示します。未対応役割がある表示はオレンジ色です。起動時の出力には不足した役割を列挙します。指などを含む全役割の件数なので、部分モーションでは不足があっても再生できます。名前だけでの骨対応と、人型役割による対応は別に数えます。

## 髪・スカート・尻尾の揺れ

`START.bat`の11は、YUMEKAの外部blend・右腕IKに、髪2鎖、スカート10鎖、尻尾1鎖の揺れを重ねます。12は同じ13鎖に身体の球・カプセル8個を追加します。12の設定では髪・尻尾を8回、スカート鎖を32回制約反復します。設定ファイルはそれぞれ[接触なしの鎖](../samples/config/yumeka-secondary-motion.txt)、[接触ありの鎖](../samples/config/yumeka-secondary-contact-motion.txt)、[身体形状](../samples/config/yumeka-secondary-colliders.txt)です。外部viewerでは`--secondary-motion <config-file>`と`--secondary-colliders <config-file>`を指定します。更新はアニメーションとIKの後に行い、`R`で回転と揺れをリセットします。身体接触を含むNative描画・見た目は未検証です。cloth meshと鎖どうしの制約は未対応です。[モデルの揺れもの](model-secondary-motion.md)に設定形式とAPIを記載しています。
