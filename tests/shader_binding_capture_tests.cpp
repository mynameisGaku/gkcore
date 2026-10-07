#include <gkcore.h>
#include <cstdio>
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
 * 指定slotへ定数を書き込み、失敗理由を記録する。
 */
bool SetConstant(gk::ShaderHandle shader, uint32_t slot, gk::Float4 value, const char* operation)
{
    return Check(gk::SetShaderFloat4(shader, slot, value), operation);
}

/**
 * slot 0と63を変えて、Scene用の単色矩形を登録する。
 */
bool DrawSceneColor(gk::ShaderHandle shader, float x, gk::Float4 color)
{
    // Scene矩形のAPIがすべて成功したか。
    bool passed = Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene color)");
    passed = Check(gk::SetPixelShader(shader), "SetPixelShader(Scene color)") && passed;
    passed = SetConstant(shader, 0, color, "SetShaderFloat4(Scene slot 0)") && passed;
    passed = SetConstant(shader, 63, gk::Float4{ 1.0f, 1.0f, 1.0f, 1.0f }, "SetShaderFloat4(Scene slot 63)") && passed;
    passed = Check(gk::DrawRect(x, 32.0f, 64.0f, 48.0f, gk::ColorRGB(255, 255, 255), true), "DrawRect(Scene color)") && passed;
    return passed;
}

/**
 * slot 0と63を変えて、UI用の単色矩形を登録する。
 */
bool DrawUiColor(gk::ShaderHandle shader, float x, gk::Float4 color)
{
    // UI矩形のAPIがすべて成功したか。
    bool passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI color)");
    passed = Check(gk::SetPixelShader(shader), "SetPixelShader(UI color)") && passed;
    passed = SetConstant(shader, 0, gk::Float4{ 1.0f, 1.0f, 1.0f, 1.0f }, "SetShaderFloat4(UI slot 0)") && passed;
    passed = SetConstant(shader, 63, color, "SetShaderFloat4(UI slot 63)") && passed;
    passed = Check(gk::DrawRect(x, 112.0f, 64.0f, 48.0f, gk::ColorRGB(255, 255, 255), true), "DrawRect(UI color)") && passed;
    return passed;
}

/**
 * 2つの定数を白にそろえ、指定画像の色を描く。
 */
bool DrawTextured(gk::ShaderHandle shader, gk::ImageHandle image, gk::DrawLayer layer, float x)
{
    // 画像描画のAPIがすべて成功したか。
    bool passed = Check(gk::SetDrawLayer(layer), "SetDrawLayer(image)");
    passed = Check(gk::SetPixelShader(shader), "SetPixelShader(image)") && passed;
    passed = SetConstant(shader, 0, gk::Float4{ 1.0f, 1.0f, 1.0f, 1.0f }, "SetShaderFloat4(image slot 0)") && passed;
    passed = SetConstant(shader, 63, gk::Float4{ 1.0f, 1.0f, 1.0f, 1.0f }, "SetShaderFloat4(image slot 63)") && passed;
    passed = Check(gk::DrawImage(image, x, 208.0f, false), "DrawImage(binding capture)") && passed;
    return passed;
}

/**
 * SceneとUIの色を変え、描画命令ごとに異なる定数を登録する。
 */
bool DrawFrame(gk::ShaderHandle shader, const gk::ImageHandle* images, int frame)
{
    // このフレームで描画命令の登録が成功したか。
    bool passed = Check(gk::BeginFrame(), "BeginFrame");
    // 共通の不透明色定数。
    const gk::Float4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
    if (passed && frame == 0)
    {
        passed = Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(unset slot)");
        passed = Check(gk::SetPixelShader(shader), "SetPixelShader(unset slot)") && passed;
        passed = SetConstant(shader, 0, white, "SetShaderFloat4(unset slot 0)") && passed;
        passed = Check(gk::DrawRect(224.0f, 32.0f, 64.0f, 48.0f, gk::ColorRGB(255, 255, 255), true), "DrawRect(unset slot 63)") && passed;
    }
    else if (passed)
    {
        passed = Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(zero slot)");
        passed = Check(gk::SetPixelShader(shader), "SetPixelShader(zero slot)") && passed;
        passed = SetConstant(shader, 0, white, "SetShaderFloat4(zero slot 0)") && passed;
        passed = SetConstant(shader, 63, gk::Float4{ 0.0f, 0.0f, 0.0f, 0.0f }, "SetShaderFloat4(zero slot 63)") && passed;
        passed = Check(gk::DrawRect(224.0f, 32.0f, 64.0f, 48.0f, gk::ColorRGB(255, 255, 255), true), "DrawRect(zero slot 63)") && passed;
    }

    // SceneとUIの描画で交互に使う色定数。
    const gk::Float4 red{ 1.0f, 0.0f, 0.0f, 1.0f };
    const gk::Float4 green{ 0.0f, 1.0f, 0.0f, 1.0f };
    const gk::Float4 blue{ 0.0f, 0.0f, 1.0f, 1.0f };
    const gk::Float4 yellow{ 1.0f, 1.0f, 0.0f, 1.0f };
    // 2色の配置を切り替える偶数フレーム判定。
    const bool evenFrame = frame % 2 == 0;
    passed = DrawSceneColor(shader, 32.0f, evenFrame ? red : green) && passed;
    passed = DrawSceneColor(shader, 128.0f, evenFrame ? green : red) && passed;
    passed = DrawUiColor(shader, 32.0f, evenFrame ? blue : yellow) && passed;
    passed = DrawUiColor(shader, 128.0f, evenFrame ? yellow : blue) && passed;
    passed = DrawTextured(shader, images[0], gk::DrawLayer::Scene, 32.0f) && passed;
    passed = DrawTextured(shader, images[1], gk::DrawLayer::Scene, 128.0f) && passed;
    passed = DrawTextured(shader, images[0], gk::DrawLayer::UI, 320.0f) && passed;
    passed = DrawTextured(shader, images[1], gk::DrawLayer::UI, 416.0f) && passed;

    // 白い画像の代替と、描画命令から渡す色を同時に確認する。
    passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(fallback)") && passed;
    passed = Check(gk::SetPixelShader(shader), "SetPixelShader(fallback)") && passed;
    passed = SetConstant(shader, 0, white, "SetShaderFloat4(fallback slot 0)") && passed;
    passed = SetConstant(shader, 63, white, "SetShaderFloat4(fallback slot 63)") && passed;
    passed = Check(gk::DrawRect(224.0f, 208.0f, 64.0f, 32.0f, gk::ColorRGB(0, 255, 255), true), "DrawRect(fallback white texture)") && passed;

    passed = SetConstant(shader, 0, gk::Float4{ 0.0f, 0.0f, 0.0f, 0.0f }, "SetShaderFloat4(zero slot 0 after queue)") && passed;
    passed = SetConstant(shader, 63, gk::Float4{ 0.0f, 0.0f, 0.0f, 0.0f }, "SetShaderFloat4(zero slot 63 after queue)") && passed;
    return passed;
}

