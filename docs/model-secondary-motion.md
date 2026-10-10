# モデルの揺れもの

`ModelSecondaryMotion.h` は、animation後のbone鎖へ軽量な二次動作を加えます。
髪、尻尾、スカート、アクセサリーなど、親子が連続したbone列をモデルinstanceへ登録します。
物理計算はmodel空間で行い、骨位置の照会や描画予約では時間を進めません。

## 使い方

`SetModelSecondaryMotionChain`へ先頭から末端までのbone番号と設定を渡します。
同じ先頭boneを登録し直すと設定を置き換えます。
別の鎖は祖先・子孫関係がない枝に置きます。
登録したboneは書き換え可能で、鎖の末端には有限でゼロ以外の`endOffset`が必要です。
末端の先の仮想点も鎖の長さと曲げ制約に含めます。

身体との接触を使う場合は、鎖を登録した後に`SetModelSecondaryMotionColliders`で最大64個の球またはカプセルを設定します。
`FModelSecondaryMotionCollider`の端点と半径は取付boneのlocal空間で指定し、端点を同じにすると球になります。
節の余白はmodel空間の`margin`で加算され、既定値は`0.005`です。
設定はmodel instanceごとに保持されます。
`count=0`または`nullptr, 0`を渡すと接触形状だけを解除し、揺れものの鎖設定は残ります。
既存の`FModelSecondaryMotionSettings`のlayoutは変更しません。

身体形状は身体のboneへ取り付けます。
揺れもの鎖root自身とその子孫boneだけは指定できません。
別枝の脚boneなども使用できます。
端点・半径の変換を保つため、取付boneとその祖先は正のほぼ一様scaleである必要があります。

毎frameはanimation clockを進め、clipをsampleしてblendし、IKを設定・解決した後、描画前に`UpdateModelSecondaryMotion`を一度呼びます。
この明示更新がmodel空間の鎖を進め、確定姿勢をinstanceへ保存します。
身体形状もその時点のclip・blend・IK姿勢へ追従します。
`DrawModel`、`GetModelBonePosition(s)`、描画済みpacketの表示は保存済み姿勢を使い、再度積分しません。
更新後に同じframeで何度描画しても結果は変わりません。

```cpp
uint32_t hairBones[] = { rootBone, middleBone, tipBone };
gk::FModelSecondaryMotionSettings settings{};
settings.frequencyHz = 4.0f;
settings.dampingRatio = 0.8f;
settings.gravity = { 0.0f, -1.0f, 0.0f };
settings.maxAngleDegrees = 35.0f;
settings.endOffset = { 0.0f, 0.035f, 0.0f };
if (gk::SetModelSecondaryMotionChain(model, hairBones, 3, settings) != 0)
{
    // GetLastErrorMessage()で失敗理由を取得する。
}
const gk::FModelSecondaryMotionCollider bodyShapes[] = {
    { spineBone, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.11f, 0.0f }, 0.075f }
};
if (gk::SetModelSecondaryMotionColliders(model, bodyShapes, 1) != 0)
{
    // GetLastErrorMessage()で失敗理由を取得する。
}
// 各frameでanimationとIKを更新した後、DrawModelより前に呼ぶ。
if (gk::UpdateModelSecondaryMotion(model, deltaSeconds) != 0)
{
    // GetLastErrorMessage()で失敗理由を取得する。
}
```

身体形状だけを解除して鎖を続ける場合は、`gk::SetModelSecondaryMotionColliders(model, nullptr, 0)`を呼びます。

animationを停止しても、更新を続ければ速度は減衰し、重力などを含む平衡姿勢へ落ち着きます。
重力がある設定では、平衡姿勢は厳密な元animation姿勢から少しずれることがあります。
`ResetModelSecondaryMotion`は現在のanimation・blend・IK姿勢へ位置と速度を即時に戻します。
`ClearModelSecondaryMotion`は設定とsimulationを解放します。
大きなroot移動、方向の瞬時反転で中間の鎖がゼロ長になる場合、または0.25秒以上の更新間隔は、以前の速度を持ち越さず現在の目標へ再配置します。

接触判定は明示更新時の各節位置と隣り合う節間線分を調べます。
rootは固定し、rootが形状内にある場合はその形状との最初の線分だけを検査から外します。
自由端の節1は引き続き検査します。
制約を同時に満たす姿勢が作れない場合は更新を失敗させ、最後に確定した姿勢と揺れ状態を保ちます。
resetは元のanimation姿勢へ戻し、次の更新で現在の接触形状を解きます。

## 設定値と制約

