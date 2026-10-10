# キー入力

`gk::IsKeyDown` は、キーを押している間の状態を調べます。
移動のように押し続ける操作に使います。

入力はアプリのメインスレッドから問い合わせてください。

`gk::WasKeyPressed` は、直近の `gk::ProcessEvents()` で新しく押されたキーを調べます。
押した後、同じイベント処理中に離した場合も、その押下を記録します。
次の `ProcessEvents()` で記録を消し、同じ処理中に何度問い合わせても結果は変わりません。
OSによるキーリピートは、新しい押下として扱いません。

フォーカスを失うと、キーを押している状態と押下記録を消します。
フォーカスが戻ったとき、すでに押されているキーから押下記録は作りません。
未初期化の状態や `gk::Key` にない値を `WasKeyPressed` に渡すと `false` を返し、診断メッセージを設定します。
フォーカスがない場合も `false` を返します。

Spaceのような切り替え操作には `WasKeyPressed` を使います。
矢印キーのような押し続ける操作には `IsKeyDown` を使います。
終了操作では両方を調べると、短い押下と押し続けのどちらにも対応できます。

```cpp
#include <gkcore.h>
int main()
{
    if (gk::SetWindowSize(800, 600) != 0)
    {
        return 1;
    }
    if (gk::Init() != 0)
    {
        return 1;
    }
    // Spaceで切り替える矩形の色。
    bool enabled = false;
    // 矢印キーで動かす矩形の位置。
    float x = 32.0f;
    // 描画に失敗した場合の終了コード。
    int result = 0;
    while (gk::ProcessEvents())
    {
        if (gk::WasKeyPressed(gk::Key::Space))
        {
            enabled = !enabled;
        }
        if (gk::IsKeyDown(gk::Key::ArrowLeft))
        {
            x -= 2.0f;
        }
        if (gk::IsKeyDown(gk::Key::ArrowRight))
        {
            x += 2.0f;
        }
        if (gk::WasKeyPressed(gk::Key::Escape) || gk::IsKeyDown(gk::Key::Escape))
        {
            break;
        }
        if (gk::BeginFrame() != 0 || gk::DrawRect(x, 100.0f, 120.0f, 60.0f, enabled ? gk::ColorRGB(40, 220, 120) : gk::ColorRGB(220, 80, 40), true) != 0 || gk::Present() != 0)
        {
            result = 1;
            break;
        }
    }
    gk::Shutdown();
    return result;
}
```

利用できるキーは `gk::Key` を参照してください。
現在は Escape、矢印、Space、Enter、Tab、Backspace、数字、英字、Shift、Control を問い合わせできます。
