#include <Windows.h>
#include <gkcore.h>
#include <gkcore/Handle.h>
#include <gkcore/ModelSecondaryMotion.h>
#include <gkcore/FModelSecondaryMotionSettings.h>
#include <gkcore/FModelSecondaryMotionCollider.h>

/**
 * 配布headerとDLLだけで、揺れものの公開APIをリンク・呼び出しできることを確認する。
 */
bool VerifySecondaryMotionApiExports()
{
    // 配布headerだけで設定を作り、新しい揺れものAPIのexportも呼び出す。
    const gk::FModelSecondaryMotionSettings secondarySettings{};
    const gk::FModelSecondaryMotionCollider collider{};
    const uint32_t bone = 0;
    if (gk::SetModelSecondaryMotionChain({}, &bone, 1, secondarySettings) != -1 || gk::UpdateModelSecondaryMotion({}, 1.0 / 60.0) != -1 || gk::ResetModelSecondaryMotion({}) != -1 || gk::ClearModelSecondaryMotion({}) != -1 || gk::SetModelSecondaryMotionColliders({}, &collider, 1) != -1)
    {
        return false;
    }
    return true;
}

#if defined(GKCORE_PACKAGE_GPU_SMOKE)
/**
 * インストール済みSDKだけで初期化から最初の描画、終了まで確認する。
 */
int main()
{
    if (!VerifySecondaryMotionApiExports())
    {
        return 1;
    }
    if (gk::SetWindowSize(640, 480) != 0)
    {
        return 1;
    }
    if (gk::Init() != 0)
    {
        return 1;
    }

    // インストールSDKだけで、新しい押下問い合わせもリンク・呼び出しできることを確認する。
    static_cast<void>(gk::WasKeyPressed(gk::Key::Space));

    // 描画APIがすべて成功したかを記録する。
    bool passed = gk::BeginFrame() == 0;
    if (passed)
    {
        passed = gk::SetDrawLayer(gk::DrawLayer::Scene) == 0;
    }
    if (passed)
    {
        passed = gk::DrawRect(32.0f, 32.0f, 160.0f, 96.0f, gk::ColorRGB(32, 128, 224), true) == 0;
    }
    if (passed)
    {
        passed = gk::SetDrawLayer(gk::DrawLayer::UI) == 0;
    }
    if (passed)
    {
        passed = gk::DrawString(32.0f, 32.0f, "installed SDK smoke", gk::ColorRGB(255, 255, 255)) == 0;
    }
    if (passed)
    {
        passed = gk::Present() == 0;
    }

    gk::Shutdown();
    return passed ? 0 : 1;
}
#else
/**
 * GPUを使わずに公開header、リンク、無効handleの扱いを確認する。
 */
int main()
{
    if (!VerifySecondaryMotionApiExports())
    {
        return 1;
    }
    if (gk::WasKeyPressed(gk::Key::Space))
    {
        return 1;
    }
    // 無効状態の確認に使うhandle。
    gk::ImageHandle invalid;
    if (invalid)
    {
        return 1;
    }
    if (gk::ColorRGB(10, 20, 30) != 0x000a141eU)
    {
        return 1;
    }
    // 存在しない画像から無効handleが返るAPIをリンクする。
    const gk::ImageHandle missingImage = gk::LoadImage("__gkcore_missing_consumer_fixture__.png");
    if (missingImage)
    {
        return 1;
    }
    return gk::DrawString(0.0f, 0.0f, "link check", 0xffffffffU) == -1 ? 0 : 1;
}
#endif
