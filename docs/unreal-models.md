# Unrealモデルのローカル表示

SciFITrooper_Man_03とclown_monsterはUnreal Engine用のuasset一式です。gkcoreはuassetを直接読み込まず、Unreal Editorで骨格つきFBXと画像を書き出したものを使います。今回の素材はローカル検証専用で、リポジトリやSDKへ含めません。

## 起動

開発checkoutのSTART.batで、5はSci-Fi Trooper、6はClown Monsterを選びます。Silly DancingとCapoeiraを両モデルへ適用し、ブレンドします。Bで2本目の寄与を切り替え、Mで一時停止、矢印キーで回転、Escapeで終了できます。

準備済みファイルはbuild/local-assets/UnrealModels/にあります。viewer-models.jsonはモデル・縮尺・中心・材質・元モーションの場所を保持します。素材がないcheckoutでは、メニュー選択時に未準備として表示します。

## 確認した内容

| モデル | 展開頂点 | ボーン | 人型役割 | 各Mixamo motionとの対応 |
| --- | ---: | ---: | ---: | ---: |
| Sci-Fi Trooper | 65,760 | 79 | 52 | 52 |
| Clown Monster | 228,720 | 169 | 52 | 52 |

Unreal Engine 5.8.3で書き出し、元のIdleまたはWalkと、外部Mixamoブレンドをそれぞれ6姿勢で描画しました。Debug/Releaseとも、左右の太もも・すね・足首・つま先8役割の対応を確認し、全位置・法線のGPU結果がCPU参照と許容0.0005以内で一致しました。位置最大差0、法線最大差約0.000000179、24画像は構成間で全画素一致しています。実画像でもモデルと脚の動きを確認しています。画像はbuild/real-model-captures/{Debug,Release}/unreal-models/、ログはbuild/native-validation/unreal-*-{debug,release}-*.logです。

基本色は材質slotに合わせたPNGを使用し、表示用画像は最大2048角へ縮小しています。元のnormal・ORM・独自Unreal材質を再現した結果ではありません。元モデルの骨名ではspine_01/02/03、neck_01、ball_l/r、ゼロ埋めされた指名を人型の役割へ対応付けます。追加のspine_04/05、neck_02、twist、metacarpalは同じ役割へ重複登録せず、階層中の補助骨として保持します。

## 書き出し

検証用コピーのUnrealプロジェクトでtools/export_unreal_validation_models.pyをPython commandletから実行します。GKCORE_UE_EXPORT_MANIFESTへJSONの場所を指定し、モデルasset、必要な基本色画像asset、元モーションassetと出力先を明示します。FBXへ残る作者PCの古い画像pathは、コピー側の読込情報を出力PNGへ更新してから書き出します。元のDownloadsフォルダーは変更しません。

今回のcommandletはAllowCommandletRenderingとRenderOffscreenを使用しました。NullRHIはskeletal meshの書き出しでMeshObjectのassertになり、NoShaderCompileは初期材質のassertになるため使用しません。不要なengine pluginとremote shader compileを無効にしたコピーで書き出しています。APIはUnreal Engineの[AssetExportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetExportTask?application_version=5.6)を使用します。

Mixamo以外の共通適用も、ClownMonsterWalkをSci-Fi Trooperへ、SciFITrooperIdleをClown Monsterへ移す両方向で確認しました。骨数79/169の違いがあっても各52人型役割が対応し、補助骨の名前対応を含め各68本を結び付けました。各6姿勢はDebug/Releaseとも脚の8役割とGPU/CPU一致を通過しています。
