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
 * 終了要求とevent処理失敗を診断で区別する。
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
 * sampler検証用GLBをSceneまたはUIへ描き、指定frameの画像を取得する。
 */
int main(int argc, char** argv)
{
    if (argc != 3 || (std::strcmp(argv[2], "scene") != 0 && std::strcmp(argv[2], "ui") != 0 && std::strcmp(argv[2], "scene-pbr") != 0 && std::strcmp(argv[2], "ui-pbr") != 0 && std::strcmp(argv[2], "stress") != 0))
    {
        std::fprintf(stderr, "usage: model_sampler_capture_tests <model-file> <scene|ui|scene-pbr|ui-pbr|stress>\n");
        return 2;
    }

    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    // 通常の色sample検査とPBR反射検査で照明を切り替える。
    const bool pbrMode = std::strcmp(argv[2], "scene-pbr") == 0 || std::strcmp(argv[2], "ui-pbr") == 0;
    // capture中の照明と後処理を固定する。
    bool passed = Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -3.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera") && Check(gk::SetAmbientLight(pbrMode ? 0.1f : 1.0f), "SetAmbientLight") && Check(gk::SetDirectionalLight(pbrMode ? gk::Vec3{ 0.7f, 0.4f, 1.0f } : gk::Vec3{ 0.0f, -1.0f, 0.0f }, pbrMode ? 2.0f : 0.0f), "SetDirectionalLight") && Check(gk::SetBloomEnabled(false), "SetBloomEnabled") && Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity") && Check(gk::SetToneMappingEnabled(false), "SetToneMappingEnabled") && Check(gk::SetFxaaEnabled(false), "SetFxaaEnabled") && Check(gk::SetExposure(1.0f), "SetExposure") && Check(gk::SetSaturation(1.0f), "SetSaturation") && Check(gk::SetContrast(1.0f), "SetContrast") && Check(gk::SetPostEffectShader({}), "SetPostEffectShader(disabled)");

    // stressでは複数frameでtexture variantの退避後描画を確認する。
    const bool stressMode = std::strcmp(argv[2], "stress") == 0;
    // stress検査に使うframe数。
    const int frameCount = stressMode ? 132 : 1;
    // 指定したframeだけcaptureし、各回でmodelを新規decodeするloop。
    for (int frame = 0; passed && frame < frameCount; ++frame)
    {
        if (!CheckEvents())
        {
            passed = false;
            break;
        }
        // このframeで読み込むmodelの所有handle。
        const gk::ModelHandle model = gk::LoadModel(argv[1]);
        if (!model.IsValid())
        {
            std::fprintf(stderr, "LoadModel(frame %d): %s\n", frame, gk::GetLastErrorMessage());
            passed = false;
            break;
        }

        // modelをUI layerへ送るかを示すmode判定。
        const bool uiMode = std::strcmp(argv[2], "ui") == 0 || std::strcmp(argv[2], "ui-pbr") == 0;
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
            // Present前に所有handleを破棄し、登録済みdrawの画像保持を確認する。
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
