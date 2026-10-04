#include <gkcore.h>
#include <cstdio>
#include <cstring>
#include <math.h>

/**
 * APIの失敗理由を出力し、画像検査用の描画を中断する。
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
 * gkcore_model_lightingと同じglbと照明設定で一枚描画する。
 */
int RunModelCapture(const char* modelPath, bool rotatedLight)
{
    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    // 照明サンプルで使う左右2材質のモデル。
    const gk::ModelHandle model = gk::LoadModel(modelPath);
    // 後続の設定と描画を続けられるか。
    bool passed = model.IsValid();
    if (!passed)
    {
        std::fprintf(stderr, "LoadModel: %s\n", gk::GetLastErrorMessage());
    }
    passed = passed && Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -5.4f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera");
    passed = passed && Check(gk::SetModelPosition(model, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetModelPosition");
    passed = passed && Check(gk::SetModelScale(model, gk::Vec3{ 0.82f, 0.82f, 0.82f }), "SetModelScale");
    passed = passed && Check(gk::SetAmbientLight(0.2f), "SetAmbientLight");
    // サンプル標準角度から、比較用modeだけ90度回す。
    const float lightAngle = 0.7853982f + (rotatedLight ? 1.5707963f : 0.0f);
    // サンプルと同じ傾きと強度で使う照明方向。
    const gk::Vec3 lightDirection{ cosf(lightAngle), -0.7f, sinf(lightAngle) };
    passed = passed && Check(gk::SetDirectionalLight(lightDirection, 3.0f), "SetDirectionalLight");
    passed = passed && Check(gk::SetModelRotation(model, gk::Vec3{ 0.0f, 0.25f, 0.0f }), "SetModelRotation");
    if (passed)
    {
        passed = Check(gk::BeginFrame(), "BeginFrame") && Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "Scene") && Check(gk::DrawModel(model), "DrawModel") && Check(gk::Present(), "Present");
    }

    if (model.IsValid())
    {
        passed = Check(gk::DeleteModel(model), "DeleteModel") && passed;
    }
    gk::Shutdown();
    return passed ? 0 : 1;
}

/**
 * 指定した効果で色・画像・文字・3Dを描き、開発用のGPU画像取得を実行する。
 */
int main(int argc, char** argv)
{
    if (argc != 4)
    {
        return 2;
    }
    // 照明サンプルのglbを使う2方向の画像検査。
    if (std::strcmp(argv[1], "model") == 0 || std::strcmp(argv[1], "model_rotated") == 0)
    {
        return RunModelCapture(argv[2], std::strcmp(argv[1], "model_rotated") == 0);
    }
    // 効果と画像はテスト側から指定し、作業フォルダーに依存させない。
    const bool tint = std::strcmp(argv[1], "tint") == 0;
    const bool fxaa = std::strcmp(argv[1], "direct") != 0;
    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }
    // 単色画像で画像用shaderの読み出し先も検査する。
    const gk::ImageHandle image = gk::LoadImage(argv[2]);
    // Sceneだけに適用する色変更用shader。
    gk::ShaderHandle shader{};
    if (tint)
    {
        shader = gk::LoadPixelShader(argv[3]);
    }
    // 途中の失敗は終了コードにも反映する。
    bool passed = image.IsValid() && (!tint || shader.IsValid());
    passed = passed && Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -5.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera");
    passed = passed && Check(gk::SetFxaaEnabled(fxaa), "SetFxaaEnabled");
    if (tint)
    {
        passed = passed && Check(gk::SetShaderFloat4(shader, 0, gk::Float4{ 0.2f, 1.0f, 1.0f, 1.0f }), "SetShaderFloat4");
        passed = passed && Check(gk::SetPostEffectShader(shader), "SetPostEffectShader");
    }
    // 両方のフレーム用bufferを繰り返し使い、更新と終了まで通す。
    for (int frame = 0; passed && frame < 6; ++frame)
    {
        passed = Check(gk::BeginFrame(), "BeginFrame") && Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "Scene") && Check(gk::DrawRect(32.0f, 32.0f, 128.0f, 96.0f, gk::ColorRGB(255, 0, 0), true), "Scene rectangle") && Check(gk::DrawTriangle3D(gk::Vec3{ -1.0f, -0.9f, 0.0f }, gk::Vec3{ 1.0f, -0.9f, 0.0f }, gk::Vec3{ 0.0f, 1.0f, 0.0f }, gk::ColorRGB(0, 0, 255), true), "Triangle") && Check(gk::DrawImage(image, 480.0f, 150.0f, false), "Scene image") && Check(gk::SetDrawLayer(gk::DrawLayer::UI), "UI") && Check(gk::DrawRect(480.0f, 32.0f, 96.0f, 64.0f, gk::ColorRGB(0, 255, 0), true), "UI rectangle") && Check(gk::DrawString(32.0f, 410.0f, "描画テスト", gk::ColorRGB(255, 255, 255)), "UI text") && Check(gk::Present(), "Present");
    }
    if (image)
    {
        gk::DeleteImage(image);
    }
    if (shader)
    {
        gk::SetPostEffectShader({});
        gk::DeleteShader(shader);
    }
    gk::Shutdown();
    return passed ? 0 : 1;
}
