#include <stdio.h>
#include <math.h>
#include <gkcore.h>

/**
 * Private helpers for the model lighting example.
 */
namespace {
/**
 * Prints the latest gkcore diagnostic after a failed sample operation.
 */
void PrintError(const char* operation) {
    fprintf(stderr, "%s: %s\n", operation, gk::GetLastErrorMessage());
}
}

/**
 * Loads the sample model and demonstrates how moving directional light changes its shading.
 */
int main() {
    if (gk::SetWindowSize(960, 540) != 0 || gk::Init() != 0) {
        PrintError("gkcore の初期化");
        return 1;
    }

    const gk::ModelHandle model = gk::LoadModel("model_lighting.glb");
    if (!model.IsValid()) {
        PrintError("照明サンプルモデルの読み込み");
        gk::Shutdown();
        return 1;
    }

    if (gk::SetCamera(gk::Vec3{0.0f, 0.0f, -5.4f}, gk::Vec3{0.0f, 0.0f, 0.0f}) != 0 ||
        gk::SetModelPosition(model, gk::Vec3{0.0f, 0.0f, 0.0f}) != 0 ||
        gk::SetModelScale(model, gk::Vec3{0.82f, 0.82f, 0.82f}) != 0 ||
        gk::SetAmbientLight(0.2f) != 0) {
        PrintError("カメラまたはモデル設定");
        gk::DeleteModel(model);
        gk::Shutdown();
        return 1;
    }

    float lightAngle = 0.7853982f;
    bool previousSpace = false;
    bool failed = false;
    while (gk::ProcessEvents() && !gk::IsKeyDown(gk::Key::Escape)) {
        if (gk::IsKeyDown(gk::Key::ArrowLeft)) lightAngle -= 0.035f;
        if (gk::IsKeyDown(gk::Key::ArrowRight)) lightAngle += 0.035f;
        const bool space = gk::IsKeyDown(gk::Key::Space);
        if (space && !previousSpace) lightAngle += 1.5707963f;
        previousSpace = space;

        const float directionX = cosf(lightAngle);
        const float directionZ = sinf(lightAngle);
        const gk::Vec3 rotatedDirection = {directionX, -0.7f, directionZ};

        if (gk::SetDirectionalLight(rotatedDirection, 3.0f) != 0 ||
            gk::SetModelRotation(model, gk::Vec3{0.0f, 0.25f, 0.0f}) != 0) {
            PrintError("照明またはモデル設定");
            failed = true;
            break;
        }

        if (gk::BeginFrame() != 0) {
            PrintError("フレーム開始");
            failed = true;
            break;
        }

        bool frameFailed = false;
        if (gk::SetDrawLayer(gk::DrawLayer::Scene) != 0 || gk::DrawModel(model) != 0 ||
            gk::SetDrawLayer(gk::DrawLayer::UI) != 0 ||
            gk::DrawString(20.0f, 30.0f,
                           "左/右: 光の方向を回転  Space: 方向を切替  Escape: 終了",
                           gk::ColorRGB(255, 255, 255)) != 0 ||
            gk::DrawString(20.0f, 58.0f,
                           "左: 非金属・滑らか     右: 金属・やや粗い",
                           gk::ColorRGB(225, 230, 240)) != 0) {
            PrintError("描画命令");
            frameFailed = true;
        }
        if (gk::Present() != 0) {
            PrintError("フレーム表示");
            frameFailed = true;
        }
        if (frameFailed) {
            failed = true;
            break;
        }
    }

    if (gk::DeleteModel(model) != 0) {
        PrintError("モデル解放");
        failed = true;
    }
    gk::Shutdown();
    return failed ? 1 : 0;
}