/**
 * 6フレームで定数の保持、画像の切り替え、使用中のシェーダーの削除制約を確認する。
 */
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: shader_binding_capture_tests <compiled-shader> <fixture-directory>\n");
        return 2;
    }
    // コンパイル済みピクセルシェーダーのパス。
    const std::string shaderPath = argv[1];
    // 検査用の画像を読み込むフォルダー。
    const std::string fixtureDirectory = argv[2];
    // 2つの画像を読み込むパス。
    const std::string imageAPath = fixtureDirectory + "\\pattern_a.png";
    const std::string imageBPath = fixtureDirectory + "\\pattern_b.png";
    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    // 基準の効果設定とカメラ設定が成功したか。
    bool passed = Check(gk::SetBloomEnabled(false), "SetBloomEnabled") && Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity") && Check(gk::SetToneMappingEnabled(false), "SetToneMappingEnabled") && Check(gk::SetExposure(1.0f), "SetExposure") && Check(gk::SetSaturation(1.0f), "SetSaturation") && Check(gk::SetContrast(1.0f), "SetContrast") && Check(gk::SetFxaaEnabled(false), "SetFxaaEnabled") && Check(gk::SetPostEffectShader({}), "SetPostEffectShader disabled") && Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -5.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera");
    // 6フレームで使うシェーダーのhandle。
    gk::ShaderHandle shader = gk::LoadPixelShader(shaderPath.c_str());
    // SceneとUIで色を確認する画像のhandle。
    const gk::ImageHandle imageA = gk::LoadImage(imageAPath.c_str());
    const gk::ImageHandle imageB = gk::LoadImage(imageBPath.c_str());
    if (!shader.IsValid() || !imageA.IsValid() || !imageB.IsValid())
    {
        std::fprintf(stderr, "fixture load failed: %s\n", gk::GetLastErrorMessage());
        passed = false;
    }

    // 各描画で参照する2つの画像handle。
    const gk::ImageHandle images[2] = { imageA, imageB };
    if (passed)
    {
        // 登録時の定数の保持を確認するフレーム番号。
        for (int frame = 0; passed && frame < 6; ++frame)
        {
            passed = CheckEvents();
            if (passed)
            {
                passed = Check(gk::SetPixelShader(shader), "SetPixelShader(frame)");
            }
            if (passed)
            {
                passed = DrawFrame(shader, images, frame);
            }
            if (passed && frame == 0)
            {
                // 描画途中のフレームが使用中のシェーダーを削除した結果。
                const int deleteWhileUsed = gk::DeleteShader(shader);
                // 削除拒否のAPI診断。
                const char* diagnostic = gk::GetLastErrorMessage();
                if (deleteWhileUsed != -1 || !diagnostic || diagnostic[0] == '\0')
                {
                    std::fprintf(stderr, "DeleteShader while frame is open was not rejected with a diagnostic\n");
                    passed = false;
                }
                else
                {
                    std::printf("expected shader deletion rejection: %s\n", diagnostic);
                }
            }
            if (passed)
            {
                passed = Check(gk::Present(), "Present");
            }
        }
    }

    if (imageA.IsValid())
    {
        passed = Check(gk::DeleteImage(imageA), "DeleteImage(pattern_a)") && passed;
    }
    if (imageB.IsValid())
    {
        passed = Check(gk::DeleteImage(imageB), "DeleteImage(pattern_b)") && passed;
    }
    if (shader.IsValid() && passed)
    {
        passed = Check(gk::DeleteShader(shader), "DeleteShader after six Present calls");
        if (passed)
        {
            // 削除済みシェーダーのhandleを選択した結果。
            const int staleSelection = gk::SetPixelShader(shader);
            // 削除済みhandleの拒否を示す診断。
            const char* diagnostic = gk::GetLastErrorMessage();
            if (staleSelection != -1 || !diagnostic || diagnostic[0] == '\0')
            {
                std::fprintf(stderr, "SetPixelShader accepted a stale shader handle or omitted its diagnostic\n");
                passed = false;
            }
            else
            {
                std::printf("expected stale shader rejection: %s\n", diagnostic);
            }
        }
    }
    else if (shader.IsValid())
    {
        Check(gk::DeleteShader(shader), "DeleteShader cleanup");
    }
    gk::Shutdown();
    return passed ? 0 : 1;
}
