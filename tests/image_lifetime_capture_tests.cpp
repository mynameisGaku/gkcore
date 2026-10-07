#include <gkcore.h>
#include <cstdio>
#include <cstring>
#include <string>

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
 * 窓のイベントを処理し、終了要求や失敗を検出する。
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
 * 指定名の画像をfixture directoryから読み込む。
 */
gk::ImageHandle LoadFixture(const std::string& directory, const char* name)
{
    // fixture directory内の画像path。
    const std::string path = directory + "\\" + name;
    // 読み込んだ画像handle。
    const gk::ImageHandle image = gk::LoadImage(path.c_str());
    if (!image.IsValid())
    {
        std::fprintf(stderr, "LoadImage(%s): %s\n", path.c_str(), gk::GetLastErrorMessage());
    }
    return image;
}

/**
 * 検査用のScene背景を描き、UI効果との分離を調べる。
 */
bool DrawScene()
{
    // Scene描画APIがすべて成功したか。
    bool passed = Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)");
    passed = Check(gk::DrawRect(0.0f, 0.0f, 640.0f, 480.0f, gk::ColorRGB(40, 80, 120), true), "DrawRect scene background") && passed;
    passed = Check(gk::DrawTriangle3D(gk::Vec3{ -0.8f, -0.7f, 0.0f }, gk::Vec3{ 0.8f, -0.7f, 0.0f }, gk::Vec3{ 0.0f, 0.9f, 0.0f }, gk::ColorRGB(220, 180, 80), true), "DrawTriangle3D scene marker") && passed;
    return passed;
}

/**
 * UIの不透明背景、透明画像、回転画像を描く。
 */
bool DrawUi(const gk::ImageHandle* images)
{
    // UI描画APIがすべて成功したか。
    bool passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI)");
    passed = Check(gk::DrawRect(32.0f, 32.0f, 192.0f, 160.0f, gk::ColorRGB(0, 0, 255), true), "DrawRect blue UI backing") && passed;
    passed = Check(gk::DrawImage(images[0], 32.0f, 32.0f, true), "DrawImage alpha stripes") && passed;
    passed = Check(gk::DrawImage(images[0], 32.0f, 112.0f, false), "DrawImage alpha ignored") && passed;
    passed = Check(gk::DrawImageRotated(images[1], 160.0f, 64.0f, 64.0f, 0.0f, true), "DrawImageRotated half alpha") && passed;
    passed = Check(gk::DrawImage(images[2], 32.0f, 240.0f, false), "DrawImage quadrant reference") && passed;
    passed = Check(gk::DrawImageRotated(images[2], 160.0f, 248.0f, 2.0f, 1.57079632679f, false), "DrawImageRotated quadrants") && passed;
    return passed;
}

/**
 * Scene効果をbaselineまたは強い設定へそろえる。
 */
bool SetEffectMode(bool enabled)
{
    // 効果設定APIがすべて成功したか。
    bool passed = Check(gk::SetBloomEnabled(enabled), "SetBloomEnabled");
    passed = Check(gk::SetBloomIntensity(enabled ? 4.0f : 0.0f), "SetBloomIntensity") && passed;
    passed = Check(gk::SetFxaaEnabled(enabled), "SetFxaaEnabled") && passed;
    passed = Check(gk::SetToneMappingEnabled(enabled), "SetToneMappingEnabled") && passed;
    passed = Check(gk::SetExposure(enabled ? 4.0f : 1.0f), "SetExposure") && passed;
    passed = Check(gk::SetSaturation(enabled ? 0.0f : 1.0f), "SetSaturation") && passed;
    passed = Check(gk::SetContrast(enabled ? 2.0f : 1.0f), "SetContrast") && passed;
    return passed;
}

/**
 * 画像登録後にqueueへ追加し、handle削除後も描画を完了できるか確認する。
 */
bool DrawAndDeleteQueuedImages(const std::string& directory, bool effectEnabled, bool checkStaleHandle)
{
    // このフレームで描くalpha、半透明、四隅画像。
    gk::ImageHandle images[3] = { LoadFixture(directory, "alpha.png"), LoadFixture(directory, "half.png"), LoadFixture(directory, "corners.png") };
    // 3つのfixtureを読み込めたか。
    bool passed = images[0].IsValid() && images[1].IsValid() && images[2].IsValid();
    if (!passed)
    {
        std::fprintf(stderr, "one or more PNG fixtures could not be loaded\n");
    }
    passed = SetEffectMode(effectEnabled) && passed;
    passed = CheckEvents() && passed;
    passed = Check(gk::BeginFrame(), "BeginFrame") && passed;
    passed = DrawScene() && passed;
    passed = DrawUi(images) && passed;
    // queueへ登録した各画像handleをPresent前に解放する。
    for (const gk::ImageHandle image : images)
    {
        if (image.IsValid())
        {
            passed = Check(gk::DeleteImage(image), "DeleteImage after queue") && passed;
        }
    }
    if (checkStaleHandle && images[0].IsValid())
    {
        // 削除済みhandleでの描画結果。
        const int staleDraw = gk::DrawImage(images[0], 0.0f, 0.0f, true);
        // 無効handleを拒否したAPI診断。
        const char* diagnostic = gk::GetLastErrorMessage();
        if (staleDraw != -1 || !diagnostic || diagnostic[0] == '\0')
        {
            std::fprintf(stderr, "stale image handle was not rejected with a diagnostic\n");
            passed = false;
        }
        else
        {
            std::printf("expected stale-image rejection: %s\n", diagnostic);
        }
    }
    passed = Check(gk::Present(), "Present after image handle deletion") && passed;
    return passed;
}

/**
 * 132回画像を読み直してcache容量を越え、末尾2フレームだけを取得する。
 */
bool RunEviction(const std::string& directory)
{
    // すべてのcache検査frameが成功したか。
    bool passed = true;
    // cache容量を超える連続frameの番号。
    for (int frame = 0; passed && frame < 132; ++frame)
    {
        // 最後のframeだけScene効果を有効にする。
        const bool effectEnabled = frame == 131;
        passed = DrawAndDeleteQueuedImages(directory, effectEnabled, false);
    }
    return passed;
}

/**
 * baseline、Scene効果、cache入れ替えの画像を描画する。
 */
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: image_lifetime_capture_tests <baseline|fx|eviction> <fixture-directory>\n");
        return 2;
    }
    // 実行する画像検査mode。
    const std::string mode = argv[1];
    // PNG fixtureを読み込むdirectory。
    const std::string directory = argv[2];
    if (mode != "baseline" && mode != "fx" && mode != "eviction")
    {
        std::fprintf(stderr, "unknown image lifetime mode: %s\n", mode.c_str());
        return 2;
    }
    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }
    // camera設定と画像検査が成功したか。
    bool passed = Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -5.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera");
    if (passed)
    {
        passed = mode == "eviction" ? RunEviction(directory) : DrawAndDeleteQueuedImages(directory, mode == "fx", true);
    }
    gk::Shutdown();
    return passed ? 0 : 1;
}
