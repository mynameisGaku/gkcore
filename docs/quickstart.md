# はじめてのgkcore

`<gkcore.h>`と`gk::`の関数で、ウィンドウ・入力・2D・3D描画を扱います。
The ForgeのAPIをゲーム側から使う必要はありません。

## サンプルを起動する

Windows x64のC++開発環境、v142 14.29 toolset、Windows SDK 10.0.22621.0、CMake、Python 3.9以降を用意して、リポジトリ直下で次を実行します。
現在の固定版の依存物は、このtoolsetとSDKでビルドします。

```bat
PRE_SETUP.bat
build\runtime-windows\Release\gkcore_mixed_scene.exe
```

セットアップは依存物を`.devtools/`へ取得し、Runtimeとサンプルを`build/runtime-windows/`へ出力します。
Visual Studioでは同じフォルダーの`gkcore.slnx`または`gkcore.sln`を開き、Release・x64を選びます。
最初の編集対象は[examples/mixed_scene.cpp](../examples/mixed_scene.cpp)です。

## 最小ループ

```cpp
#include <gkcore.h>
/**
 * ウィンドウを作り、終了要求まで矩形を描く。
 */
int main()
{
    if (gk::SetWindowSize(1280, 720) != 0 || gk::Init() != 0)
    {
        return 1;
    }
    while (gk::ProcessEvents() && !gk::IsKeyDown(gk::Key::Escape))
    {
        if (gk::BeginFrame() != 0 || gk::DrawRect(32.0f, 32.0f, 208.0f, 112.0f, gk::ColorRGB(70, 150, 240), true) != 0 || gk::Present() != 0)
        {
            break;
        }
    }
    gk::Shutdown();
    return 0;
}
```

描画は`BeginFrame()`と`Present()`の間に登録します。
APIが失敗した直後に`GetLastErrorMessage()`を呼ぶと、原因を取得できます。
短いキー入力の切り替えには`WasKeyPressed()`、押し続ける操作には`IsKeyDown()`を使います。

`DrawLayer::Scene`にゲームの2D・3Dを、`DrawLayer::UI`にHUDを描きます。
SceneにBloom・トーンマッピング・FXAAなどを適用してからUIを合成します。
効果の設定は次の`BeginFrame()`で反映します。

画像は`LoadImage()`、モデルは`LoadModel()`で読み込みます。
モデルの位置・回転・scaleとカメラを設定し、`DrawModel()`で描きます。
GLB・FBXのアニメーションと連番OBJ、外部モーション、ブレンド、ヒューマノイド対応、IKも利用できます。

## 自分のゲームをビルドする

SDKを出力します。

```bat
cmake --install build/runtime-windows --config Release --component Runtime --prefix sdk
```

ゲーム側に`main.cpp`と次の`CMakeLists.txt`を置きます。

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyGame LANGUAGES CXX)
find_package(gkcore CONFIG REQUIRED)
add_executable(MyGame main.cpp)
target_compile_features(MyGame PRIVATE cxx_std_17)
target_link_libraries(MyGame PRIVATE gkcore::gkcore)
# RuntimeのDLL、標準shader、GPU設定をゲームの実行ファイルへ添える。
get_target_property(gkcore_runtime gkcore::gkcore IMPORTED_LOCATION_RELEASE)
get_filename_component(gkcore_bin "${gkcore_runtime}" DIRECTORY)
add_custom_command(TARGET MyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${gkcore_bin}" "$<TARGET_FILE_DIR:MyGame>"
    VERBATIM)
```

構成時に`-DCMAKE_PREFIX_PATH=C:/path/to/gkcore/sdk`を指定し、x64・Releaseでビルドします。
アプリの実行にはSDKの`bin/`一式が必要です。

使い方は[入力](input.md)、[画像](images.md)、[モデル](models.md)、[モデルアニメーション](model-animation.md)、[照明](lighting.md)、[ポストエフェクト](effects.md)にまとめています。
