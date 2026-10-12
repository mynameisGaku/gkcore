# モデルの揺れもの

`ModelSecondaryMotion.h` は、animation後のbone鎖へ軽量な二次動作を加えます。髪、尻尾、スカート、アクセサリーなど、親子が連続したbone列をモデルinstanceへ登録します。物理計算はmodel空間で行い、骨位置の照会や描画予約では時間を進めません。

## 使い方

`SetModelSecondaryMotionChain`へ先頭から末端までのbone番号と設定を渡します。同じ先頭boneを登録し直すと設定を置き換えます。別の鎖は祖先・子孫関係がない枝に置きます。登録したboneは書き換え可能で、鎖の末端には有限でゼロ以外の`endOffset`が必要です。末端の先の仮想点も鎖の長さと曲げ制約に含めます。

身体との接触を使う場合は、鎖を登録した後に`SetModelSecondaryMotionColliders`で最大64個の球またはカプセルを設定します。`FModelSecondaryMotionCollider`の端点と半径は取付boneのlocal空間で指定し、端点を同じにすると球になります。節の余白はmodel空間の`margin`で加算され、既定値は`0.005`です。設定はmodel instanceごとに保持されます。`count=0`または`nullptr, 0`を渡すと接触形状だけを解除し、揺れものの鎖設定は残ります。既存の`FModelSecondaryMotionSettings`のlayoutは変更しません。

身体形状は身体のboneへ取り付けます。揺れもの鎖root自身とその子孫boneだけは指定できません。別枝の脚boneなども使用できます。端点・半径の変換を保つため、取付boneとその祖先は正のほぼ一様scaleである必要があります。

鎖ごとの`SetModelSecondaryMotionChainCollisionRadius(model, firstBone, radius)`は、各boneと仮想末端を覆う球の半径をmodel空間で設定します。既定は`0`で、従来の細い節・線分接触を保ちます。指定した半径は身体形状の半径に加えて判定し、鎖を同じ先頭boneで登録し直したときと`ResetModelSecondaryMotion`の後も保持されます。setterはbone姿勢をその場で動かさず、次の`UpdateModelSecondaryMotion`から半径を使います。無効な先頭boneや負値・非有限値は失敗し、前の設定を保ちます。`ClearModelSecondaryMotion`で鎖とともに解放されます。

複数の鎖の自由boneを線分で結ぶには、`SetModelSecondaryMotionLinks`へ`FModelSecondaryMotionLink`配列を渡します。各端点は別々に登録した鎖の自由boneで、固定rootや仮想末端は指定できません。最大1024本、半径はmodel空間の有限な0以上、反復数は1〜32です。端点の順序を反転した重複も拒否します。既定の反復数は32で、`nullptr, 0`は接続線分だけを解除し、鎖や身体形状は保持します。`ResetModelSecondaryMotion`と鎖の再登録後も接続線分と反復数を保持し、次の明示更新で接触を解きます。

複数鎖の自由bone間の距離を保つには、`SetModelSecondaryMotionDistanceConstraints`へ`FModelSecondaryMotionDistanceConstraint`配列を渡します。各端点は別々に登録した鎖の自由boneで、最大1024組です。固定root・同じ鎖のbone・順序を反転した重複ペアは指定できません。`restDistance=0`は登録時の基準姿勢を測って距離として固定します。この基準姿勢は揺れを加える前のclip・blend・IK反映後の位置です。animationで毎frame基準距離を測り直す機能ではありません。正の`restDistance`はmodel空間の距離を直接指定します。`compliance=0`は距離を保ち、正の値は伸び縮みを許します。値が大きいほど柔らかく、両端は同じ動きやすさで補正します。

距離は、柔らかさを時間刻みに合わせて扱うXPBDという位置補正で解きます。各Updateのframe末尾で連成処理を呼び、その制約反復内では補正の累計`lambda`を保持します。lambdaはUpdateごとに0へ戻します。時間刻み`h`は、0より大きく0.25秒未満のdeltaではそのdelta、deltaが0または0.25秒以上のhitchでは`1/120`秒です。検査する誤差は`abs(実距離 - 基準距離 - (compliance / h²) * lambda)`で、柔らかい設定では実距離そのものの伸びを拒否しません。候補は基準距離の0.05%、描画FKは0.1%を許容します。各鎖のsubstepと距離制約を同じ刻みで一緒に解く構成ではなく、更新頻度による動きの差は残ります。極端に短い距離や大きなfloat座標では必要な相対精度を保てず、更新が失敗する場合があります。

距離制約は身体colliderがなくても使えます。骨の接触や最大角度と同時に満たせない場合、更新は失敗し、全鎖の直前の確定状態を保ちます。`ResetModelSecondaryMotion`は基準姿勢へ戻し、設定した距離制約は次のUpdateから再び適用されます。距離制約と接続線分は反復数を共有し、どちらかのsetterを最後に呼んだときの反復数が使われます。`nullptr, 0`は距離制約だけを解除します。

