#include <gkcore.h>
#include <chrono>
#include <cstdio>

/**
 * API失敗を操作名と理由付きで記録する。
 */
bool Check(int result, const char* operation)
{
    if (result == 0)
    {
        return true;
    }
    std::fprintf(stderr, "%s: %s\n", operation, gk::GetLastErrorMessage());
    return false;
}

/**
 * 連続した123フレームを描画し、終了処理まで成功したか返す。
 */
int main()
{
    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    // 中央の小さな三角形だけを3Dカメラで確認できるようにする。
    // 各APIの失敗を後続の描画へ伝え、終了コードにも反映する。
    bool passed = Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -5.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera");
    // FPS基準ではなく、全フレーム処理の経過時間を記録する。
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; passed && frame < 123; ++frame)
    {
        if (!gk::ProcessEvents())
        {
            passed = false;
            break;
        }
        // 最後の矩形だけを交互に赤と青にし、描画順と前フレーム残留を検出する。
        const uint32_t finalRectangleColor = frame % 2 == 0 ? gk::ColorRGB(255, 0, 0) : gk::ColorRGB(0, 0, 255);
        passed = Check(gk::BeginFrame(), "BeginFrame") && Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "Scene");
        for (int rectangle = 0; passed && rectangle < 1024; ++rectangle)
        {
            // 最終矩形が欠けた場合も画像で分かるよう、先行分は灰色で覆う。
            const uint32_t rectangleColor = rectangle == 1023 ? finalRectangleColor : gk::ColorRGB(96, 96, 96);
            passed = Check(gk::DrawRect(0.0f, 0.0f, 640.0f, 480.0f, rectangleColor, true), "Scene overdraw rectangle");
        }
        passed = passed && Check(gk::DrawTriangle3D(gk::Vec3{ -0.25f, -0.20f, 0.0f }, gk::Vec3{ 0.25f, -0.20f, 0.0f }, gk::Vec3{ 0.0f, 0.25f, 0.0f }, gk::ColorRGB(0, 255, 255), true), "Scene triangle") && Check(gk::SetDrawLayer(gk::DrawLayer::UI), "UI") && Check(gk::DrawRect(480.0f, 32.0f, 96.0f, 64.0f, gk::ColorRGB(0, 255, 0), true), "UI rectangle") && Check(gk::DrawString(32.0f, 410.0f, "連続描画テスト", gk::ColorRGB(255, 255, 255)), "UI text") && Check(gk::Present(), "Present");
    }
    // 終了時間をFPS合否判定には使わず、診断用にだけ出力する。
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (passed)
    {
        std::printf("連続描画テスト: 123 frames, elapsed %lld ms\n", static_cast<long long>(elapsed));
    }
    gk::Shutdown();
    return passed ? 0 : 1;
}
