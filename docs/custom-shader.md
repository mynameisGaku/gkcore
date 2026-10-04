# カスタムピクセルシェーダー

HLSL でピクセルシェーダーを作り、開発用コンパイラーで gkcore 用の artifact に変換してから `gk::LoadPixelShader` で読み込みます。実行時に HLSL をコンパイルする必要はありません。

## シェーダーの入力と定数

`<gkcore/Shader.hlsl>` が共通のピクセル入力とリソース名を定義します。自分で頂点シェーダーを書く必要はなく、gkcore が渡す次の入力から `float4` の色を返します。

| HLSL 名 | 意味 |
|---|---|
| `GkcorePixelInput.position` | 画面上の位置 (`SV_Position`) |
| `GkcorePixelInput.color` | 描画命令から渡される色 (`COLOR0`) |
| `GkcorePixelInput.uv` | 画像の UV 座標 (`TEXCOORD0`) |
| `gkcoreUserData[0..63]` | `gk::SetShaderFloat4` で指定する 64 個の定数 (`b0, space3`) |
| `gkcoreTexture` / `gkcoreSampler` | 必要な場合に使う画像と sampler (`t0` / `s0, space0`) |

ピクセルシェーダーの戻り値は `SV_Target0` に出力します。`examples/shaders/tint.hlsl` は、描画色に定数 slot 0 の tint を掛ける最小例です。

```hlsl
#include <gkcore/Shader.hlsl>

float4 main(GkcorePixelInput input) : SV_Target0
{
    return input.color * gkcoreUserData[0];
}
```

画像を使う shader では `gkcoreTexture.Sample(gkcoreSampler, input.uv)` を色に掛けられます。`DrawImage` のときはその画像が binding され、画像を使わない描画では白い texture が渡ります。画像の GPU 転送とスプライト描画経路は gkcore 側で用意します。

## コンパイル

HLSL ソースは開発用ツールでコンパイルします。The Forge と DXC 1.8.2405 を `PRE_SETUP.bat` で取得した開発環境から、次を実行します。

```bat
python tools/compile_pixel_shader.py --dxc-root .devtools/dxc-1.8.2405 --input examples/shaders/tint.hlsl --output build/tint.frag
```

出力される `.frag` は DXIL ピクセルシェーダーを FSL 形式に包んだコンパイル済みファイルです。`gk::LoadPixelShader` はこの形式を読み込みます。shader の配布では、このファイルと実行に必要な DLL を Runtime SDK に含めます。DXC コンパイラーの実行ファイルや開発スクリプト、The Forge の開発用ソースは含めません。

## アプリから使う

shader を選択してから描画命令を追加します。定数はその値を設定した後にキューへ追加した各描画命令へ複写されます。shader を使わない描画には無効 handle を選び、内蔵 shader に戻します。

次のコードは公開 API の使用手順です。D3D12 描画部にはコンパイル済み shader の読み込み、入力検査、描画 pipeline と定数・画像 binding の処理が実装されています。Release CTest 17 件と MinGW 構文検査は成功しましたが、Windows/MSVC でのリンク、Windows の COM reflection、実 GPU 上の表示は未確認です。この例を Windows で動作確認済みとは扱わないでください。

```cpp
#include <stdio.h>
#include <gkcore.h>

namespace {
/**
 * Prints the latest gkcore diagnostic for the failed operation.
 */
void PrintError(const char* operation) {
    fprintf(stderr, "%s: %s\n", operation, gk::GetLastErrorMessage());
}
}

int main() {
    if (gk::SetWindowSize(960, 540) != 0 || gk::Init() != 0) {
        PrintError("gkcore の初期化");
        return 1;
    }

    const gk::ShaderHandle tint = gk::LoadPixelShader("build/tint.frag");
    if (!tint.IsValid()) {
        PrintError("シェーダーの読み込み");
        gk::Shutdown();
        return 1;
    }

    bool failed = false;
    while (gk::ProcessEvents() && !gk::IsKeyDown(gk::Key::Escape)) {
        if (gk::BeginFrame() != 0 ||
            gk::SetDrawLayer(gk::DrawLayer::Scene) != 0 ||
            gk::SetShaderFloat4(tint, 0, gk::Float4{1.0f, 0.55f, 0.55f, 1.0f}) != 0 ||
            gk::SetPixelShader(tint) != 0 ||
            gk::DrawRect(80.0f, 80.0f, 240.0f, 140.0f,
                         gk::ColorRGB(255, 255, 255), true) != 0 ||
            gk::SetPixelShader(gk::ShaderHandle{}) != 0 ||
            gk::SetDrawLayer(gk::DrawLayer::UI) != 0 ||
            gk::DrawString(24.0f, 24.0f, "Escape キーで終了",
                           gk::ColorRGB(255, 255, 255)) != 0 ||
            gk::Present() != 0) {
            PrintError("フレームの描画");
            failed = true;
            break;
        }
    }

    if (!failed && gk::DeleteShader(tint) != 0) {
        PrintError("シェーダーの解放");
        failed = true;
    }
    gk::Shutdown();
    return failed ? 1 : 0;
}
```

## 現在の対応範囲

公開 API は shader handle、64 個の `Float4` 定数、描画命令ごとの設定 snapshot を提供し、1 フレームあたり独自 shader を使う描画は最大 4096 件です。D3D12 描画部は共通の頂点シェーダーを使い、Scene/UI、深度検査、alpha blending の pipeline を用意しています。reflection 検査は `b0, space3` の定数と、任意の `t0, space0` / `s0, space0` binding を確認します。API はピクセルシェーダーのみを扱います。独自の頂点 shader、PBR model material shader、post-effect shader は含みません。Linux DXC で tint HLSL のコンパイルと FSL 形式への変換を確認しましたが、Windows 用 DXC package、Windows COM reflection、Runtime 上の表示を検証した結果ではありません。