複数鎖の接触制約を同時に解けない場合、`UpdateModelSecondaryMotion`は`-1`を返し、直前に確定した全鎖の物理状態と姿勢を保ちます。`GetLastErrorMessage()`で失敗理由を確認してください。接続線分は骨長や角度制約と両立しない設定では成立しません。

接続候補では数値上の接触許容幅の半分だけを使い、残りを描画姿勢へ戻す際の丸め誤差に残します。描画時の骨原点から接続線分を再検査し、最終許容幅を超える侵入は更新失敗として扱います。この余白は布の厚みやmesh面の接触を表すものではありません。

接続した鎖は初回の反復で長さ・角度・接触を検査します。後続の反復では、投影で位置が変わった鎖と、接触がまだ成立していない線分の両端鎖を補正します。位置が変わらず、接続も成立した鎖への再計算を省きます。同じ更新内での鎖復元は作業用の5配列の容量を共有し、毎回要素数を空にして現在の値を入れ直します。位置・速度・目標方向や接触判定の結果を別の鎖や更新へ持ち越すことはありません。

反復の前半は通常の接触方向へ押し出します。後半まで接触が残るときは、親から子へ向かう骨方向と最大曲げ角を使い、骨の長さを戻す処理で消えにくい回転方向へ補正します。中心で交差してこの方向を定められない場合は、カプセル全体の外側へ向かう両側の移動を比較します。これらは候補の位置補正で、最終の骨長・曲げ角・接触・描画FKの検査を省きません。反復数や接触許容幅は増やしません。

`GetModelSecondaryMotionDebugShapes`は公開値型`gk::FModelSecondaryMotionDebugShape`で、現在の姿勢に対応する接触形状をmodel空間で返します。`nullptr, 0`は必要な個数を返し、配列が足りない場合や無効なinstanceでは失敗して出力を保ちます。描画用の色分けでは、身体形状を青、自由に動く節を橙色、固定rootを白、仮想末端を灰色で表示します。身体球の半径には`SetModelSecondaryMotionColliders`のmarginが含まれ、鎖の球半径は上記のsetter値です。距離制約は半径0の接続線として、現在のbone原点間を表示します。照会はanimationと物理状態を進めません。

身体形状の中に固定rootがある場合、その形状との最初の節間線分は接触判定から除外します。次の自由節の位置は引き続き検査します。鎖の球による接触は骨の位置を身体から離すもので、skin weightによる頂点位置や布の面全体は補正しません。複数boneのblendとmesh幅があるため、骨が離れていてもmeshが身体へ埋まることがあります。

毎frameはanimation clockを進め、clipをsampleしてblendし、IKを設定・解決した後、描画前に`UpdateModelSecondaryMotion`を一度呼びます。この明示更新がmodel空間の鎖を進め、確定姿勢をinstanceへ保存します。身体形状もその時点のclip・blend・IK姿勢へ追従します。`DrawModel`、`GetModelBonePosition(s)`、描画済みpacketの表示は保存済み姿勢を使い、再度積分しません。更新後に同じframeで何度描画しても結果は変わりません。

成功した`UpdateModelSecondaryMotion`は、clip・blend・IKを反映し、揺れを重ねる前の基準姿勢をmodel instanceごとに保存します。再生条件が同じ間は、`DrawModel`と骨位置のqueryがこの姿勢を再利用するため、clipを再sampleしません。頂点接触の位置番号も設定時に選別し、鎖の再登録で更新します。UpdateとResetはこの配列を保持し、Drawで全weightを選別し直しません。再生時刻・speed・loop・blend・clip登録・IK・model bone roleの変更でcacheを無効にし、失敗したsetterや揺れ更新は直前の有効な状態を保ちます。描画snapshotは毎回独立して作られ、queryとdrawはsimulationやanimation clockを進めません。

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

変形後のskin頂点へ身体形状との接触を追加するには、`SetModelSecondaryMotionVertexContactsEnabled(model, true)`を明示します。既定は無効で、揺れものの鎖と身体形状の両方を設定してから有効にしてください。現在はzero-morph FBXだけが対応します。各frameの明示更新で骨・IK・simulationの姿勢は変更せず、描画直前にskin後の頂点だけを球・カプセルの外へ投影します。Draw packetは補正頂点と再構成normalを独立して保持し、CPU描画とGPU描画fallbackで同じ結果を使います。

この頂点接触は三角形内部の面や布全体を押し出すものではなく、自己衝突や更新間の連続衝突にも対応しません。morphを含む未対応形状や、最大64回の反復後も接触を解けない場合は描画更新を診断付きで失敗させます。

