#include <gkcore.h>
#include <cstdio>
#include <cstring>

/**
 * API失敗を操作名と診断付きで出力する。
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
 * 画面全体へ各効果の比較に必要な形を描く。
 */
bool DrawFrame()
{
    bool passed = Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)");
    passed = Check(gk::DrawRect(32.0f, 32.0f, 128.0f, 96.0f, gk::ColorRGB(128, 64, 32), true), "DrawRect orange") && passed;
    passed = Check(gk::DrawRect(180.0f, 32.0f, 96.0f, 96.0f, gk::ColorRGB(100, 100, 100), true), "DrawRect gray") && passed;
    passed = Check(gk::DrawTriangle3D(gk::Vec3{ -1.0f, -1.0f, 0.0f }, gk::Vec3{ 1.0f, -1.0f, 0.0f }, gk::Vec3{ 0.0f, 1.0f, 0.0f }, gk::ColorRGB(255, 255, 255), true), "DrawTriangle3D") && passed;
    passed = Check(gk::DrawRect(480.0f, 160.0f, 32.0f, 32.0f, gk::ColorRGB(255, 255, 255), true), "DrawRect bloom patch") && passed;
    passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI)") && passed;
    passed = Check(gk::DrawRect(480.0f, 32.0f, 96.0f, 64.0f, gk::ColorRGB(0, 255, 0), true), "DrawRect UI") && passed;
    passed = Check(gk::DrawRect(16.0f, 400.0f, 256.0f, 64.0f, gk::ColorRGB(0, 0, 0), true), "DrawRect text backing") && passed;
    passed = Check(gk::DrawString(32.0f, 420.0f, "エフェクト検査", gk::ColorRGB(255, 255, 255)), "DrawString") && passed;
    return passed;
}

/**
 * 指定modeの設定で1フレーム、または設定時点を調べる3フレームを描く。
 */
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: post_effect_capture_tests <mode>\n");
        return 2;
    }
    const bool snapshot = std::strcmp(argv[1], "snapshot") == 0;
    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    bool passed = Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -5.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera");
    passed = Check(gk::SetBloomEnabled(false), "SetBloomEnabled baseline") && passed;
    passed = Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity baseline") && passed;
    passed = Check(gk::SetExposure(1.0f), "SetExposure baseline") && passed;
    passed = Check(gk::SetSaturation(1.0f), "SetSaturation baseline") && passed;
    passed = Check(gk::SetContrast(1.0f), "SetContrast baseline") && passed;
    passed = Check(gk::SetToneMappingEnabled(true), "SetToneMappingEnabled baseline") && passed;
    passed = Check(gk::SetFxaaEnabled(false), "SetFxaaEnabled baseline") && passed;

    if (std::strcmp(argv[1], "exposure_low") == 0)
    {
        passed = Check(gk::SetExposure(0.25f), "SetExposure low") && passed;
    }
    else if (std::strcmp(argv[1], "exposure_high") == 0)
    {
        passed = Check(gk::SetExposure(4.0f), "SetExposure high") && passed;
    }
    else if (std::strcmp(argv[1], "tone_off") == 0)
    {
        passed = Check(gk::SetToneMappingEnabled(false), "SetToneMappingEnabled off") && passed;
    }
    else if (std::strcmp(argv[1], "saturation_zero") == 0)
    {
        passed = Check(gk::SetSaturation(0.0f), "SetSaturation zero") && passed;
    }
    else if (std::strcmp(argv[1], "saturation_high") == 0)
    {
        passed = Check(gk::SetSaturation(2.0f), "SetSaturation high") && passed;
    }
    else if (std::strcmp(argv[1], "contrast_zero") == 0)
    {
        passed = Check(gk::SetContrast(0.0f), "SetContrast zero") && passed;
    }
    else if (std::strcmp(argv[1], "contrast_high") == 0)
    {
        passed = Check(gk::SetContrast(2.0f), "SetContrast high") && passed;
    }
    else if (std::strcmp(argv[1], "bloom_zero") == 0)
    {
        passed = Check(gk::SetBloomEnabled(true), "SetBloomEnabled zero") && passed;
        passed = Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity zero") && passed;
    }
    else if (std::strcmp(argv[1], "bloom_high") == 0)
    {
        passed = Check(gk::SetBloomEnabled(true), "SetBloomEnabled high") && passed;
        passed = Check(gk::SetBloomIntensity(4.0f), "SetBloomIntensity high") && passed;
    }
    else if (std::strcmp(argv[1], "fxaa_on") == 0)
    {
        passed = Check(gk::SetFxaaEnabled(true), "SetFxaaEnabled on") && passed;
    }
    else if (!snapshot && std::strcmp(argv[1], "baseline") != 0)
    {
        std::fprintf(stderr, "unknown mode: %s\n", argv[1]);
        passed = false;
    }

    const int frameCount = snapshot ? 3 : 1;
    for (int frame = 0; passed && frame < frameCount; ++frame)
    {
        passed = Check(gk::BeginFrame(), "BeginFrame");
        if (snapshot && frame == 0)
        {
            passed = Check(gk::SetExposure(4.0f), "SetExposure after BeginFrame high") && passed;
        }
        else if (snapshot && frame == 1)
        {
            passed = Check(gk::SetExposure(0.25f), "SetExposure after BeginFrame low") && passed;
        }
        passed = DrawFrame() && passed;
        passed = Check(gk::Present(), "Present") && passed;
    }

    gk::Shutdown();
    return passed ? 0 : 1;
}
