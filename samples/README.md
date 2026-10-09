# サンプルの起動

リポジトリ直下の`START.bat`をダブルクリックし、番号を1つ選ぶと、既にビルド済みのRelease版`gkcore_model_viewer.exe`が起動します。自動ビルドは行いません。素材や実行ファイルが見つからないときは不足している場所を表示します。

1. YUMEKAを基本色画像付きで静止表示します。
2. Downloads内の`Silly Dancing.fbx`と`Capoeira.fbx`を使い、YUMEKAに外部motionをblendします。motionが見つからない場合はファイルの場所を尋ねます。
3. Downloads内の`monkey.obj`を正面表示します。
4. ローカル検証用のCesium Manを静止表示します。
5. Sci-Fi TrooperにDownloads内の`Silly Dancing.fbx`と`Capoeira.fbx`をblendして表示します。
6. Clown Monsterに同じ2つの外部motionをblendして表示します。

5と6はローカル準備素材を使います。素材と起動設定がないcheckoutでは、その番号を選んだときに未準備の場所を表示します。motionが見つからない場合はファイルの場所を尋ねます。`START.bat -CheckOnly`は`build/local-assets/UnrealModels/viewer-models.json`がある場合に2項目のモデル、外部motion、材質設定と画像を確認し、定義ファイルがない場合は2項目を省略します。

Visual Studioで開発solutionを開く場合はリポジトリ直下の`OPEN_PROJECT.bat`を実行します。開いた`build/runtime-windows/gkcore.slnx`で`Release`・`x64`・`v142`を選びます。この環境ではVisual Studio 2026用の`.slnx`です。主な編集対象は`examples/model_viewer.cpp`です。

`START.bat -CheckOnly`は必要ファイルと各viewer起動引数だけを確認し、モデルviewerを起動しません。YUMEKAとCesium Manはローカル素材で、Cesium ManにはCC-BY-4.0 / Cesium LegalMark条件があります。SDKや配布物には含めず、ユーザー提供のモデル・motionも権利確認なしに再配布しないでください。