animationを停止しても、更新を続ければ速度は減衰し、重力などを含む平衡姿勢へ落ち着きます。重力がある設定では、平衡姿勢は厳密な元animation姿勢から少しずれることがあります。`ResetModelSecondaryMotion`は現在のanimation・blend・IK姿勢へ位置と速度を即時に戻します。`ClearModelSecondaryMotion`は設定とsimulationを解放します。大きなroot移動、方向の瞬時反転で中間の鎖がゼロ長になる場合、または0.25秒以上の更新間隔は、以前の速度を持ち越さず現在の目標へ再配置します。

接触判定は明示更新時の各節位置と隣り合う節間線分を調べます。rootは固定し、rootが形状内にある場合はその形状との最初の線分だけを検査から外します。自由端の節1は引き続き検査します。制約を同時に満たす姿勢が作れない場合は更新を失敗させ、最後に確定した姿勢と揺れ状態を保ちます。resetは元のanimation姿勢へ戻し、次の更新で現在の接触形状を解きます。

## 設定値と制約

`FModelSecondaryMotionSettings`の単位は秒、度、model空間です。gravityとwindは加速度です。設定値は有限で、`frequencyHz`は0より大きく20以下、`dampingRatio`は0〜2、`maxAngleDegrees`は0〜180、`teleportDistance`は0より大きい値、`constraintIterations`は1〜32を受け付けます。`endOffset`は末端boneから仮想点までの有限でゼロ以外のlocal位置です。既定値は振動数3Hz、減衰比0.7、下向き重力9.81、曲げ角60度、長さ0.05の仮想末端、teleport距離0.5、制約反復8回です。

鎖のbone数は1〜1024です。各boneは直前boneの子でなければならず、bone長は0にできません。重なり合う鎖は登録できません。scaleは正でほぼ一様なものに限り、各祖先の最大・最小scale差が最大scaleに対して`128 * FLT_EPSILON`以下である必要があります。これは実モデルの丸め誤差を許容する条件です。約5%の軸差、負scale、0 scaleは拒否します。実データ検査ではYUMEKAのskirt骨に最大約`1.0371e-5`の相対軸差があり許容されました。各軸差が約5%のfixtureは拒否され、失敗時に出力姿勢を保つことも確認しています。YUMEKA実データと同じscale値を使った3骨fixtureでは、姿勢点誤差が0.0001未満でした。

鎖に登録したboneでも、頂点を動かすskin weightが設定されていなければ、そのboneを動かしても対応するmesh部分は動きません。頂点は複数boneの動きを混ぜて変形し、布の表面はboneの線から横へ広がるため、鎖の節や線分を身体に接触させても布全体が身体から離れるとは限りません。この機能は衣服や髪が身体へ埋まらないことを保証するものではありません。

## ViewerのYUMEKA設定

`model_viewer`は揺れ設定があるとき、`C`キーで接触形状の表示を切り替えます。`--collision-debug`を付けると起動時から表示します。このflagには`--secondary-motion`の指定が必要です。

`model_viewer`と`real_model_capture_tests`は`--secondary-motion <config-file>`を受け付けます。設定行は振動数、減衰比、最大曲げ角、末端offsetのXYZ、重力XYZの9数値に、任意の反復数と節球半径を続け、区切り`|`とbone名を置きます。反復数は10個目で1〜32、半径は11個目で有限な0以上のmodel空間値です。半径は各鎖boneと仮想末端の小球半径を表します。半径を指定する場合は反復数も記述します。両方を省略した9数値の既存設定、半径を省略した10数値設定は引き続き読み込めます。空行と`#`コメントを無視し、bone名はモデルから検索します。身体接触を使わない[YUMEKA設定](../samples/config/yumeka-secondary-motion.txt)は13鎖すべて節半径なしです。設定例は検証素材用で、Runtimeへ同梱するassetではありません。

身体接触には`--secondary-colliders <config-file>`を併用します。各設定行は端点6成分、半径、`|`、取付bone名の順です。bone名はモデルから検索され、節の余白`0.005` model unitが半径へ加わります。[従来の頂点接触用鎖設定](../samples/config/yumeka-secondary-contact-motion.txt)は髪・尻尾の3鎖を8回、スカート10鎖を32回反復し、節球は使いません。[骨の小球と身体カプセル用設定](../samples/config/yumeka-bone-contact-motion.txt)では髪・尻尾の節半径が`0.003`、スカートが`0.038` model unitです。骨の小球設定は[脚へ合わせて拡げた8個の身体形状](../samples/config/yumeka-bone-colliders.txt)、頂点接触の設定は[従来の身体形状](../samples/config/yumeka-secondary-colliders.txt)を使います。スカート鎖はどちらも32回反復します。

