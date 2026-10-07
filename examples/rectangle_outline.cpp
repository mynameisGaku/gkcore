#include <gkcore.h>

/**
 * Shows rectangle outlines in the scene and UI layers while Space toggles bloom.
 */
int main()
{
    if (gk::SetWindowSize(800, 600) != 0 || gk::Init() != 0)
    {
        gk::Shutdown();
        return 1;
    }

    if (gk::SetBloomEnabled(true) != 0 || gk::SetBloomIntensity(1.0f) != 0 || gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -6.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }) != 0)
    {
        gk::Shutdown();
        return 1;
    }

    bool bloomEnabled = true;
    bool failed = false;
    while (gk::ProcessEvents())
    {
        if (gk::IsKeyDown(gk::Key::Escape) || gk::WasKeyPressed(gk::Key::Escape))
            break;

        if (gk::WasKeyPressed(gk::Key::Space))
        {
            bloomEnabled = !bloomEnabled;
            if (gk::SetBloomEnabled(bloomEnabled) != 0)
            {
                failed = true;
                break;
            }
        }

        if (gk::BeginFrame() != 0)
        {
            failed = true;
            break;
        }

        const bool commandsSucceeded = gk::SetDrawLayer(gk::DrawLayer::Scene) == 0 && gk::DrawTriangle3D(gk::Vec3{ -1.2f, -1.0f, 0.0f }, gk::Vec3{ 1.2f, -1.0f, 0.0f }, gk::Vec3{ 0.0f, 1.2f, 0.0f }, gk::ColorRGB(45, 125, 220), true) == 0 && gk::DrawRect(52.0f, 112.0f, 210.0f, 130.0f, gk::ColorRGB(190, 80, 35), true) == 0 && gk::DrawRectOutline(52.0f, 112.0f, 210.0f, 130.0f, gk::ColorRGB(255, 235, 160)) == 0 && gk::DrawRect(300.0f, 112.0f, 210.0f, 130.0f, gk::ColorRGB(80, 35, 190), false) == 0 && gk::DrawRectOutline(300.0f, 280.0f, 210.0f, 130.0f, gk::ColorRGB(255, 245, 220), 4.0f) == 0 && gk::SetDrawLayer(gk::DrawLayer::UI) == 0 && gk::DrawRectOutline(24.0f, 24.0f, 490.0f, 76.0f, gk::ColorRGB(245, 245, 245), 2.0f) == 0 && gk::DrawString(40.0f, 30.0f, "Space: Bloom 切替 / Escape: 終了", gk::ColorRGB(255, 255, 255)) == 0 && gk::DrawString(40.0f, 62.0f, bloomEnabled ? "Bloom: ON" : "Bloom: OFF", bloomEnabled ? gk::ColorRGB(255, 210, 80) : gk::ColorRGB(140, 220, 180)) == 0 && gk::DrawRectOutline(565.0f, 112.0f, 190.0f, 130.0f, gk::ColorRGB(70, 235, 145), 3.0f) == 0 && gk::DrawString(580.0f, 170.0f, "UI の輪郭", gk::ColorRGB(255, 255, 255)) == 0;
        const bool presented = gk::Present() == 0;
        if (!commandsSucceeded || !presented)
        {
            failed = true;
            break;
        }
    }

    gk::Shutdown();
    return failed ? 1 : 0;
}
