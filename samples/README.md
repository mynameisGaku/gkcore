# サンプルの起動

リポジトリ直下の`START.bat`をダブルクリックし、番号を1つ選ぶと、既にビルド済みのRelease版`gkcore_model_viewer.exe`が起動します。自動ビルドは行いません。素材や実行ファイルが見つからないときは不足している場所を表示します。

1. YUMEKAを基本色画像付きで静止表示します。
2. Downloads内の`Silly Dancing.fbx`と`Capoeira.fbx`を使い、YUMEKAに外部motionをblendします。motionが見つからない場合はファイルの場所を尋ねます。
3. Downloads内の`monkey.obj`を正面表示します。
4. ローカル検証用のCesium Manを静止表示します。
5. Sci-Fi TrooperにDownloads内の`Silly Dancing.fbx`と`Capoeira.fbx`をblendして表示します。
6. Clown Monsterに同じ2つの外部motionをblendして表示します。
7. YUMEKAの外部blendへ右腕IKを重ねます。目標はmotion中の手先から体幹の前方へ腕長の最大5%だけ補正し、届く範囲に合わせて補正量を縮めます。poleはmotionの曲げ面を可能な範囲で保ちます。オレンジのマーカーで位置を確認できます。衝突回避、大きな目標による肩変形、twist用補助ボーンへの回転配分を確認する項目ではありません。
8. Downloads内の`Skinning Test.fbx`をYUMEKAへ外部motionとして単独適用し、腰の移動に表示中心を追従させます。
9. Downloads内の`Swinging.fbx`をYUMEKAへ外部motionとして単独適用し、腰の移動に表示中心を追従させます。
10. `Skinning Test.fbx`と`Swinging.fbx`をYUMEKAへblendし、右腕IKと腰の移動に追従する表示中心を使います。
11. YUMEKAの外部blendと右腕IKに髪2鎖、尻尾1鎖、スカート10鎖の揺れを重ねます。
12. 「骨の小球と身体カプセル」。11と同じ13鎖に節球（髪・尻尾`0.003`、スカート`0.038` model unit）と脚側面を含む身体形状8個を加え、`--collision-debug`の形状表示を初期状態で有効にします。`C`キーで表示を切り替えます。
13. 従来の身体形状による接触に`--secondary-vertex-contact`を追加し、skin後の頂点を補正します。この項目は任意です。
14. 「スカート骨の接続補助」。12と同じ骨球・身体カプセル設定に、スカート10鎖を輪状につなぐ接触線分を加えます。`--secondary-links`設定で線分半径`0.038` model unitを指定し、`--collision-debug`の表示は初期状態で有効です。

5と6はローカル準備素材を使います。素材と起動設定がないcheckoutでは、その番号を選んだときに未準備の場所を表示します。motionが見つからない場合はファイルの場所を尋ねます。8〜14はDownloads内のmotion素材が必要で、選んだ項目のファイルがなければ場所を尋ねます。8〜10は`--follow-motion`を付け、motion中のHips移動分だけ表示中心を動かします。motion自体の腰位置は変えません。12は`--secondary-motion`、`--secondary-colliders`、`--collision-debug`を使い、接触形状を初期表示します。14はこれらに`--secondary-links`も加えます。髪・尻尾を8回、スカート鎖と接続線分を32回制約反復し、`C`キーでdebug表示を切り替えます。13は従来の頂点接触用設定に`--secondary-vertex-contact`を加えます。鎖設定は9数値の既存行を読み込み、必要な行には`|`の前へ10個目として反復数（1〜32）、11個目として節球半径（model unit）を指定できます。接続線分の設定は`半径 | firstBone secondBone`形式で、`--secondary-links`は身体形状の設定と併用します。`START.bat -CheckOnly`は11〜14を含むviewer起動引数と設定ファイルの存在を確認しますが、viewerやNative描画は起動しません。`build/local-assets/UnrealModels/viewer-models.json`がある場合は5と6のモデル、外部motion、材質設定と画像を確認し、定義ファイルがない場合は2項目を省略します。Silly DancingとCapoeiraは従来どおり必須確認です。Skinning TestとSwingingは任意確認で、未提供のmotion単独項目を省略し、blend + IKは両方がある場合だけ確認します。

Visual Studioで開発solutionを開く場合はリポジトリ直下の`OPEN_PROJECT.bat`を実行します。開いた`build/runtime-windows/gkcore.slnx`で`Release`・`x64`・`v142`を選びます。この環境ではVisual Studio 2026用の`.slnx`です。主な編集対象は`examples/model_viewer.cpp`です。

`START.bat -CheckOnly`は必要ファイルと各viewer起動引数だけを確認し、モデルviewerを起動しません。YUMEKAのbody-contact設定とCesium Manはローカル素材で、Cesium ManにはCC-BY-4.0 / Cesium LegalMark条件があります。SDKや配布物には含めず、ユーザー提供のモデル・motionも権利確認なしに再配布しないでください。
