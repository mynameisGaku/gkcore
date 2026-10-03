#include <gkcore.h>

int main() {
    if (gk::SetWindowSize(1280, 720) != 0 || gk::Init() != 0) return 1;

    gk::SetCamera(gk::Vec3{0.0f, 0.0f, -6.0f}, gk::Vec3{0.0f, 0.0f, 0.0f});
    bool failed = false;
    while (gk::ProcessEvents() && !gk::IsKeyDown(gk::Key::Escape)) {
        if (gk::BeginFrame() != 0 || gk::SetDrawLayer(gk::DrawLayer::Scene) != 0 ||
            gk::DrawTriangle3D(gk::Vec3{-1.0f, -1.0f, 0.0f},
                               gk::Vec3{1.0f, -1.0f, 0.0f},
                               gk::Vec3{0.0f, 1.0f, 0.0f},
                               gk::ColorRGB(80, 190, 250), true) != 0 ||
            gk::DrawRect(32.0f, 32.0f, 180.0f, 64.0f,
                         gk::ColorRGB(230, 130, 50), true) != 0 ||
            gk::SetDrawLayer(gk::DrawLayer::UI) != 0 ||
            gk::DrawRect(1120.0f, 24.0f, 120.0f, 44.0f,
                         gk::ColorRGB(60, 220, 130), true) != 0 ||
            gk::DrawString(32.0f, 660.0f, "Escape キーで終了", gk::ColorRGB(255, 255, 255)) != 0 ||
            gk::Present() != 0) {
            failed = true;
            break;
        }
    }

    gk::Shutdown();
    return failed ? 1 : 0;
}
