# 人型ボーンの対応表

自動判定できない骨名は、役割と骨名を対応表へ保存できます。
同じ対応表をモデルにも外部モーションにも使い、モーションの提供元やFBX・GLBの形式に依存せず役割を結び付けます。
モデルには骨格とskinが必要です。

## ファイルの形式

UTF-8のテキストで、一行に役割名と正確な骨名をtabで区切ります。
UTF-8 BOM、CRLF・LF、空行、#で始まるコメント行を使えます。
役割名はEHumanoidBoneの名前と同じで、NoneとCountは使いません。

```text
# 役割と骨名の間はtab
Hips	pelvis
Spine	spine_01
LeftUpperLeg	thigh_l
LeftLowerLeg	calf_l
LeftFoot	foot_l
LeftToes	ball_l
```

骨名の途中の空白や日本語はそのまま使えます。
骨名の前後にあるASCIIの空白は取り除きます。
役割名の前後には空白を入れないでください。

一つの骨と一つの役割は一回だけ指定します。
役割・骨名が不明、同名の骨が複数ある、骨を変更できない、UTF-8が不正、行の形式が不正、指定が一つもない、または64KiBを超える場合は読み込みに失敗します。

## モデルとモーションへ適用する

```cpp
const auto character = gk::LoadModel("character.fbx");
const auto motion = gk::LoadModelAnimation("walk.glb");
if (gk::SetModelHumanoidBoneMap(character, "character.bonemap") != 0 || gk::SetAnimationHumanoidBoneMap(motion, "motion.bonemap") != 0 || gk::ApplyModelAnimation(character, motion) != 0)
{
    // 読込・対応付けが失敗した理由を取得する。
    const char* error = gk::GetLastErrorMessage();
}
```

対応表は役割の全置換です。
記載しなかった骨はNoneへ戻ります。
失敗時は以前の役割を保持します。
モデルの設定はinstanceごとに独立します。

読み込んだ役割は保持するので、その後に対応表ファイルを削除しても利用できます。
すでに適用したclip・ブレンドの対応は変更しません。
役割を変えた後にApplyModelAnimationやSetModelAnimationBlendを呼び直すと、新しい設定で対応付けます。
未対応の骨はモデルの初期姿勢を保ちます。

## viewerで確認する

model_viewerへ次を追加できます。

- --model-bones path はモデルの対応表。
- --motion-bones path は主externalモーションの対応表。
- --blend-bones path は2本目のexternalモーションの対応表。

主モーションの指定はexternalまたはexternal-blend、2本目はexternal-blendで使います。
指定しなかった側は従来の自動判定を使います。
これらのoptionは引数の任意の位置に置けます。
既存の--materialsは末尾に置いてください。

```text
model_viewer character.fbx 1 0 0.9 0 external walk.glb --model-bones character.bonemap --motion-bones walk.bonemap
```

再生・ブレンドとIKは[モデルアニメーション](model-animation.md)を参照してください。
