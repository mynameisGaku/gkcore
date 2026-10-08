#include <gkcore.h>
#include <cstdio>
#include <cstring>

/**
 * Runtime APIの失敗を操作名と診断付きで出力する。
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
 * GLB mip samplerをSceneまたはUIへ描き、指定frameの画素を取得する。
 */
int main(int argc, char** argv)
{
    if (argc != 3 || (std::strcmp(argv[2], "scene") != 0 && std::strcmp(argv[2], "ui") != 0 && std::strcmp(argv[2], "pbr-scene") != 0 && std::strcmp(argv[2], "pbr-ui") != 0 && std::strcmp(argv[2], "stress") != 0))
    {
        std::fprintf(stderr, "usage: model_mip_capture_tests <model-file> <scene|ui|pbr-scene|pbr-ui|stress>\n");
        return 2;
    }

    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    // 法線・MRを比較するframeだけPBR照明を使う。
    const bool pbrMode = std::strcmp(argv[2], "pbr-scene") == 0 || std::strcmp(argv[2], "pbr-ui") == 0;
    // capture中の視点、照明、後処理を固定する。
    bool passed = Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -3.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera") && Check(gk::SetAmbientLight(pbrMode ? 0.1f : 1.0f), "SetAmbientLight") && Check(gk::SetDirectionalLight(pbrMode ? gk::Vec3{ 0.7f, 0.4f, 1.0f } : gk::Vec3{ 0.0f, -1.0f, 0.0f }, pbrMode ? 2.0f : 0.0f), "SetDirectionalLight") && Check(gk::SetBloomEnabled(false), "SetBloomEnabled") && Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity") && Check(gk::SetToneMappingEnabled(false), "SetToneMappingEnabled") && Check(gk::SetFxaaEnabled(false), "SetFxaaEnabled") && Check(gk::SetExposure(1.0f), "SetExposure") && Check(gk::SetSaturation(1.0f), "SetSaturation") && Check(gk::SetContrast(1.0f), "SetContrast") && Check(gk::SetPostEffectShader({}), "SetPostEffectShader(disabled)");

    // stressでは各frameに画像を再読込し、Present前にhandleを解放する。
    const bool stressMode = std::strcmp(argv[2], "stress") == 0;
    // stress検査に使う連続frame数。
    const int frameCount = stressMode ? 132 : 1;
    // frameごとに新しいmodelを読み、描画を登録するloop。
    for (int frame = 0; passed && frame < frameCount; ++frame)
    {
        if (!CheckEvents())
        {
            passed = false;
            break;
        }
        // 現在frameだけ使うmodel handle。
        const gk::ModelHandle model = gk::LoadModel(argv[1]);
        if (!model.IsValid())
        {
            std::fprintf(stderr, "LoadModel(frame %d): %s\n", frame, gk::GetLastErrorMessage());
            passed = false;
            break;
        }

        // modelをUIへ送るかを示すmode判定。
        const bool uiMode = std::strcmp(argv[2], "ui") == 0 || std::strcmp(argv[2], "pbr-ui") == 0;
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
            // Present前にownerを解放し、登録済みdrawのtexture保持を試す。
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
