# Runtime SDKの構成

SDKはゲームのビルドと実行に必要なファイルをまとめたものです。
ゲームからは公開headerと`gkcore::gkcore`のCMake targetを使います。

```text
include/                        公開APIと共通shader入力
lib/gkcore.lib                  Windows用のリンクライブラリ
lib/cmake/gkcore/               CMakeのpackage設定
bin/gkcore.dll                 Runtime本体
bin/                           Runtimeが使う依存DLLとGPU設定
bin/CompiledShaders/DIRECT3D12/ 標準描画・効果・GPU変形のshader
share/licenses/gkcore/          依存物のライセンスとnotice
```

ReleaseでSDKを出力する場合は、Runtimeのビルド後に次を実行します。

```bat
cmake --install build/runtime-windows --config Release --component Runtime --prefix sdk
```

ゲーム側のCMakeは`find_package(gkcore CONFIG REQUIRED)`でSDKを見つけ、`target_link_libraries(MyGame PRIVATE gkcore::gkcore)`でリンクします。
`CMAKE_PREFIX_PATH`にSDKの場所を指定してください。
[クイックスタート](quickstart.md)に、ゲームの実行ファイルへRuntimeを添える手順があります。

`bin/`内の依存DLL、`gpu.cfg`・`gpu.data`、標準shaderはRuntimeの起動に必要です。
ゲームの実行ファイルと同じディレクトリへ、`bin/`の内容を一式配置します。
DebugのSDKでは構成に対応するDLLも一緒に配置してください。

リポジトリの`include/`・`src/`・`shaders/`はRuntimeのソース、`examples/`はゲーム側の書き方を示すサンプルです。
`tools/`は依存物の準備とビルド、独自shaderのコンパイルに使います。
ビルド時に取得する`.devtools/`のcompilerや依存ソースを、ゲームの実行先へコピーする必要はありません。

依存物の権利表示とライセンス文書は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。
