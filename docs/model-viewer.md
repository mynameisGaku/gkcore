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
- `ik`と`chain`は右腕の骨名候補を探して、それぞれ2ボーンIKと連続chain IKを設定します。モデルに対応する右腕の骨がない場合は起動時に失敗します。

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

GLB/FBX内のclipは`animate`または`blend`で、別ファイルのmotionは`external`または`external-blend`で確認できます。異なる骨名の人型モデルでは、一般的な名前から役割を推定しますが、未知・曖昧な骨名がある場合は自動対応できないことがあります。APIで手動の役割設定と対応結果の照会ができます。詳細は[モデルアニメーション](model-animation.md)を参照してください。

317骨のYUMEKA FBXと、66骨のMixamo `Silly Dancing.fbx`・`Capoeira.fbx`を組み合わせた実動作確認を行っています。自動推定で役割が付いた骨はそれぞれ53本と52本、共通して対応した骨は51本でした。左右の太もも・すね・足首・つま先を含め、単独再生とブレンドで確認しました。未対応の役割は初期姿勢のまま残り、必要に応じてAPIで手動設定します。この結果は該当モデルとmotionでの確認であり、モデルの見た目や衣服の重なりを含む画質改善を保証しません。

検証に使ったCesium Manはローカル検証専用です。元データと付属するCC-BY-4.0 / Cesium LegalMarkの条件は[Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/edc7c9e67c639d230715049ee31f9a96a6babbbe/Models/CesiumMan)で確認できます。ユーザー提供のモデル・motionを含め、権利確認のない私有assetをSDKや他の配布物へ含めないでください。
