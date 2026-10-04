#include <gkcore.h>

/**
 * Releases a custom post-effect safely after it is no longer selected.
 */
void Cleanup(gk::ShaderHandle shader) {
    gk::SetPostEffectShader({});
    if (shader) gk::DeleteShader(shader);
    gk::Shutdown();
}

/**
 * Draws a 2D/3D scene, applies the custom post effect, then draws the UI.
 */
int main() {
    if (gk::SetWindowSize(1280, 720) != 0 || gk::Init() != 0) {
        gk::Shutdown();
        return 1;
    }

    const gk::ShaderHandle postEffect = gk::LoadPixelShader("post_effect_tint.frag");
    if (!postEffect) {
        Cleanup(postEffect);
        return 1;
    }

    gk::SetCamera(gk::Vec3{0.0f, 0.0f, -6.0f}, gk::Vec3{0.0f, 0.0f, 0.0f});
    bool failed = false;
    bool heldSpace = false;
    bool effectEnabled = true;
    while (gk::ProcessEvents() && !gk::IsKeyDown(gk::Key::Escape)) {
        const bool spaceDown = gk::IsKeyDown(gk::Key::Space);
        if (spaceDown && !heldSpace) effectEnabled = !effectEnabled;
        heldSpace = spaceDown;
        if (gk::SetPostEffectShader(effectEnabled ? postEffect : gk::ShaderHandle{}) != 0 ||
            gk::SetShaderFloat4(postEffect, 0, gk::Float4{0.8f, 1.0f, 1.0f, 1.0f}) != 0 ||
            gk::BeginFrame() != 0) {
            failed = true;
            break;
        }
        const bool commandsSucceeded =
            gk::SetDrawLayer(gk::DrawLayer::Scene) == 0 &&
            gk::DrawTriangle3D(gk::Vec3{-1.0f, -1.0f, 0.0f},
                               gk::Vec3{1.0f, -1.0f, 0.0f},
                               gk::Vec3{0.0f, 1.0f, 0.0f},
                               gk::ColorRGB(80, 190, 250), true) == 0 &&
            gk::DrawRect(32.0f, 32.0f, 180.0f, 64.0f,
                         gk::ColorRGB(230, 130, 50), true) == 0 &&
            gk::SetDrawLayer(gk::DrawLayer::UI) == 0 &&
            gk::DrawRect(640.0f, 24.0f, 120.0f, 44.0f,
                         gk::ColorRGB(60, 220, 130), true) == 0 &&
            gk::DrawString(32.0f, 500.0f,
                           effectEnabled ? "Space: ポスト効果 ON / Escape: 終了"
                                         : "Space: ポスト効果 OFF / Escape: 終了",
                           gk::ColorRGB(255, 255, 255)) == 0;
        const bool presented = gk::Present() == 0;
        if (!commandsSucceeded || !presented) {
            failed = true;
            break;
        }
    }

    Cleanup(postEffect);
    return failed ? 1 : 0;
}
