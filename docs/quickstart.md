# はじめての gkcore

利用側は `<gkcore.h>` だけを読み込み、`gk::` の関数でウィンドウ、入力、描画を操作します。The Forge API はアプリ側へ公開しません。公開 API とサンプルは C++ 標準ライブラリ/STL に依存しない方針です。

## 最小ループ

```cpp
#include <gkcore.h>

int main() {
    if (gk::SetWindowSize(1280, 720) != 0) return 1;
    if (gk::Init() != 0) return 1;

    while (gk::ProcessEvents() && !gk::IsKeyDown(gk::Key::Escape)) {
        if (gk::BeginFrame() != 0 ||
            gk::SetDrawLayer(gk::DrawLayer::Scene) != 0 ||
            gk::DrawRect(32.0f, 32.0f, 208.0f, 112.0f,
                         gk::ColorRGB(70, 150, 240), true) != 0 ||
            gk::SetDrawLayer(gk::DrawLayer::UI) != 0 ||
            gk::DrawString(32.0f, 660.0f, "Escape キーで終了", gk::ColorRGB(255, 255, 255)) != 0 ||
            gk::Present() != 0) break;
    }

    gk::Shutdown();
    return 0;
}
```

失敗した関数の直後に `gk::GetLastErrorMessage()` を使うと、原因の文字列を確認できます。`ProcessEvents()` はウィンドウが閉じられるまで `true` を返します。

## 2D と 3D を同じフレームに描く

[`examples/mixed_scene.cpp`](../examples/mixed_scene.cpp) は同じフレームの `Scene` 層に 3D 三角形と 2D 矩形を置き、`UI` 層に矩形と日本語文字を描く例です。`gk::DrawString` は Windows のシステム標準フォントを使い、同じ文字列・色・大きさの描画を上限付きキャッシュで再利用します。フォントファイルを別途用意する必要はありません。

PNG / BMP 画像と OBJ / GLB 2.0 / FBX の静的メッシュを読み込めます。FBX の ASCII・バイナリ形式、基本色係数、相対パスの PNG 画像取り込みは CPU テストで確認しています。GLB の metallic / roughness 係数と基本色、方向光・一様な環境光によるモデル照明を実装しています。OBJ / FBX は非金属・粗い材質の係数で描画します。影、環境マップ、metallic-roughness texture、normal map、alpha mode は未対応です。照明設定、法線、材質計画の CPU テストと Linux shader compile/reflection は成功していますが、Windows/MSVC link と実 GPU 表示は未確認です。形式ごとの手順は [モデルの読み込み](models.md)、[モデル照明ガイド](lighting.md)、機能一覧は [ROADMAP](ROADMAP.md) を確認してください。

3D カメラには `gk::SetCamera(gk::Vec3{...}, gk::Vec3{...})` で位置と注視点を渡します。モデルハンドルは `gk::LoadModel` で取得し、`gk::SetModelPosition`、`gk::SetModelRotation`、`gk::SetModelScale` で指定した値が後続の `gk::DrawModel` に使われます。使い終えたら `gk::DeleteModel` で解放します。画像も `ImageHandle` で管理します。

描画命令は `gk::BeginFrame()` と `gk::Present()` の間に追加します。`gk::DrawLayer::Scene` に 2D/3D のゲーム描画を置くと、HDR 描画先へまとめて描画されます。Scene には Bloom、露出、トーンマッピング、彩度・コントラスト調整、FXAA を適用し、その後 `gk::DrawLayer::UI` の HUD を合成します。Scene と UI は別の描画層で、それぞれの中の命令順を保ちます。効果の設定は `BeginFrame()` の時点で取り込まれるので、変更する場合は次のフレームが始まる前に設定します。

## キーとマウス

`gk::IsKeyDown(gk::Key::ArrowLeft)` などでキーの現在状態を、`gk::IsMouseButtonDown(gk::MouseButton::Left)` でマウスボタンの状態を調べます。`gk::GetMousePosition(x, y)` はクライアント領域内の座標を返します。入力はアプリの main thread から問い合わせます。ウィンドウに focus がない間はボタン状態が false になり、座標取得は false を返して `x` と `y` に 0 を書き込みます。

## ポストエフェクト

`gk::SetBloomEnabled`、`gk::SetBloomIntensity`、`gk::SetExposure`、`gk::SetToneMappingEnabled`、`gk::SetSaturation`、`gk::SetContrast`、`gk::SetFxaaEnabled` で効果を調整できます。Bloom、トーンマッピング、FXAA は初期設定で有効です。彩度とコントラストの `1.0f` は補正なしです。設定できる範囲や例は [ポストエフェクトの使い方](effects.md) を参照してください。Windows/MSVC でのリンクと実 GPU 上の見た目は未確認です。

## カスタムシェーダー

HLSL のソースは開発用コンパイラーで gkcore 用 artifact に変換し、`gk::LoadPixelShader` で読み込みます。最小の tint shader、共通入力、定数の渡し方は [カスタムシェーダーガイド](custom-shader.md) にあります。Windows/MSVC でのビルドと実 GPU 上の表示は未確認です。詳しい対応状況は [機能一覧](ROADMAP.md) を参照してください。

## 開発環境

開発環境は Windows 10/11 x64、Visual Studio 2022 または Visual Studio 2026 の C++ 開発環境と v142 14.29 toolset が対象です。CMake は Visual Studio 2022 では 3.21 以降、Visual Studio 2026 では 4.2 以降が必要です。The Forge/DXC の取得、ビルドとテストの手順は [`PRE_SETUP.bat`](../PRE_SETUP.bat) にあります。セットアップが成功すると `BUILD READY` と表示します。実 GPU の描画確認は DX12 対応 Windows PC で `PRE_SETUP.bat --gpu-check` を実行します。