鎖どうしの接続線分を設定するviewerは`--secondary-links <config-file>`も指定します。このoptionには`--secondary-colliders`が必要です。各行は`半径 | firstBone secondBone`で、半径はmodel空間です。設定ファイルは[スカート接続線分](../samples/config/yumeka-secondary-links.txt)を参照してください。接続線分の端点は異なる鎖の自由boneに限り、同じペアの重複や固定root・仮想末端は使えません。

骨間距離を設定するviewerは`--secondary-distances <config-file>`を指定します。このoptionには`--secondary-motion`が必要ですが、身体colliderは不要です。各行は`restDistance compliance | firstBone secondBone`です。YUMEKAの[10本の距離設定](../samples/config/yumeka-secondary-distances.txt)は登録時の距離を使い、complianceは`0.001`です。START15は試験設定です。0秒・3.35秒から各120frameをDebug/Releaseで再生し、計480frameのGPU変形検査と保存24組の画像一致を確認しました。loop区間の平均侵入点数は15.833→15.483、最大深度は0.020639→0.023409で、布面の品質改善とは判定していません。実時間の計測は身体接触と距離制約が未収束になり、300warmup後340frameで停止しました。その後、更新中のファイル書き込みを省いたdelta記録で611番目の停止を再現し、骨長復元に沿う接触補正を加えました。同じ611 deltaはDebug/Releaseで完走し、通常の実時間3000frame/warmup300も完走しています。これは布面の非貫通や全設定の収束を保証するものではありません。

```powershell
model_viewer.exe <model-file> <scale> <centerX> <centerY> <centerZ> external-blend-ik <motion-a> <motion-b> --follow-motion --secondary-motion yumeka-bone-contact-motion.txt --secondary-colliders yumeka-bone-colliders.txt --collision-debug
```

自分のモデル用に`character-secondary-motion.txt`を作り、bone名を実際の名前へ置き換えます。次は1本の鎖の設定行です。

```text
4 0.8 35 0 0.035 0 0 -1 0 | HairRoot HairMiddle HairTip
```

必要な鎖だけ反復数を増やす例です。10個目の値`32`が制約反復数で、`|`より前に置きます。

```text
4 0.8 35 0 0.035 0 0 -1 0 32 | HairRoot HairMiddle HairTip
```

```powershell
model_viewer.exe <model-file> <scale> <centerX> <centerY> <centerZ> external-blend <motion-a> <motion-b> --secondary-motion character-secondary-motion.txt
```

YUMEKAのSTART14では、節球・脚の身体形状に加え、スカート10本を輪状に結ぶ接続線分を設定します。線分半径は`0.038` model unit、制約反復数は32です。実機のDebugとReleaseで0秒・3.35秒を各120frameずつ描画し、480frameすべてが有効で、24組の画像がbyte単位で一致しました。接続線分ありでは平均侵入頂点数が16.517から15.833、最大侵入深度が0.03001から0.02064へ下がりましたが、侵入は残ります。一本だけの接続は480frame完走したものの、開始時の最大侵入深度が0.03020で輪状設定より大きく、採用していません。半径`0.055`はframe 76で制約を解けず失敗したため使いません。これらの結果は[接続線分比較記録](../build/native-validation/secondary-links-trial-comparison.json)と[Debug/Release画像比較](../build/native-validation/secondary-links-image-comparison.json)に記録しています。

## 対応範囲と未対応

この機能は、書き換え可能なbone姿勢を持つGLB / FBXへ適用します。OBJ連番など、boneを持たないanimationには適用できません。ばね、減衰、重力、風、bone長、基準方向からの最大曲げ角と、身体球・カプセルとの接触を扱います。

YUMEKAの骨球設定では、計測したスカート幅の中央値`0.0378` model unitに合わせ、節球半径を`0.038`にしています。髪・尻尾は`0.003`です。元FBXとskin weightは変更しません。骨の接触を解いても、頂点は腰や胴体の骨の動きも混ぜて変形するため、布の面が身体へ入る場合があります。

頂点接触は、対応するzero-morph FBXの個々の変形点を補正する任意の機能です。骨球の設定では無効です。どちらも布の面全体の接触を扱わないため、衣服全体が身体へ埋まらないことは保証しません。実モデルの比較結果は[TDD検証ログ](TDD_LOG.md)に記録しています。

身体の球・カプセルと、登録した鎖の自由bone間の離散接触を実装しています。更新間の高速なすり抜けを保証する連続衝突判定、cloth mesh、cloth self-collision、world transform（SRT）の慣性、SDK内部の物理専用固定更新ループは未対応です。この機能は一般的なrigid-body物理engineやcloth simulationを提供しません。骨の節と線分への接触処理であり、身体のmesh面の非貫通は保証しません。実モデルでの髪・スカートの揺れと復帰品質、代表設定での300FPS達成は完了条件に残っています。現在の測定値は[モデルviewerの処理時間](model-performance.md)を参照してください。
