# モデルを読み込む

`gk::LoadModel` にファイルの場所を渡すと、モデルハンドルが返ります。OBJ、GLB 2.0、FBX の静的メッシュを読み込めます。位置・回転・大きさを設定し、`gk::DrawModel` で描画します。FBX の読み込みと材質・画像の取り込みは CPU テストで確認しています。Windows/MSVC Runtime Release build/link と COM reflection は確認済みです。RTX 4070 SUPER の GPU smoke では GLB fixture に対する `LoadModel`、`DrawModel`、`Present` の成功を確認していますが、サンプルGLBについては、別のGPU画素テストで2球の色と照明方向による変化を確認し、起動画面も目視しました。[確認した画像](render-validation.md)を参照してください。FBX の GPU 描画確認を意味するものではありません。

```cpp
#include <stdio.h>
#include <gkcore.h>

int main() {
    if (gk::SetWindowSize(1280, 720) != 0 || gk::Init() != 0) return 1;

    const gk::ModelHandle scene = gk::LoadModel("assets/scene.fbx");
    if (!scene.IsValid()) {
        fprintf(stderr, "%s\n", gk::GetLastErrorMessage());
        gk::Shutdown();
        return 1;
    }

    gk::SetModelPosition(scene, gk::Vec3{0.0f, 0.0f, 0.0f});
    gk::SetModelRotation(scene, gk::Vec3{0.0f, 0.0f, 0.0f});
    gk::SetModelScale(scene, gk::Vec3{1.0f, 1.0f, 1.0f});
    bool failed = false;
    while (gk::ProcessEvents() && !gk::IsKeyDown(gk::Key::Escape)) {
        if (gk::BeginFrame() != 0 ||
            gk::SetDrawLayer(gk::DrawLayer::Scene) != 0 ||
            gk::DrawModel(scene) != 0 ||
            gk::Present() != 0) {
            failed = true;
            break;
        }
    }

    gk::DeleteModel(scene);
    gk::Shutdown();
    return failed ? 1 : 0;
}
```

読み込みに失敗すると無効なハンドルが返ります。`gk::GetLastErrorMessage()` で理由を確認してください。使い終わったモデルは `gk::DeleteModel(scene)` で解放します。カメラの設定とフレーム内の描画順は[クイックスタート](quickstart.md)を参照してください。

## ファイルの置き方

```text
assets/
├── scene.fbx       # LoadModel へ渡すモデル
└── textures/
    ├── wall.png     # 相対パスで参照する画像
```

FBX が外部画像を参照するときは、FBX ファイルの場所を基準にした相対パスで読み込みます。絶対パスは拒否されます。モデルと画像をまとめて移動できるよう、関連ファイルは同じ `assets` フォルダーに置いてください。

FBX モデルと相対パスの画像が読み込みから描画へ進む流れです。

```mermaid
flowchart LR
    M["assets/scene.fbx"] --> L["gk::LoadModel"]
    T["assets/textures/wall.png<br/>相対パスの画像"] --> L
    L --> H["ModelHandle"]
    H --> D["gk::DrawModel"]
```

## FBX の対応範囲

FBX は ASCII 形式とバイナリ形式の両方に対応します。`.fbx` も OBJ / GLB と同じ `gk::LoadModel("assets/scene.fbx")` で読み込みます。対象は静的メッシュと基本色係数・画像です。外部 PNG と埋め込み PNG の取り込みを確認しています。FBX での BMP 読み込みはまだ確認していません。

読み込んだモデルは右手系の Y-up、メートル単位になります。FBX の階層変換とメッシュの幾何変換を反映し、センチメートル単位のモデルも変換します。UV は先頭の UV セットを使い、V 座標を反転して画像の左上原点に合わせます。UV 変換を含む画像設定には対応しません。RGB 材質係数は線形値として扱い、画像の RGB は sRGB として読み込みます。画像は端の色で固定して描画します。繰り返し・鏡映の指定は反映しません。透明度係数はアルファ値へ反映しますが、アルファ合成方式の切り替えには対応しません。

GLB の metallic / roughness scalar factor と base-color factor / texture はモデル材質に使われます。OBJ と FBX は metallic `0`、roughness `1` の材質値で描画します。モデルには方向光と一様な環境光による材質照明を設定できます。使い方は[モデル照明ガイド](lighting.md)を参照してください。影、環境マップ / IBL、metallic-roughness texture、normal map、alpha mode の切り替えは未対応です。

アニメーション、スキニング、モーフターゲット、レイヤー合成や手続き的に生成する画像、UV 変換も対象外です。FBX の CPU 読み込みは確認済みです。Windows/MSVC Runtime Release build/link と COM reflection は確認済みです。GPU smoke は GLB fixture の描画 API 呼び出しと Present までを確認し、サンプルGLBの画素出力と見た目は追加の画像テストで確認しました。FBX個別のGPU描画は未確認です。形式ごとの最新状態は[機能一覧](ROADMAP.md)を参照してください。
