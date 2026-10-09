# プロジェクトの構成と入口

日常の操作はサンプルviewerの起動とVisual Studio solutionから始められます。`START.bat`はビルド済みRelease版を選択して起動し、`OPEN_PROJECT.bat`は通常の開発solutionを開きます。solutionがまだない場合は`PRE_SETUP.bat`で開発環境を準備してください。

## 入口

| 操作 | 入口と用途 |
| --- | --- |
| サンプルを見る | `START.bat`。YUMEKA、外部motion blend、`monkey.obj`、Cesium Manから選びます。 |
| ソースを編集する | `OPEN_PROJECT.bat`から`build/runtime-windows/gkcore.slnx`を開き、`Release`・`x64`・`v142`を選びます。主なviewer編集対象は`examples/model_viewer.cpp`です。 |
| 退役済み生成物を整理する | `CLEAN.bat -WhatIf`で対象を確認し、必要な場合だけ`CLEAN.bat`を実行します。自動削除はありません。 |

## ソースと開発用ファイル

| 場所 | 役割 |
| --- | --- |
| `include/` | ゲーム側へ公開するgkcore API。 |
| `src/` | Runtimeの実装。backend、描画、resource、animationなどの内部コードを置きます。 |
| `examples/` | `model_viewer.cpp`を含む、編集・ビルド可能なサンプルのソースです。 |
| `shaders/` | gkcoreが使うFSL/HLSL shaderのソースです。 |
| `tests/` | CPU/GPU契約、統合、package検査とfixture生成用コードです。 |
| `tools/` | setup、依存build、shader生成、検査、明示的な生成物整理用scriptです。 |
| `cmake/` | CMake packageとbuild補助設定です。 |
| `third_party/` | Runtime buildに組み込む固定依存ソースとlicense情報です。 |
| `.devtools/` | 開発用compilerや取得済み依存物です。ゲーム実行には使いません。 |
| `docs/` | API、使い方、検証範囲の説明です。 |
| `samples/` | 起動メニューなど、開発checkout内の利用補助scriptと説明です。 |

## build出力

`build/`以下は生成物です。通常の編集・起動で使うsolutionは`build/runtime-windows/gkcore.slnx`です。Release・x64・v142を選びます。Debug専用root、CPU test、shader生成、native検証用の個別出力は、開発・検証作業のためのもので、通常のviewer入口には含めません。

| 場所 | 役割 |
| --- | --- |
| `build/runtime-windows/` | 通常のRelease RuntimeとVisual Studio solution、viewer出力です。 |
| `build/runtime-windows-debug/`、`build/forge-debug/` | Debug検証用の別build rootです。 |
| `build/dev-windows/` | CPU testや開発検査用のbuild出力です。 |
| `build/native-validation/` | native GPU検証のlog、capture、比較結果を保存します。 |
| `build/local-assets/` | ローカル確認用素材と変換物です。SDKやpackageへ入れません。 |
| `build/real-model-captures/` | 実モデル表示のための検証captureです。 |
| `build/archive/` | 退役済みの検証用生成directoryを一時保管しています。現在のbuildや最新記録ではありません。 |

`CLEAN.bat`は`build/archive/`内の既知の退役済みdirectoryと`build/native-validation/`にある既知の調査probeだけを対象にします。pathがrepositoryのbuild領域内にあること、再解析pointや`.git`を含まないことを確認してから削除します。現在のbuild、`build/local-assets/`、capture、最新logは対象外です。整理前に`CLEAN.bat -WhatIf`で一覧を確認してください。

Runtime SDKの配布内容と開発用checkoutとの違いは[配布物と開発物](package-layout.md)を参照してください。サンプル別の起動方法は[サンプルの起動](../samples/README.md)にあります。
