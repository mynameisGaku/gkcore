// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>
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
 * 描画前にイベントを処理し、終了要求と失敗を区別する。
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
 * アニメーションまたは独立した静的参照をGPU captureする。
 */
int main(int argc, char** argv)
{
    // 外部clipを読み込むmodeかを判定する。
    const bool externalMode = argc >= 3 && std::strcmp(argv[2], "external") == 0;
    const bool external = argc == 4 && externalMode;
    // 同じ位相のOBJ列からmodelを組み立てるmode。
    const bool objSequenceMode = argc >= 3 && std::strcmp(argv[2], "obj-sequence") == 0;
    const bool objSequence = argc == 4 && objSequenceMode;
    if (argc < 3 || argc > 4)
    {
        std::fprintf(stderr, "usage: model_animation_capture_tests <model-file> <translation|blend|skin-morph|ik|snapshot|sequence-stress|obj-sequence|obj-reference|fbx-...|static|external> [second-file]\n");
        return 2;
    }
    // 今回描画する契約名。
    const char* mode = argv[2];
    // 静的参照はposeを変更せず、期待位置だけを設定する。
    const bool fbxReference = std::strcmp(mode, "fbx-node-reference") == 0 || std::strcmp(mode, "fbx-skin-reference") == 0 || std::strcmp(mode, "fbx-morph-reference") == 0 || std::strcmp(mode, "fbx-rest-morph-reference") == 0;
    const bool fbxRest = std::strcmp(mode, "fbx-node-rest") == 0 || std::strcmp(mode, "fbx-skin-rest") == 0 || std::strcmp(mode, "fbx-morph-rest") == 0;
    const bool isStatic = std::strncmp(mode, "static", 6) == 0 || std::strcmp(mode, "snapshot-reference") == 0 || std::strcmp(mode, "snapshot-rest") == 0 || std::strcmp(mode, "obj-reference") == 0 || fbxReference || fbxRest;
    const bool knownMode = std::strcmp(mode, "translation") == 0 || std::strcmp(mode, "blend") == 0 || std::strcmp(mode, "skin-morph") == 0 || std::strcmp(mode, "ik") == 0 || std::strcmp(mode, "snapshot") == 0 || std::strcmp(mode, "snapshot-reference") == 0 || std::strcmp(mode, "snapshot-rest") == 0 || std::strcmp(mode, "sequence-stress") == 0 || std::strcmp(mode, "rest") == 0 || std::strcmp(mode, "rest-morph") == 0 || std::strcmp(mode, "external") == 0 || std::strcmp(mode, "obj-sequence") == 0 || std::strcmp(mode, "obj-reference") == 0 || std::strcmp(mode, "fbx-node") == 0 || std::strcmp(mode, "fbx-node-reference") == 0 || std::strcmp(mode, "fbx-node-rest") == 0 || std::strcmp(mode, "fbx-skin") == 0 || std::strcmp(mode, "fbx-skin-reference") == 0 || std::strcmp(mode, "fbx-skin-rest") == 0 || std::strcmp(mode, "fbx-morph") == 0 || std::strcmp(mode, "fbx-morph-reference") == 0 || std::strcmp(mode, "fbx-morph-rest") == 0 || std::strcmp(mode, "fbx-rest-morph") == 0 || std::strcmp(mode, "fbx-rest-morph-reference") == 0 || std::strcmp(mode, "static") == 0 || std::strcmp(mode, "static-x025") == 0 || std::strcmp(mode, "static-blend") == 0;
    if (!knownMode || (externalMode && !external) || (objSequenceMode && !objSequence) || ((external || objSequence) && argc != 4) || (!external && !objSequence && argc != 3))
    {
        std::fprintf(stderr, "unexpected mode or animation path: %s\n", mode);
        return 2;
    }

    if (!Check(gk::SetWindowSize(640, 480), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }
    // OBJ sequenceは2つの同じtopologyを持つfileから構築する。
    const char* sequencePaths[2] = { argv[1], argc == 4 ? argv[3] : argv[1] };
    // 描画対象。
    const gk::ModelHandle model = objSequence ? gk::LoadModelSequence(sequencePaths, 2, 1.0f) : gk::LoadModel(argv[1]);
    // API失敗を集約する状態。
    bool passed = model.IsValid();
    if (!passed)
    {
        std::fprintf(stderr, "LoadModel: %s\n", gk::GetLastErrorMessage());
    }
    // 適用後に解放し、model側がsourceを保持することを検査するhandle。
    gk::ModelAnimationHandle externalAnimation{};
    if (passed && external)
    {
        externalAnimation = gk::LoadModelAnimation(argv[3]);
        passed = externalAnimation.IsValid() && Check(gk::ApplyModelAnimation(model, externalAnimation, 0, false), "ApplyModelAnimation") && Check(gk::SetModelAnimationTime(model, 1.0), "SetModelAnimationTime(external)");
        if (!passed)
        {
            std::fprintf(stderr, "LoadModelAnimation: %s\n", gk::GetLastErrorMessage());
        }
        if (externalAnimation.IsValid())
        {
            passed = Check(gk::DeleteModelAnimation(externalAnimation), "DeleteModelAnimation before DrawModel") && passed;
        }
        externalAnimation = {};
    }
    const bool sequenceStress = std::strcmp(mode, "sequence-stress") == 0;
    if (passed && !isStatic && !external && (std::strcmp(mode, "translation") == 0 || std::strcmp(mode, "skin-morph") == 0 || std::strcmp(mode, "snapshot") == 0 || sequenceStress || objSequence))
    {
        // Sequenceはfps=1の2frame、GLB clipはduration=2秒。
        const double sampleTime = objSequence ? 0.5 : (sequenceStress ? 0.0 : 1.0);
        passed = Check(gk::PlayModelAnimation(model, 0, false), "PlayModelAnimation") && Check(gk::SetModelAnimationTime(model, sampleTime), "SetModelAnimationTime");
    }
    if (passed && !isStatic && !external && std::strcmp(mode, "blend") == 0)
    {
        passed = Check(gk::PlayModelAnimation(model, 0, false), "PlayModelAnimation(primary)") && Check(gk::SetModelAnimationBlend(model, 1, 0.5f), "SetModelAnimationBlend") && Check(gk::SetModelAnimationTime(model, 1.0, 0), "SetModelAnimationTime(primary)") && Check(gk::SetModelAnimationTime(model, 1.0, 1), "SetModelAnimationTime(secondary)");
    }
    if (passed && !isStatic && !external && std::strcmp(mode, "ik") == 0)
    {
        // 3関節IK fixtureのボーン番号。
        const int32_t root = gk::FindModelBone(model, "Root");
        const int32_t middle = gk::FindModelBone(model, "Middle");
        const int32_t end = gk::FindModelBone(model, "End");
        passed = root >= 0 && middle >= 0 && end >= 0 && Check(gk::SetModelTwoBoneIk(model, static_cast<uint32_t>(root), static_cast<uint32_t>(middle), static_cast<uint32_t>(end), gk::Vec3{ 1.0f, 1.0f, 0.0f }, gk::Vec3{ 0.0f, 1.0f, 0.0f }), "SetModelTwoBoneIk");
    }
    // FBX fixtureはmeshを小さく作ってあるため、指定倍率で拡大する。
    const bool fbxMode = std::strncmp(mode, "fbx-", 4) == 0;
    if (passed && fbxMode && !isStatic && std::strcmp(mode, "fbx-rest-morph") != 0)
    {
        passed = Check(gk::PlayModelAnimation(model, 0, false), "PlayModelAnimation(FBX)") && Check(gk::SetModelAnimationTime(model, 0.5), "SetModelAnimationTime(FBX)");
    }

    // fixtureを正面から見るcameraとtarget。
    const gk::Vec3 cameraPosition{ 1.0f, 0.5f, -5.0f };
    const gk::Vec3 cameraTarget{ 1.0f, 0.5f, 0.0f };
    passed = passed && Check(gk::SetCamera(cameraPosition, cameraTarget), "SetCamera") && Check(gk::SetAmbientLight(1.0f), "SetAmbientLight") && Check(gk::SetDirectionalLight(gk::Vec3{ 0.0f, -1.0f, 0.0f }, 0.0f), "SetDirectionalLight") && Check(gk::SetBloomEnabled(false), "SetBloomEnabled") && Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity") && Check(gk::SetToneMappingEnabled(false), "SetToneMappingEnabled") && Check(gk::SetFxaaEnabled(false), "SetFxaaEnabled") && Check(gk::SetExposure(1.0f), "SetExposure") && Check(gk::SetSaturation(1.0f), "SetSaturation") && Check(gk::SetContrast(1.0f), "SetContrast") && Check(gk::SetPostEffectShader({}), "SetPostEffectShader(disabled)");
    if (passed && sequenceStress)
    {
        // 同じmodelで132回描画し、各DrawModel後に次姿勢へ進めてもsnapshotを保つ。
        for (uint32_t frame = 0; passed && frame < 132; ++frame)
        {
            const double drawTime = frame % 2 == 0 ? 0.0 : 1.0;
            const double nextTime = frame % 2 == 0 ? 1.0 : 0.0;
            passed = CheckEvents() && Check(gk::SetModelAnimationTime(model, drawTime), "SetModelAnimationTime(stress draw)") && Check(gk::BeginFrame(), "BeginFrame(stress)") && Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)") && Check(gk::DrawRect(0.0f, 0.0f, 640.0f, 480.0f, gk::ColorRGB(40, 80, 120), true), "DrawRect(stress background)") && Check(gk::DrawModel(model), "DrawModel(stress snapshot)") && Check(gk::SetModelAnimationTime(model, nextTime), "SetModelAnimationTime(after snapshot)") && Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI)") && Check(gk::DrawRect(520.0f, 32.0f, 64.0f, 48.0f, gk::ColorRGB(0, 255, 0), true), "DrawRect(stress marker)") && Check(gk::Present(), "Present(stress)");
        }
    }
    if (passed && !sequenceStress)
    {
        passed = CheckEvents() && Check(gk::BeginFrame(), "BeginFrame") && Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)") && Check(gk::DrawRect(0.0f, 0.0f, 640.0f, 480.0f, gk::ColorRGB(40, 80, 120), true), "DrawRect(background)");
    }
    // 同じframeに異なるposeを2度記録してsnapshot保持を確かめるmode。
    const bool snapshotMode = std::strcmp(mode, "snapshot") == 0 || std::strcmp(mode, "snapshot-reference") == 0 || std::strcmp(mode, "snapshot-rest") == 0;
    if (passed && !sequenceStress && snapshotMode)
    {
        // 最初のanimated poseはclip移動を含め、参照位置と一致させる。
        const float firstPosition = std::strcmp(mode, "snapshot-rest") == 0 ? -0.45f : (isStatic ? -0.2f : -0.45f);
        passed = Check(gk::SetModelPosition(model, gk::Vec3{ firstPosition, 0.0f, 0.0f }), "SetModelPosition(snapshot first)") && Check(gk::DrawModel(model), "DrawModel(snapshot first)");
        if (passed && !isStatic)
        {
            passed = Check(gk::SetModelAnimationTime(model, 0.0), "SetModelAnimationTime(after snapshot)");
        }
        passed = passed && Check(gk::SetModelPosition(model, gk::Vec3{ 0.45f, 0.0f, 0.0f }), "SetModelPosition(snapshot second)") && Check(gk::DrawModel(model), "DrawModel(snapshot second)");
    }
    else if (passed && !sequenceStress)
    {
        // 参照画像で姿勢変化を置き換えるworld位置。
        gk::Vec3 position{ 0.0f, 0.0f, 0.0f };
        gk::Vec3 modelScale{ 1.0f, 1.0f, 1.0f };
        if (std::strcmp(mode, "static-x025") == 0)
        {
            position.x = 0.25f;
        }
        else if (std::strcmp(mode, "static-blend") == 0)
        {
            position = gk::Vec3{ 0.125f, 0.125f, 0.0f };
        }
        else if (std::strcmp(mode, "obj-reference") == 0)
        {
            position.x = 0.25f;
        }
        if (fbxMode)
        {
            modelScale = gk::Vec3{ 40.0f, 40.0f, 40.0f };
            position = std::strcmp(mode, "fbx-skin") == 0 || std::strcmp(mode, "fbx-skin-reference") == 0 || std::strcmp(mode, "fbx-skin-rest") == 0 ? gk::Vec3{ -0.2f, -0.2f, 0.0f } : gk::Vec3{ -5.6f, -1.8f, -2.0f };
            if (fbxReference)
            {
                const bool morphReference = std::strcmp(mode, "fbx-morph-reference") == 0 || std::strcmp(mode, "fbx-rest-morph-reference") == 0;
                position.y += morphReference ? 0.2f : 0.8f;
            }
        }
        passed = Check(gk::SetModelPosition(model, position), "SetModelPosition") && Check(gk::SetModelScale(model, modelScale), "SetModelScale") && Check(gk::DrawModel(model), "DrawModel");
    }
    if (passed && !sequenceStress)
    {
        passed = Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI)") && Check(gk::DrawRect(520.0f, 32.0f, 64.0f, 48.0f, gk::ColorRGB(0, 255, 0), true), "DrawRect(UI marker)") && Check(gk::Present(), "Present");
    }
    if (model.IsValid())
    {
        passed = Check(gk::DeleteModel(model), "DeleteModel after Present") && passed;
    }
    gk::Shutdown();
    return passed ? 0 : 1;
}
