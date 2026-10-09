#include <gkcore.h>
#include <cstdio>
#include <cstring>

namespace
{

/**
 * API失敗を操作名と診断付きで表示する。
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
 * 終了要求とevent処理失敗を診断で分ける。
 */
bool CheckEvents()
{
    if (gk::ProcessEvents())
    {
        return true;
    }
    const char* diagnostic = gk::GetLastErrorMessage();
    if (diagnostic && diagnostic[0])
    {
        std::fprintf(stderr, "ProcessEvents: %s\n", diagnostic);
    }
    return false;
}

/**
 * 指定名のfixtureを読み込む。
 */
gk::ModelHandle Load(const char* directory, const char* name)
{
    char path[1024];
    const int length = std::snprintf(path, sizeof(path), "%s/%s.glb", directory, name);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(path))
    {
        std::fprintf(stderr, "fixture path is too long: %s\n", name);
        return {};
    }
    const gk::ModelHandle model = gk::LoadModel(path);
    if (!model.IsValid())
    {
        std::fprintf(stderr, "LoadModel(%s): %s\n", name, gk::GetLastErrorMessage());
    }
    return model;
}

/**
 * Scene背景と透明モデルfixtureを1frame描画する。
 */
bool DrawFrame(const char* directory, const char* mode)
{
    const bool swatchMode = std::strcmp(mode, "swatch") == 0;
    const bool overlapMode = std::strcmp(mode, "overlap-scene") == 0 || std::strcmp(mode, "overlap-ui") == 0 || std::strcmp(mode, "overlap-camera-reverse") == 0;
    const bool triangleMode = std::strcmp(mode, "triangles-scene") == 0 || std::strcmp(mode, "triangles-camera-reverse") == 0;
    const bool overlayBarrierMode = std::strcmp(mode, "overlay-barrier") == 0;
    const bool maskMode = std::strcmp(mode, "mask-depth") == 0;
    const bool opaqueFrontMode = std::strcmp(mode, "opaque-front") == 0;
    const bool projectionOpaqueMode = std::strcmp(mode, "projection-opaque-actual") == 0 || std::strcmp(mode, "projection-opaque-reference") == 0 || std::strcmp(mode, "projection-opaque-identity") == 0;
    const bool projectionBlendMode = std::strcmp(mode, "projection-blend-actual") == 0 || std::strcmp(mode, "projection-blend-reference") == 0 || std::strcmp(mode, "projection-blend-reversed") == 0;
    const bool projectionMode = projectionOpaqueMode || projectionBlendMode;
    if (!swatchMode && !overlapMode && !triangleMode && !overlayBarrierMode && !maskMode && !opaqueFrontMode && !projectionMode)
    {
        std::fprintf(stderr, "unknown blend capture mode: %s\n", mode);
        return false;
    }

    const bool reverseCamera = std::strcmp(mode, "overlap-camera-reverse") == 0 || std::strcmp(mode, "triangles-camera-reverse") == 0;
    const bool uiMode = std::strcmp(mode, "overlap-ui") == 0;
    const gk::Vec3 cameraPosition = projectionMode ? gk::Vec3{ 0.42f, 0.24f, -3.4f } : (reverseCamera ? gk::Vec3{ 0.0f, 0.0f, 3.0f } : gk::Vec3{ 0.0f, 0.0f, -3.0f });
    const gk::Vec3 cameraTarget = projectionMode ? gk::Vec3{ 0.05f, -0.03f, 0.0f } : gk::Vec3{ 0.0f, 0.0f, 0.0f };
    bool passed = Check(gk::SetCamera(cameraPosition, cameraTarget), "SetCamera") && Check(gk::SetAmbientLight(projectionMode ? 0.22f : 1.0f), "SetAmbientLight") && Check(gk::SetDirectionalLight(projectionMode ? gk::Vec3{ 0.4f, 0.4f, -0.8f } : gk::Vec3{ 0.0f, 0.0f, -1.0f }, projectionMode ? 1.5f : 0.0f), "SetDirectionalLight") && Check(gk::SetBloomEnabled(false), "SetBloomEnabled") && Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity") && Check(gk::SetToneMappingEnabled(false), "SetToneMappingEnabled") && Check(gk::SetFxaaEnabled(false), "SetFxaaEnabled") && Check(gk::SetExposure(1.0f), "SetExposure") && Check(gk::SetSaturation(1.0f), "SetSaturation") && Check(gk::SetContrast(1.0f), "SetContrast") && Check(gk::SetPostEffectShader({}), "SetPostEffectShader(disabled)");

    gk::ModelHandle first{};
    gk::ModelHandle second{};
    if (passed && swatchMode)
    {
        first = Load(directory, "blend-swatch");
        passed = first.IsValid() && Check(gk::SetModelPosition(first, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetModelPosition(swatch)");
    }
    else if (passed && overlapMode)
    {
        first = Load(directory, "blend-red");
        second = Load(directory, "blend-blue");
        passed = first.IsValid() && second.IsValid() && Check(gk::SetModelPosition(first, gk::Vec3{ 0.0f, 0.0f, -0.35f }), "SetModelPosition(near red)") && Check(gk::SetModelPosition(second, gk::Vec3{ 0.0f, 0.0f, 0.35f }), "SetModelPosition(far blue)");
    }
    else if (passed && triangleMode)
    {
        first = Load(directory, "blend-triangle-pair");
        passed = first.IsValid() && Check(gk::SetModelPosition(first, gk::Vec3{ 0.1f, 0.0f, 0.0f }), "SetModelPosition(triangle pair)");
    }
    else if (passed && overlayBarrierMode)
    {
        first = Load(directory, "blend-red");
        passed = first.IsValid() && Check(gk::SetModelPosition(first, gk::Vec3{ 0.0f, 0.0f, -0.35f }), "SetModelPosition(barrier model)");
    }
    else if (passed && maskMode)
    {
        first = Load(directory, "blend-mask-front");
        second = Load(directory, "blend-opaque-back");
        passed = first.IsValid() && second.IsValid() && Check(gk::SetModelPosition(first, gk::Vec3{ 0.0f, 0.0f, -0.35f }), "SetModelPosition(mask front)") && Check(gk::SetModelPosition(second, gk::Vec3{ 0.0f, 0.0f, 0.35f }), "SetModelPosition(opaque back)");
    }
    else if (passed && opaqueFrontMode)
    {
        first = Load(directory, "blend-opaque-front");
        second = Load(directory, "blend-blue");
        passed = first.IsValid() && second.IsValid() && Check(gk::SetModelPosition(first, gk::Vec3{ 0.0f, 0.0f, -0.35f }), "SetModelPosition(opaque front)") && Check(gk::SetModelPosition(second, gk::Vec3{ 0.0f, 0.0f, 0.35f }), "SetModelPosition(blend back)");
    }
    else if (passed && projectionOpaqueMode)
    {
        const bool reference = std::strcmp(mode, "projection-opaque-reference") == 0;
        first = Load(directory, reference ? "projection-opaque-reference" : "projection-opaque-actual");
        passed = first.IsValid();
    }
    else if (passed && projectionBlendMode)
    {
        const bool reference = std::strcmp(mode, "projection-blend-reference") == 0;
        first = Load(directory, reference ? "projection-blend-red-reference" : "projection-blend-red-actual");
        second = Load(directory, reference ? "projection-blend-blue-reference" : "projection-blend-blue-actual");
        passed = first.IsValid() && second.IsValid();
    }

    if (passed && projectionMode)
    {
        const bool actual = std::strcmp(mode, "projection-opaque-actual") == 0 || std::strcmp(mode, "projection-blend-actual") == 0 || std::strcmp(mode, "projection-blend-reversed") == 0;
        // 公開APIの回転単位に合わせ、fixture生成時の角度をradianで渡す。
        const gk::Vec3 rotation = actual ? gk::Vec3{ 0.4014257f, -0.5934119f, 0.3316126f } : gk::Vec3{ 0.0f, 0.0f, 0.0f };
        const gk::Vec3 scale = actual ? gk::Vec3{ -0.82f, 1.27f, 0.61f } : gk::Vec3{ 1.0f, 1.0f, 1.0f };
        const gk::Vec3 firstPosition = projectionOpaqueMode ? gk::Vec3{ 0.08f, -0.04f, 0.12f } : gk::Vec3{ 0.08f, -0.04f, -0.22f };
        passed = Check(gk::SetModelPosition(first, firstPosition), "SetModelPosition(projection first)") && Check(gk::SetModelRotation(first, rotation), "SetModelRotation(projection first)") && Check(gk::SetModelScale(first, scale), "SetModelScale(projection first)");
        if (passed && projectionBlendMode)
        {
            passed = Check(gk::SetModelPosition(second, gk::Vec3{ 0.08f, -0.04f, 0.22f }), "SetModelPosition(projection second)") && Check(gk::SetModelRotation(second, rotation), "SetModelRotation(projection second)") && Check(gk::SetModelScale(second, scale), "SetModelScale(projection second)");
        }
    }

    if (passed)
    {
        passed = CheckEvents() && Check(gk::BeginFrame(), "BeginFrame") && Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)");
    }
    if (passed)
    {
        const uint32_t background = swatchMode ? gk::ColorRGB(0, 0, 255) : gk::ColorRGB(0, 0, 0);
        passed = Check(gk::DrawRect(0.0f, 0.0f, 640.0f, 480.0f, background, true), "DrawRect(background)");
    }
    if (passed && overlapMode && uiMode)
    {
        passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI models)");
    }
    if (passed && (swatchMode || triangleMode))
    {
        passed = Check(gk::DrawModel(first), "DrawModel(single)");
    }
    if (passed && overlayBarrierMode)
    {
        passed = Check(gk::DrawModel(first), "DrawModel(before Scene barrier)") && Check(gk::DrawRect(280.0f, 200.0f, 80.0f, 80.0f, gk::ColorRGB(0, 0, 255), true), "DrawRect(Scene barrier)");
    }
    if (passed && overlapMode)
    {
        // 近い赤を先に予約し、遠い青を後から追加してsortingを検査する。
        passed = Check(gk::DrawModel(first), "DrawModel(near red first)") && Check(gk::DrawModel(second), "DrawModel(far blue second)");
    }
    if (passed && maskMode)
    {
        // mask面を先に予約しても、透明部分を後ろのopaque面が埋める。
        passed = Check(gk::DrawModel(first), "DrawModel(mask front first)") && Check(gk::DrawModel(second), "DrawModel(opaque back second)");
    }
    if (passed && opaqueFrontMode)
    {
        // OPAQUE面はalpha値を無視し、後ろの半透明modelをdepthで隠す。
        passed = Check(gk::DrawModel(first), "DrawModel(opaque front)") && Check(gk::DrawModel(second), "DrawModel(blend back)");
    }
    if (passed && projectionOpaqueMode)
    {
        passed = Check(gk::DrawModel(first), "DrawModel(projection opaque)");
    }
    if (passed && projectionBlendMode)
    {
        // 予約順を逆にしたcaptureも同じback-to-front結果になる。
        const bool reverseSubmission = std::strcmp(mode, "projection-blend-reversed") == 0;
        passed = reverseSubmission ? (Check(gk::DrawModel(second), "DrawModel(projection far first)") && Check(gk::DrawModel(first), "DrawModel(projection near second)")) : (Check(gk::DrawModel(first), "DrawModel(projection near first)") && Check(gk::DrawModel(second), "DrawModel(projection far second)"));
    }
    if (passed)
    {
        passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI marker)") && Check(gk::DrawRect(520.0f, 32.0f, 64.0f, 48.0f, gk::ColorRGB(0, 255, 0), true), "DrawRect(UI marker)") && Check(gk::Present(), "Present");
    }
    if (first.IsValid())
    {
        passed = Check(gk::DeleteModel(first), "DeleteModel(first)") && passed;
    }
    if (second.IsValid())
    {
        passed = Check(gk::DeleteModel(second), "DeleteModel(second)") && passed;
    }
    return passed;
}

}

/**
 * BLEND、MASK、OPAQUEのscene/UI画像をGPU captureする。
 */
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: model_blend_capture_tests <fixture-dir> <mode>\n");
        return 2;
    }
    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }
    const bool passed = DrawFrame(argv[1], argv[2]);
    gk::Shutdown();
    return passed ? 0 : 1;
}
