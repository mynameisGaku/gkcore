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
    // 終了要求では空、イベント処理の失敗では診断が入る。
    const char* diagnostic = gk::GetLastErrorMessage();
    if (diagnostic && diagnostic[0])
    {
        std::fprintf(stderr, "ProcessEvents: %s\n", diagnostic);
    }
    return false;
}

/**
 * 指定GLBとmodeでalpha MASKのScene/UI/depth出力を取得する。
 */
int main(int argc, char** argv)
{
    const bool customReject = argc == 4 && std::strcmp(argv[2], "custom-reject") == 0;
    const bool normalCapture = argc == 3 && (std::strcmp(argv[2], "scene") == 0 || std::strcmp(argv[2], "ui") == 0 || std::strcmp(argv[2], "depth") == 0);
    if (!customReject && !normalCapture)
    {
        std::fprintf(stderr, "usage: model_alpha_capture_tests <model-file> <scene|ui|depth> [shader-path for custom-reject]\n");
        return 2;
    }

    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    // 検査対象GLBのhandle。
    const gk::ModelHandle model = gk::LoadModel(argv[1]);
    // API失敗を後続処理の終了コードへ反映する状態。
    bool passed = model.IsValid();
    if (!passed)
    {
        std::fprintf(stderr, "LoadModel: %s\n", gk::GetLastErrorMessage());
    }

    // custom shader付きモデルの負の契約で使うhandle。
    gk::ShaderHandle customShader{};
    if (passed && customReject)
    {
        customShader = gk::LoadPixelShader(argv[3]);
        passed = customShader.IsValid();
        if (!passed)
        {
            std::fprintf(stderr, "LoadPixelShader: %s\n", gk::GetLastErrorMessage());
        }
        passed = passed && Check(gk::SetShaderFloat4(customShader, 0, gk::Float4{ 1.0f, 1.0f, 1.0f, 1.0f }), "SetShaderFloat4(slot 0)");
    }

    passed = passed && Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -3.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera") && Check(gk::SetModelPosition(model, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetModelPosition") && Check(gk::SetModelRotation(model, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetModelRotation") && Check(gk::SetModelScale(model, gk::Vec3{ 1.0f, 1.0f, 1.0f }), "SetModelScale") && Check(gk::SetAmbientLight(1.0f), "SetAmbientLight") && Check(gk::SetDirectionalLight(gk::Vec3{ 0.0f, -1.0f, 0.0f }, 0.0f), "SetDirectionalLight") && Check(gk::SetBloomEnabled(false), "SetBloomEnabled") && Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity") && Check(gk::SetToneMappingEnabled(false), "SetToneMappingEnabled") && Check(gk::SetFxaaEnabled(false), "SetFxaaEnabled") && Check(gk::SetExposure(1.0f), "SetExposure") && Check(gk::SetSaturation(1.0f), "SetSaturation") && Check(gk::SetContrast(1.0f), "SetContrast") && Check(gk::SetPostEffectShader({}), "SetPostEffectShader(disabled)");

    if (passed)
    {
        passed = CheckEvents();
    }
    if (passed)
    {
        passed = Check(gk::BeginFrame(), "BeginFrame") && Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)") && Check(gk::DrawRect(0.0f, 0.0f, 640.0f, 480.0f, gk::ColorRGB(40, 80, 120), true), "DrawRect(background)");
        if (passed && customReject)
        {
            passed = Check(gk::SetPixelShader(customShader), "SetPixelShader(custom)") && Check(gk::DrawModel(model), "DrawModel(custom masked)");
            if (passed)
            {
                const int presentResult = gk::Present();
                const char* diagnostic = gk::GetLastErrorMessage();
                const char* expectedText = "custom pixel shaders do not support masked model materials";
                if (presentResult == -1 && diagnostic && std::strstr(diagnostic, expectedText))
                {
                    std::fprintf(stdout, "expected masked custom shader rejection: %s\n", diagnostic);
                }
                else
                {
                    std::fprintf(stderr, "custom shader rejection mismatch: result=%d diagnostic=%s\n", presentResult, diagnostic ? diagnostic : "");
                    passed = false;
                }
                passed = Check(gk::SetPixelShader({}), "SetPixelShader(disabled after rejection)") && passed;
            }
        }
        else if (passed && std::strcmp(argv[2], "ui") == 0)
        {
            passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI model)") && Check(gk::DrawModel(model), "DrawModel(UI)");
        }
        else if (passed)
        {
            passed = Check(gk::DrawModel(model), "DrawModel(Scene)");
            if (passed && std::strcmp(argv[2], "depth") == 0)
            {
                // カメラから遠い青い面を同じ画面範囲へ置き、discard後のdepth書き込みを検査する。
                // 青い背面の左下頂点。
                const gk::Vec3 leftBottom{ -1.05f, -0.7583333f, 0.5f };
                // 青い背面の右下頂点。
                const gk::Vec3 rightBottom{ 1.05f, -0.7583333f, 0.5f };
                // 青い背面の右上頂点。
                const gk::Vec3 rightTop{ 1.05f, 0.7583333f, 0.5f };
                // 青い背面の左上頂点。
                const gk::Vec3 leftTop{ -1.05f, 0.7583333f, 0.5f };
                passed = Check(gk::DrawTriangle3D(leftBottom, rightBottom, rightTop, gk::ColorRGB(0, 0, 255), true), "DrawTriangle3D(back lower)") && Check(gk::DrawTriangle3D(leftBottom, rightTop, leftTop, gk::ColorRGB(0, 0, 255), true), "DrawTriangle3D(back upper)");
            }
        }
        if (passed && !customReject)
        {
            passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI marker)") && Check(gk::DrawRect(520.0f, 32.0f, 64.0f, 48.0f, gk::ColorRGB(0, 255, 0), true), "DrawRect(UI marker)") && Check(gk::Present(), "Present");
        }
    }

    if (model.IsValid())
    {
        passed = Check(gk::DeleteModel(model), "DeleteModel after Present") && passed;
    }
    if (customShader.IsValid())
    {
        passed = Check(gk::DeleteShader(customShader), "DeleteShader after Present") && passed;
    }
    gk::Shutdown();
    return passed ? 0 : 1;
}