`FModelSecondaryMotionSettings`の単位は秒、度、model空間です。
gravityとwindは加速度です。
設定値は有限で、`frequencyHz`は0より大きく20以下、`dampingRatio`は0〜2、`maxAngleDegrees`は0〜180、`teleportDistance`は0より大きい値、`constraintIterations`は1〜32を受け付けます。
`endOffset`は末端boneから仮想点までの有限でゼロ以外のlocal位置です。
既定値は振動数3Hz、減衰比0.7、下向き重力9.81、曲げ角60度、長さ0.05の仮想末端、teleport距離0.5、制約反復8回です。

鎖のbone数は1〜1024です。
各boneは直前boneの子でなければならず、bone長は0にできません。
重なり合う鎖は登録できません。
scaleは正でほぼ一様なものに限り、各祖先の最大・最小scale差が最大scaleに対して`128 * FLT_EPSILON`以下である必要があります。
これは実モデルの丸め誤差を許容する条件です。
約5%の軸差、負scale、0 scaleは拒否します。
実データ検査ではYUMEKAのskirt骨に最大約`1.0371e-5`の相対軸差があり許容されました。
各軸差が約5%のfixtureは拒否され、失敗時に出力姿勢を保つことも確認しています。
YUMEKA実データと同じscale値を使った3骨fixtureでは、姿勢点誤差が0.0001未満でした。

## ViewerのYUMEKA設定

`model_viewer`と`real_model_capture_tests`は`--secondary-motion <config-file>`を受け付けます。
設定行は振動数、減衰比、最大曲げ角、末端offsetのXYZ、重力XYZの9数値、区切り`|`、bone名の順です。
`|`の前には任意で制約反復数を10個目の数値として指定できます。
省略時は8回、指定時は1〜32回です。
9数値だけの既存設定は従来どおり読み込めます。
空行と`#`コメントを無視し、bone名はモデルから検索します。
現在のYUMEKA設定は髪2本、尻尾1本、スカート10本の計13鎖です。
設定例は検証素材用で、Runtimeへ同梱するassetではありません。

身体接触には`--secondary-colliders <config-file>`を併用します。
各設定行は端点6成分、半径、`|`、取付bone名の順です。
bone名はモデルから検索され、節の余白`0.005` model unitが半径へ加わります。
現在のYUMEKA接触鎖設定は、髪・尻尾の3鎖を8回、スカート10鎖を32回反復する設定です。
スカートの角度制約と接触制約が同時に成立しにくい姿勢でも、反復数を増やして解ける場合に備えています。
身体半径・余白・角度設定は変えていません。
8個の身体形状と併用します。
なお、身体接触を使わない既存presetはすべて既定の8回です。

```powershell
model_viewer.exe <model-file> <scale> <centerX> <centerY> <centerZ> external-blend-ik <motion-a> <motion-b> --follow-motion --secondary-motion yumeka-secondary-contact-motion.txt --secondary-colliders yumeka-secondary-colliders.txt
```

自分のモデル用に`character-secondary-motion.txt`を作り、bone名を実際の名前へ置き換えます。
次は1本の鎖の設定行です。

```text
4 0.8 35 0 0.035 0 0 -1 0 | HairRoot HairMiddle HairTip
```

必要な鎖だけ反復数を増やす例です。
10個目の値`32`が制約反復数で、`|`より前に置きます。

```text
4 0.8 35 0 0.035 0 0 -1 0 32 | HairRoot HairMiddle HairTip
```

```powershell
model_viewer.exe <model-file> <scale> <centerX> <centerY> <centerZ> external-blend <motion-a> <motion-b> --secondary-motion character-secondary-motion.txt
```

## 対応範囲と未対応

この機能はskeletal GLB / FBX等の書き換え可能なbone姿勢へ適用します。
OBJ連番など非skeletal animationには適用できません。
髪・スカート・アクセサリー等の二次動作自体はユーザー指定の必須範囲です。
一方、spring式、重力・風、許容値、model空間でのsimulationは開発側が選んだ成立手段です。
実装はばね、減衰、重力、風、bone長、基準方向からの最大曲げ角を扱います。
比較画像では13鎖の変化を確認し、明確な発散や裂けは見つかっていません。
接近時の揺れと角度制約を含む見た目の品質判定は継続中です。

身体の球・カプセルとの離散接触を実装しています。
更新間の高速なすり抜けを保証する連続衝突判定、cloth mesh、cloth self-collision、隣接するスカート鎖どうしの制約、world transform（SRT）の慣性、SDK内部の物理専用固定更新ループは未対応です。
この機能は一般的なrigid-body物理engineやcloth simulationを提供しません。
骨の節と線分への接触処理であり、身体のmesh面の非貫通は保証しません。
実モデルでの髪・スカートの揺れと復帰品質、代表設定での300FPS達成は完了条件に残っています。
