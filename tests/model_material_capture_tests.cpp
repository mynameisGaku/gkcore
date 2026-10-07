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
 * イベント処理の失敗を診断付きで通知し、終了要求と区別する。
 */
bool CheckEvents()
{
    if (gk::ProcessEvents())
    {
        return true;
    }
    // 終了要求は診断が空、処理失敗では診断が入る。
    const char* diagnostic = gk::GetLastErrorMessage();
    if (diagnostic && diagnostic[0])
    {
        std::fprintf(stderr, "ProcessEvents: %s\n", diagnostic);
    }
    return false;
}

/**
 * MR材質モデルをSceneまたはUIへ描き、必要なら連続再読込する。
 */
int main(int argc, char** argv)
{
    if (argc != 3 || (std::strcmp(argv[2], "scene") != 0 && std::strcmp(argv[2], "ui") != 0 && std::strcmp(argv[2], "stress") != 0))
    {
        std::fprintf(stderr, "usage: model_material_capture_tests <model-file> <scene|ui|stress>\n");
        return 2;
    }

    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    // 連続再読込を行うmodeかを判定する。
    const bool stressMode = std::strcmp(argv[2], "stress") == 0;
    // UI modeではモデルをScene背景の後ろに重ねる。
    const bool uiMode = std::strcmp(argv[2], "ui") == 0;
    // 一回描画か、再読込を繰り返すかを判定する結果。
    bool passed = Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -3.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera") && Check(gk::SetAmbientLight(0.1f), "SetAmbientLight") && Check(gk::SetDirectionalLight(gk::Vec3{ 0.0f, 0.0f, 1.0f }, 2.0f), "SetDirectionalLight") && Check(gk::SetBloomEnabled(false), "SetBloomEnabled") && Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity") && Check(gk::SetToneMappingEnabled(false), "SetToneMappingEnabled") && Check(gk::SetFxaaEnabled(false), "SetFxaaEnabled") && Check(gk::SetExposure(1.0f), "SetExposure") && Check(gk::SetSaturation(1.0f), "SetSaturation") && Check(gk::SetContrast(1.0f), "SetContrast") && Check(gk::SetPostEffectShader({}), "SetPostEffectShader(disabled)");

    // 単発描画か、texture cacheのcapacityを越える再読込回数。
    const int frameCount = stressMode ? 132 : 1;
    // 取得画像の比較対象を描画するframe番号。
    for (int frame = 0; passed && frame < frameCount; ++frame)
    {
        if (!CheckEvents())
        {
            passed = false;
            break;
        }
        // 各stress frameで新たに読み込むmodel handle。
        const gk::ModelHandle model = gk::LoadModel(argv[1]);
        if (!model.IsValid())
        {
            std::fprintf(stderr, "LoadModel(frame %d): %s\n", frame, gk::GetLastErrorMessage());
            passed = false;
            break;
        }

        passed = Check(gk::SetModelPosition(model, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetModelPosition") && Check(gk::SetModelRotation(model, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetModelRotation") && Check(gk::SetModelScale(model, gk::Vec3{ 1.0f, 1.0f, 1.0f }), "SetModelScale") && Check(gk::BeginFrame(), "BeginFrame") && Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)") && Check(gk::DrawRect(0.0f, 0.0f, 640.0f, 480.0f, gk::ColorRGB(40, 80, 120), true), "DrawRect(background)");
        if (passed && uiMode)
        {
            passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI model)");
        }
        if (passed)
        {
            passed = Check(gk::DrawModel(model), "DrawModel");
        }
        if (passed)
        {
            passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI marker)") && Check(gk::DrawRect(520.0f, 32.0f, 64.0f, 48.0f, gk::ColorRGB(0, 255, 0), true), "DrawRect(UI marker)");
        }

        if (stressMode && model.IsValid())
        {
            // Present前にhandleを破棄し、queue済みdrawが保持されることも通す。
            passed = Check(gk::DeleteModel(model), "DeleteModel before Present") && passed;
        }
        if (passed)
        {
            passed = Check(gk::Present(), "Present");
        }
        if (!stressMode && model.IsValid())
        {
            passed = Check(gk::DeleteModel(model), "DeleteModel after Present") && passed;
        }
    }

    gk::Shutdown();
    return passed ? 0 : 1;
}
