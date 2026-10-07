#include <gkcore.h>
#include <cstdio>

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
 * 窓のイベントを処理し、終了要求を検出する。
 */
bool CheckEvents()
{
    if (gk::ProcessEvents())
    {
        return true;
    }
    // イベント処理が失敗した理由。
    const char* diagnostic = gk::GetLastErrorMessage();
    std::fprintf(stderr, "ProcessEvents failed%s%s\n", diagnostic && diagnostic[0] ? ": " : "", diagnostic && diagnostic[0] ? diagnostic : "");
    return false;
}

/**
 * 指定GLBをSceneへ描き、UIを重ねて画像検査用の1フレームを表示する。
 */
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: glb_texture_capture_tests <model-path>\n");
        return 2;
    }
    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    // モデルの色を照明や効果で変えずに比較するための設定。
    bool passed = Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -3.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera") && Check(gk::SetAmbientLight(1.0f), "SetAmbientLight") && Check(gk::SetDirectionalLight(gk::Vec3{ 0.0f, -1.0f, 0.0f }, 0.0f), "SetDirectionalLight") && Check(gk::SetBloomEnabled(false), "SetBloomEnabled") && Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity") && Check(gk::SetToneMappingEnabled(false), "SetToneMappingEnabled") && Check(gk::SetFxaaEnabled(false), "SetFxaaEnabled") && Check(gk::SetExposure(1.0f), "SetExposure") && Check(gk::SetSaturation(1.0f), "SetSaturation") && Check(gk::SetContrast(1.0f), "SetContrast") && Check(gk::SetPostEffectShader({}), "SetPostEffectShader disabled");
    // 描画するGLBモデルのhandle。
    const gk::ModelHandle model = gk::LoadModel(argv[1]);
    if (!model.IsValid())
    {
        std::fprintf(stderr, "LoadModel: %s\n", gk::GetLastErrorMessage());
        passed = false;
    }

    if (passed)
    {
        passed = CheckEvents();
    }
    if (passed)
    {
        passed = Check(gk::BeginFrame(), "BeginFrame") && Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)") && Check(gk::DrawRect(0.0f, 0.0f, 640.0f, 480.0f, gk::ColorRGB(40, 80, 120), true), "DrawRect(Scene background)") && Check(gk::DrawModel(model), "DrawModel") && Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI)") && Check(gk::DrawRect(520.0f, 32.0f, 64.0f, 48.0f, gk::ColorRGB(0, 255, 0), true), "DrawRect(UI marker)") && Check(gk::Present(), "Present");
    }

    if (model.IsValid())
    {
        passed = Check(gk::DeleteModel(model), "DeleteModel after Present") && passed;
    }
    gk::Shutdown();
    return passed ? 0 : 1;
}
