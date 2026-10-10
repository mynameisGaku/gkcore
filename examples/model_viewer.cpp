// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>
#include "examples/support/FHumanoidMapOptions.h"
#include "examples/support/ModelMappingReport.h"
#include "examples/support/FModelArmIkPreview.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Win32の同名macroを外し、公開API名をそのまま使う。
#ifdef LoadImage
#undef LoadImage
#endif

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
    fprintf(stderr, "%s: %s\n", operation, gk::GetLastErrorMessage());
    return false;
}

/**
 * 数値引数を有限のfloatとして解析する。
 */
bool ParseFloat(const char* text, float& value)
{
    if (!text || !text[0])
    {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const float parsed = strtof(text, &end);
    if (errno == ERANGE || end == text || *end != '\0' || !isfinite(parsed))
    {
        return false;
    }
    value = parsed;
    return true;
}

/**
 * 材質設定ファイルのalpha mode名をenumへ変換する。
 */
bool ParseAlphaMode(const char* text, gk::EModelAlphaMode& mode)
{
    if (strcmp(text, "0") == 0 || strcmp(text, "opaque") == 0 || strcmp(text, "OPAQUE") == 0)
    {
        mode = gk::EModelAlphaMode::Opaque;
    }
    else if (strcmp(text, "1") == 0 || strcmp(text, "mask") == 0 || strcmp(text, "MASK") == 0)
        mode = gk::EModelAlphaMode::Mask;
    else if (strcmp(text, "2") == 0 || strcmp(text, "blend") == 0 || strcmp(text, "BLEND") == 0)
        mode = gk::EModelAlphaMode::Blend;
    else
        return false;
    return true;
}

/**
 * 材質設定を順に適用し、画像handleを直後に解放する。
 */
bool ApplyMaterialConfig(gk::ModelHandle model, const char* configPath)
{
    FILE* file = fopen(configPath, "rb");
    if (!file)
    {
        fprintf(stderr, "cannot open material config: %s\n", configPath);
        return false;
    }
    bool passed = true;
    // 一行分の材質設定と画像pathを保持する。
    char line[16384]{};
    while (passed && fgets(line, sizeof(line), file))
    {
        // 固定長bufferに収まらない行は途中で切らず失敗にする。
        const size_t lineLength = strlen(line);
        if (lineLength == sizeof(line) - 1 && line[lineLength - 1] != '\n' && !feof(file))
        {
            fprintf(stderr, "material config row exceeds the supported length\n");
            passed = false;
            break;
        }
        // 空行とコメント行は設定として扱わない。
        char* cursor = line;
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        if (!*cursor || *cursor == '\r' || *cursor == '\n' || *cursor == '#')
        {
            continue;
        }
        // 設定対象と色係数を読み取る変数。
        unsigned int materialIndex = 0;
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;
        float cutoff = 0.0f;
        char modeName[32]{};
        // 数値設定の後に続く画像pathの開始位置。
        int pathOffset = -1;
        if (sscanf(cursor, "%u %f %f %f %f %31s %f %n", &materialIndex, &red, &green, &blue, &alpha, modeName, &cutoff, &pathOffset) != 7 || pathOffset < 0)
        {
            fprintf(stderr, "invalid material config row: %s", cursor);
            passed = false;
            break;
        }
        char* imagePath = cursor + pathOffset;
        while (*imagePath == ' ' || *imagePath == '\t')
        {
            ++imagePath;
        }
        size_t pathLength = strlen(imagePath);
        while (pathLength && (imagePath[pathLength - 1] == '\r' || imagePath[pathLength - 1] == '\n' || imagePath[pathLength - 1] == ' ' || imagePath[pathLength - 1] == '\t'))
        {
            imagePath[--pathLength] = '\0';
        }
        gk::FModelMaterialSettings settings{};
        settings.baseColorFactor[0] = red;
        settings.baseColorFactor[1] = green;
        settings.baseColorFactor[2] = blue;
        settings.baseColorFactor[3] = alpha;
        settings.alphaCutoff = cutoff;
        if (!pathLength || materialIndex >= gk::GetModelMaterialCount(model) || !ParseAlphaMode(modeName, settings.alphaMode))
        {
            fprintf(stderr, "invalid material index, mode, or texture path: %s\n", cursor);
            passed = false;
            break;
        }
        const gk::ImageHandle image = gk::LoadImage(imagePath);
        if (!image.IsValid())
        {
            fprintf(stderr, "LoadImage(%s): %s\n", imagePath, gk::GetLastErrorMessage());
            passed = false;
            break;
        }
        settings.baseColorImage = image;
        const int updated = gk::SetModelMaterial(model, materialIndex, settings);
        if (updated != 0)
        {
            fprintf(stderr, "SetModelMaterial(%u): %s\n", materialIndex, gk::GetLastErrorMessage());
            passed = false;
        }
        if (gk::DeleteImage(image) != 0)
        {
            fprintf(stderr, "DeleteImage(%s): %s\n", imagePath, gk::GetLastErrorMessage());
            passed = false;
        }
    }
    if (ferror(file))
    {
        fprintf(stderr, "material config read failed: %s\n", configPath);
        passed = false;
    }
    fclose(file);
    return passed;
}

/**
 * 起動時に指定された描画modeを検証する。
 */
bool IsKnownMode(const char* mode)
{
    return strcmp(mode, "static") == 0 || strcmp(mode, "front") == 0 || strcmp(mode, "rotate") == 0 || strcmp(mode, "animate") == 0 || strcmp(mode, "ik") == 0 || strcmp(mode, "chain") == 0 || strcmp(mode, "blend") == 0 || strcmp(mode, "external") == 0 || strcmp(mode, "external-blend") == 0 || strcmp(mode, "external-ik") == 0 || strcmp(mode, "external-blend-ik") == 0;
}

#if defined(GKCORE_MODEL_BENCHMARK)
/**
 * 環境変数のframe数を範囲検査して読み取る。
 */
bool ReadFrameCount(const char* name, uint32_t fallback, uint32_t maximum, bool allowZero, uint32_t& value)
{
    // 環境から指定されたframe数。
    const char* text = getenv(name);
    if (!text || !text[0])
    {
        value = fallback;
        return true;
    }
    errno = 0;
    // 数値変換後の読み取り位置。
    char* end = nullptr;
    // 範囲検査前の整数値。
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || (!allowZero && parsed == 0) || parsed > maximum)
    {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

/**
 * 計測値を昇順に並べる。
 */
int CompareMilliseconds(const void* left, const void* right)
{
    // 比較する二つのframe時間。
    const double a = *static_cast<const double*>(left);
    const double b = *static_cast<const double*>(right);
    return a < b ? -1 : (a > b ? 1 : 0);
}
#endif

}

/**
 * 任意の対応model fileを表示し、回転・骨格・後処理を試せるviewer。
 */
int main(int argc, char** argv)
{
    // 人型bone対応表optionを先に取り除き、残りの位置引数を従来どおり解釈する。
    gk::examples::FHumanoidMapOptions humanoidMapOptions{};
    const char* humanoidMapError = nullptr;
    if (!gk::examples::ParseHumanoidMapOptions(argc, argv, humanoidMapOptions, &humanoidMapError))
    {
        fprintf(stderr, "invalid humanoid bone options: %s\n", humanoidMapError ? humanoidMapError : "unknown error");
        return 2;
    }
    if (argc < 6 || argc > 11)
    {
        fprintf(stderr, "usage: model_viewer <model-file> <scale> <centerX> <centerY> <centerZ> [mode args] [--materials <config-file>] [--model-bones <path>] [--motion-bones <path>] [--blend-bones <path>]\n");
        return 2;
    }
    float scale = 0.0f;
    float center[3]{};
    if (!ParseFloat(argv[2], scale) || !(scale > 0.0f) || !ParseFloat(argv[3], center[0]) || !ParseFloat(argv[4], center[1]) || !ParseFloat(argv[5], center[2]))
    {
        fprintf(stderr, "scaleは正の有限値、centerは有限値を指定してください\n");
        return 2;
    }
    // 材質設定optionを引数末尾から取り出す。
    const bool hasMaterialConfig = argc >= 8 && strcmp(argv[argc - 2], "--materials") == 0;
    // 材質設定ファイルのpath。未指定ならnull。
    const char* materialConfigPath = hasMaterialConfig ? argv[argc - 1] : nullptr;
    // optionを除いた引数個数。
    const int effectiveArgc = argc - (hasMaterialConfig ? 2 : 0);
    // 指定された表示mode。
    const char* mode = effectiveArgc >= 7 ? argv[6] : "static";
    // 外部motionを1本読むmodeかを示す。
    const bool externalIkMode = strcmp(mode, "external-ik") == 0 || strcmp(mode, "external-blend-ik") == 0;
    const bool externalMode = strcmp(mode, "external") == 0 || strcmp(mode, "external-ik") == 0;
    // 外部motionを2本blendするmodeかを示す。
    const bool externalBlendMode = strcmp(mode, "external-blend") == 0 || strcmp(mode, "external-blend-ik") == 0;
    // modeに必要な引数が揃っているかを示す。
    const bool modeArgumentsValid = (externalMode && !externalBlendMode && effectiveArgc == 8) || (externalBlendMode && effectiveArgc == 9) || (!externalMode && !externalBlendMode && effectiveArgc <= 7);
    if (!IsKnownMode(mode) || !modeArgumentsValid || (hasMaterialConfig && !materialConfigPath[0]) || !gk::examples::ValidateHumanoidMapOptions(humanoidMapOptions, externalMode || externalBlendMode, externalBlendMode, &humanoidMapError))
    {
        fprintf(stderr, "invalid viewer mode or humanoid bone options: %s\n", humanoidMapError ? humanoidMapError : mode);
        return 2;
    }
    if (!Check(gk::SetWindowSize(1280, 720), "SetWindowSize") || !Check(gk::SetVSyncEnabled(false), "SetVSyncEnabled(false)") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }
    const gk::ModelHandle model = gk::LoadModel(argv[1]);
    bool failed = !model.IsValid();
    if (failed)
    {
        fprintf(stderr, "LoadModel: %s\n", gk::GetLastErrorMessage());
    }
    const gk::Vec3 cameraPosition{ 0.0f, 0.0f, 3.0f };
    const gk::Vec3 cameraTarget{ 0.0f, 0.0f, 0.0f };
    const gk::Vec3 modelPosition{ -scale * center[0], -scale * center[1], -scale * center[2] };
    if (!failed && (!Check(gk::SetCamera(cameraPosition, cameraTarget), "SetCamera") || !Check(gk::SetModelPosition(model, modelPosition), "SetModelPosition") || !Check(gk::SetModelScale(model, gk::Vec3{ scale, scale, scale }), "SetModelScale") || !Check(gk::SetAmbientLight(0.22f), "SetAmbientLight")))
    {
        failed = true;
    }
    if (!failed && hasMaterialConfig && !ApplyMaterialConfig(model, materialConfigPath))
    {
        failed = true;
    }
    if (!failed && humanoidMapOptions.modelPath && !Check(gk::SetModelHumanoidBoneMap(model, humanoidMapOptions.modelPath), "SetModelHumanoidBoneMap"))
    {
        failed = true;
    }
    // IKとchainも役割対応を使うため、手動表または自動判定を適用する。
    const bool humanoidIkMode = strcmp(mode, "ik") == 0 || strcmp(mode, "chain") == 0 || externalIkMode;
    if (!failed && !humanoidMapOptions.modelPath && (externalMode || externalBlendMode || humanoidIkMode) && !Check(gk::AutoMapModelHumanoidBones(model), "AutoMapModelHumanoidBones"))
    {
        failed = true;
    }

    // 適用後に解放する主motionのhandle。
    gk::ModelAnimationHandle externalAnimation{};
    // blend枠へ適用後に解放するmotion handle。
    gk::ModelAnimationHandle secondaryAnimation{};
    // 各枠へ登録した骨対応の表示情報を保持する。
    gk::examples::FModelMappingReport primaryMappingReport{};
    gk::examples::FModelMappingReport secondaryMappingReport{};
    if (!failed && (externalMode || externalBlendMode))
    {
        externalAnimation = gk::LoadModelAnimation(argv[7]);
        if (!externalAnimation.IsValid() || gk::GetAnimationClipCount(externalAnimation) == 0)
        {
            fprintf(stderr, "LoadModelAnimation(primary): %s\n", gk::GetLastErrorMessage());
            failed = true;
        }
        else if (!(humanoidMapOptions.motionPath ? Check(gk::SetAnimationHumanoidBoneMap(externalAnimation, humanoidMapOptions.motionPath), "SetAnimationHumanoidBoneMap(primary)") : Check(gk::AutoMapAnimationHumanoidBones(externalAnimation), "AutoMapAnimationHumanoidBones(primary)")) || !Check(gk::ApplyModelAnimation(model, externalAnimation, 0, true), "ApplyModelAnimation(primary)"))
            failed = true;
        if (!failed && externalBlendMode)
        {
            secondaryAnimation = gk::LoadModelAnimation(argv[8]);
            if (!secondaryAnimation.IsValid() || gk::GetAnimationClipCount(secondaryAnimation) == 0)
            {
                fprintf(stderr, "LoadModelAnimation(secondary): %s\n", gk::GetLastErrorMessage());
                failed = true;
            }
            else if (!(humanoidMapOptions.blendPath ? Check(gk::SetAnimationHumanoidBoneMap(secondaryAnimation, humanoidMapOptions.blendPath), "SetAnimationHumanoidBoneMap(secondary)") : Check(gk::AutoMapAnimationHumanoidBones(secondaryAnimation), "AutoMapAnimationHumanoidBones(secondary)")) || !Check(gk::SetModelAnimationBlend(model, secondaryAnimation, 0, 0.5f), "SetModelAnimationBlend(external)"))
                failed = true;
        }
        if (!failed && !gk::examples::BuildModelMappingReport(model, 0, u8"主モーション", primaryMappingReport))
        {
            fprintf(stderr, "GetModelAnimationMappingInfo(primary): %s\n", gk::GetLastErrorMessage());
            failed = true;
        }
        if (!failed && externalBlendMode && !gk::examples::BuildModelMappingReport(model, 1, u8"副モーション", secondaryMappingReport))
        {
            fprintf(stderr, "GetModelAnimationMappingInfo(secondary): %s\n", gk::GetLastErrorMessage());
            failed = true;
        }
        if (!failed)
        {
            printf("%s\n", primaryMappingReport.summary);
            if (primaryMappingReport.hasMissingRoles)
            {
                printf("%s\n", primaryMappingReport.missingRoles);
            }
            if (externalBlendMode)
            {
                printf("%s\n", secondaryMappingReport.summary);
                if (secondaryMappingReport.hasMissingRoles)
                {
                    printf("%s\n", secondaryMappingReport.missingRoles);
                }
            }
        }
        if (externalAnimation.IsValid() && !Check(gk::DeleteModelAnimation(externalAnimation), "DeleteModelAnimation(primary after apply)"))
        {
            failed = true;
        }
        if (secondaryAnimation.IsValid() && !Check(gk::DeleteModelAnimation(secondaryAnimation), "DeleteModelAnimation(secondary after blend)"))
        {
            failed = true;
        }
    }

    // 腕の長さと肩の側から、肩より外側のsample目標を作る。
    gk::examples::FModelArmIkPreview armIkPreview{};
    if (!failed && humanoidIkMode && !externalIkMode)
    {
        if (!gk::examples::BuildModelArmIkPreview(model, armIkPreview))
        {
            fprintf(stderr, "IK preview requires arm and torso roles with valid joint positions: %s\n", gk::GetLastErrorMessage());
            failed = true;
        }
        else
        {
            const int result = strcmp(mode, "ik") == 0 ? gk::SetModelHumanoidTwoBoneIk(model, gk::EHumanoidBone::RightUpperArm, gk::EHumanoidBone::RightLowerArm, gk::EHumanoidBone::RightHand, armIkPreview.target, armIkPreview.pole) : gk::SetModelIkChain(model, armIkPreview.bones, 3, armIkPreview.target);
            if (!Check(result, "SetModelIk(preview)"))
            {
                failed = true;
            }
        }
    }
    if (!failed && strcmp(mode, "blend") == 0)
    {
        if (gk::GetModelAnimationCount(model) < 2)
        {
            fprintf(stderr, u8"blend mode\u306b\u306f2\u500b\u4ee5\u4e0a\u306e\u57cb\u3081\u8fbc\u307fanimation clip\u304c\u5fc5\u8981\u3067\u3059\n");
            failed = true;
        }
        else if (!Check(gk::PlayModelAnimation(model, 0, true), "PlayModelAnimation") || !Check(gk::SetModelAnimationBlend(model, 1, 0.5f), "SetModelAnimationBlend"))
            failed = true;
    }
    if (!failed && strcmp(mode, "animate") == 0)
    {
        if (gk::GetModelAnimationCount(model) < 1)
        {
            fprintf(stderr, u8"animate mode\u306b\u306f\u57cb\u3081\u8fbc\u307fanimation clip\u304c\u5fc5\u8981\u3067\u3059\n");
            failed = true;
        }
        else if (!Check(gk::PlayModelAnimation(model, 0, true), "PlayModelAnimation"))
            failed = true;
    }

    // 左右入力で更新するモデルのY回転。
    float rotationY = strcmp(mode, "front") == 0 ? 3.141592654f : 0.0f;
    // Bloomの現在の有効状態。
    bool bloomEnabled = false;
    // motion再生を一時停止しているかを示す。
    bool motionPlaying = true;
    // 外部motion間blendを有効にしているかを示す。
    bool externalBlendEnabled = externalBlendMode;
#if defined(GKCORE_MODEL_BENCHMARK)
    // 環境変数から読んだ計測対象frame数。
    uint32_t benchmarkFrames = 0;
    // 平均と百分位から除外するwarmup frame数。
    uint32_t benchmarkWarmup = 0;
    if (!ReadFrameCount("GKCORE_BENCHMARK_FRAMES", 300, 4096, false, benchmarkFrames) || !ReadFrameCount("GKCORE_BENCHMARK_WARMUP", 30, 4096, true, benchmarkWarmup))
    {
        fprintf(stderr, "GKCORE_BENCHMARK_FRAMES must be from 1 through 4096 and GKCORE_BENCHMARK_WARMUP from 0 through 4096\n");
        failed = true;
    }
    // warmupと計測を合わせた有限loopの上限。
    const uint32_t benchmarkTotalFrames = benchmarkWarmup + benchmarkFrames;
    // 最大frame数分の計測値を固定配列へ記録する。
    double frameMilliseconds[4096]{};
    // DrawModel APIごとのCPU所要時間。
    double drawModelMilliseconds[4096]{};
    // Present APIごとのCPU所要時間。
    double presentMilliseconds[4096]{};
    // 計測区間全体のframe時間合計。
    double measuredFrameTotalMilliseconds = 0.0;
    // warmup後に最初の計測frameを開始したtick。
    LARGE_INTEGER benchmarkMeasurementStart{};
    // 直前に計測したframeの終了tick。
    LARGE_INTEGER benchmarkPreviousFrameEnd{};
    // warmupを含めて完了したframe数。
    uint32_t benchmarkFrame = 0;
    // QPCのtickを秒へ換算する周波数。
    LARGE_INTEGER timerFrequency{};
    // 前frameの開始tick。
    LARGE_INTEGER previousFrameCounter{};
    if (!QueryPerformanceFrequency(&timerFrequency) || timerFrequency.QuadPart <= 0 || !QueryPerformanceCounter(&previousFrameCounter))
    {
        fprintf(stderr, "QueryPerformanceCounter is unavailable\n");
        failed = true;
    }
#else
    // 表示に使う半秒ごとの実測FPS文字列。
    char fpsText[64] = "FPS: --";
    // QPCのtickを秒へ換算する周波数。
    LARGE_INTEGER timerFrequency{};
    // 前frameの開始tick。
    LARGE_INTEGER previousFrameCounter{};
    // 現在のFPS集計区間の開始tick。
    LARGE_INTEGER fpsWindowStart{};
    // 現在のFPS集計区間で提示したframe数。
    uint32_t fpsWindowFrames = 0;
    if (!QueryPerformanceFrequency(&timerFrequency) || timerFrequency.QuadPart <= 0 || !QueryPerformanceCounter(&previousFrameCounter) || !QueryPerformanceCounter(&fpsWindowStart))
    {
        fprintf(stderr, "QueryPerformanceCounter is unavailable\n");
        failed = true;
    }
#endif
    if (!failed && (!Check(gk::SetBloomEnabled(bloomEnabled), "SetBloomEnabled") || !Check(gk::SetBloomIntensity(0.35f), "SetBloomIntensity")))
    {
        failed = true;
    }
    while (!failed)
    {
#if defined(GKCORE_MODEL_BENCHMARK)
        if (benchmarkFrame >= benchmarkTotalFrames)
        {
            break;
        }
#endif
        // frame全体と実時間差の基準にする開始tick。
        LARGE_INTEGER frameStart{};
        if (!QueryPerformanceCounter(&frameStart))
        {
            fprintf(stderr, "QueryPerformanceCounter failed while measuring a frame\n");
            failed = true;
            break;
        }
        // 実時間に基づいて回転・animationを進める秒数。
        const double deltaSeconds = static_cast<double>(frameStart.QuadPart - previousFrameCounter.QuadPart) / static_cast<double>(timerFrequency.QuadPart);
        previousFrameCounter = frameStart;
#if defined(GKCORE_MODEL_BENCHMARK)
        // 可視計測のrotateは、入力操作に依存せず実際に回転を更新する。
        if (strcmp(mode, "rotate") == 0)
        {
            rotationY += static_cast<float>(2.1 * deltaSeconds);
        }
#endif
        if (!gk::ProcessEvents() || gk::WasKeyPressed(gk::Key::Escape))
        {
            break;
        }
        if (gk::IsKeyDown(gk::Key::ArrowLeft))
        {
            rotationY -= static_cast<float>(2.1 * deltaSeconds);
        }
        if (gk::IsKeyDown(gk::Key::ArrowRight))
        {
            rotationY += static_cast<float>(2.1 * deltaSeconds);
        }
        if (gk::WasKeyPressed(gk::Key::R))
        {
            rotationY = 0.0f;
        }
        if (gk::WasKeyPressed(gk::Key::Space))
        {
            bloomEnabled = !bloomEnabled;
            if (!Check(gk::SetBloomEnabled(bloomEnabled), "SetBloomEnabled"))
            {
                failed = true;
                break;
            }
        }
        if (gk::WasKeyPressed(gk::Key::M) && (externalMode || externalBlendMode || strcmp(mode, "animate") == 0 || strcmp(mode, "blend") == 0))
        {
            motionPlaying = !motionPlaying;
            // 2番目の再生枠が設定済みかを示す。
            const bool hasSecondary = externalBlendMode || strcmp(mode, "blend") == 0;
            if (!Check(gk::SetModelAnimationSpeed(model, motionPlaying ? 1.0 : 0.0, 0), "SetModelAnimationSpeed(primary)") || (hasSecondary && !Check(gk::SetModelAnimationSpeed(model, motionPlaying ? 1.0 : 0.0, 1), "SetModelAnimationSpeed(secondary)")))
            {
                failed = true;
                break;
            }
        }
        if (gk::WasKeyPressed(gk::Key::B) && externalBlendMode)
        {
            externalBlendEnabled = !externalBlendEnabled;
            if (!Check(gk::SetModelAnimationBlendWeight(model, externalBlendEnabled ? 0.5f : 0.0f), "SetModelAnimationBlendWeight"))
            {
                failed = true;
                break;
            }
        }
        if ((strcmp(mode, "blend") == 0 || strcmp(mode, "animate") == 0 || externalMode || externalBlendMode) && !Check(gk::UpdateModelAnimation(model, deltaSeconds), "UpdateModelAnimation"))
        {
            failed = true;
            break;
        }
        if (externalIkMode)
        {
            // 毎frameの目標は、直前のIK姿勢を消したアニメーション姿勢から作る。
            gk::examples::FModelArmIkPreview currentArmIkPreview{};
            if (!Check(gk::ClearModelIk(model), "ClearModelIk(animated external IK)"))
            {
                failed = true;
                break;
            }
            if (!gk::examples::BuildAnimatedModelArmIkPreview(model, currentArmIkPreview))
            {
                fprintf(stderr, "animated external IK preview requires arm and torso roles with valid joint positions: %s\n", gk::GetLastErrorMessage());
                failed = true;
                break;
            }
            if (!Check(gk::SetModelHumanoidTwoBoneIk(model, gk::EHumanoidBone::RightUpperArm, gk::EHumanoidBone::RightLowerArm, gk::EHumanoidBone::RightHand, currentArmIkPreview.target, currentArmIkPreview.pole), "SetModelHumanoidTwoBoneIk(animated external)"))
            {
                failed = true;
                break;
            }
            armIkPreview = currentArmIkPreview;
        }
        // 指定中心を画面中央に保ったまま、modelと目標を一緒に回転する。
        const float rotationCosine = cosf(rotationY);
        const float rotationSine = sinf(rotationY);
        const gk::Vec3 centeredPosition{ -scale * (center[0] * rotationCosine + center[2] * rotationSine), -scale * center[1], -scale * (-center[0] * rotationSine + center[2] * rotationCosine) };
        const gk::Vec3 rotation{ 0.0f, rotationY, 0.0f };
        const gk::Vec3 light{ sinf(0.35f), -0.45f, -cosf(0.35f) };
        if (!Check(gk::SetModelPosition(model, centeredPosition), "SetModelPosition") || !Check(gk::SetModelRotation(model, rotation), "SetModelRotation") || !Check(gk::SetDirectionalLight(light, 2.5f), "SetDirectionalLight") || !Check(gk::BeginFrame(), "BeginFrame") || !Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)") || !Check(gk::DrawRect(0.0f, 0.0f, 1280.0f, 720.0f, gk::ColorRGB(28, 34, 48), true), "DrawRect(background)"))
        {
            failed = true;
            break;
        }
        // DrawModel APIのCPU処理開始tick。
        LARGE_INTEGER drawStart{};
        // DrawModel APIのCPU処理終了tick。
        LARGE_INTEGER drawEnd{};
        if (!QueryPerformanceCounter(&drawStart) || !Check(gk::DrawModel(model), "DrawModel") || !QueryPerformanceCounter(&drawEnd) || !Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI)") || !Check(gk::DrawString(24.0f, 24.0f, "← / → 回転  M: Motion  B: Blend  Space: Bloom  R: reset  Escape: 終了", gk::ColorRGB(255, 255, 255)), "DrawString(controls)") || !Check(gk::DrawString(24.0f, 58.0f, mode, gk::ColorRGB(215, 225, 240)), "DrawString(mode)"))
        {
            failed = true;
            break;
        }
        if (humanoidIkMode)
        {
            if (!gk::examples::DrawModelArmIkTarget(armIkPreview, scale, { center[0], center[1], center[2] }, rotationY))
            {
                failed = true;
                break;
            }
        }
#if defined(GKCORE_MODEL_BENCHMARK)
        if (!Check(gk::DrawString(24.0f, 92.0f, "Benchmark", gk::ColorRGB(215, 225, 240)), "DrawString(benchmark)"))
        {
            failed = true;
            break;
        }
#else
        if (!Check(gk::DrawString(24.0f, 92.0f, fpsText, gk::ColorRGB(215, 225, 240)), "DrawString(FPS)"))
        {
            failed = true;
            break;
        }
#endif
#if !defined(GKCORE_MODEL_BENCHMARK)
        if (externalMode || externalBlendMode)
        {
            if (!Check(gk::DrawString(24.0f, 126.0f, primaryMappingReport.summary, primaryMappingReport.hasMissingRoles ? gk::ColorRGB(255, 170, 120) : gk::ColorRGB(215, 225, 240)), "DrawString(primary mapping)") || (externalBlendMode && !Check(gk::DrawString(24.0f, 160.0f, secondaryMappingReport.summary, secondaryMappingReport.hasMissingRoles ? gk::ColorRGB(255, 170, 120) : gk::ColorRGB(215, 225, 240)), "DrawString(secondary mapping)")))
            {
                failed = true;
                break;
            }
        }
#endif
        // Present APIのCPU処理開始tick。
        LARGE_INTEGER presentStart{};
        // Present APIのCPU処理終了tick。
        LARGE_INTEGER presentEnd{};
        if (!QueryPerformanceCounter(&presentStart) || !Check(gk::Present(), "Present") || !QueryPerformanceCounter(&presentEnd))
        {
            failed = true;
            break;
        }
#if !defined(GKCORE_MODEL_BENCHMARK)
        ++fpsWindowFrames;
        const double fpsWindowSeconds = static_cast<double>(presentEnd.QuadPart - fpsWindowStart.QuadPart) / static_cast<double>(timerFrequency.QuadPart);
        if (fpsWindowSeconds >= 0.5)
        {
            snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", static_cast<double>(fpsWindowFrames) / fpsWindowSeconds);
            fpsWindowFrames = 0;
            fpsWindowStart = presentEnd;
        }
#endif
#if defined(GKCORE_MODEL_BENCHMARK)
        // Present後に記録するframe終了tick。
        LARGE_INTEGER frameEnd{};
        if (!QueryPerformanceCounter(&frameEnd))
        {
            fprintf(stderr, "QueryPerformanceCounter failed after Present\n");
            failed = true;
            break;
        }
        if (benchmarkFrame >= benchmarkWarmup)
        {
            // warmup除外後の配列位置。
            const uint32_t sample = benchmarkFrame - benchmarkWarmup;
            if (sample == 0)
            {
                benchmarkMeasurementStart = frameStart;
                benchmarkPreviousFrameEnd = frameStart;
            }
            frameMilliseconds[sample] = static_cast<double>(frameEnd.QuadPart - benchmarkPreviousFrameEnd.QuadPart) * 1000.0 / static_cast<double>(timerFrequency.QuadPart);
            benchmarkPreviousFrameEnd = frameEnd;
            drawModelMilliseconds[sample] = static_cast<double>(drawEnd.QuadPart - drawStart.QuadPart) * 1000.0 / static_cast<double>(timerFrequency.QuadPart);
            presentMilliseconds[sample] = static_cast<double>(presentEnd.QuadPart - presentStart.QuadPart) * 1000.0 / static_cast<double>(timerFrequency.QuadPart);
            measuredFrameTotalMilliseconds = static_cast<double>(frameEnd.QuadPart - benchmarkMeasurementStart.QuadPart) * 1000.0 / static_cast<double>(timerFrequency.QuadPart);
        }
        ++benchmarkFrame;
#endif
    }
#if defined(GKCORE_MODEL_BENCHMARK)
    {
        // 実際に完了した計測frame数。途中終了では要求数より少ない。
        const uint32_t measuredFrames = benchmarkFrame > benchmarkWarmup ? (benchmarkFrame - benchmarkWarmup < benchmarkFrames ? benchmarkFrame - benchmarkWarmup : benchmarkFrames) : 0;
        // 各API所要時間とframe率の集計値。
        double averageDrawModelMs = 0.0;
        double averagePresentMs = 0.0;
        double averageFps = 0.0;
        double p95FrameMs = 0.0;
        if (measuredFrames)
        {
            for (uint32_t sample = 0; sample < measuredFrames; ++sample)
            {
                averageDrawModelMs += drawModelMilliseconds[sample];
                averagePresentMs += presentMilliseconds[sample];
            }
            averageDrawModelMs /= measuredFrames;
            averagePresentMs /= measuredFrames;
            averageFps = measuredFrameTotalMilliseconds > 0.0 ? static_cast<double>(measuredFrames) * 1000.0 / measuredFrameTotalMilliseconds : 0.0;
            qsort(frameMilliseconds, measuredFrames, sizeof(double), CompareMilliseconds);
            const uint32_t percentileIndex = static_cast<uint32_t>(ceil(static_cast<double>(measuredFrames) * 0.95)) - 1;
            p95FrameMs = frameMilliseconds[percentileIndex];
        }
        fprintf(stdout, "{\"framesRequested\":%u,\"warmupFrames\":%u,\"framesMeasured\":%u,\"completed\":%s,\"averageFps\":%.3f,\"p95FrameMs\":%.3f,\"averageDrawModelMs\":%.3f,\"averagePresentMs\":%.3f}\n", benchmarkFrames, benchmarkWarmup, measuredFrames, benchmarkFrame == benchmarkTotalFrames ? "true" : "false", averageFps, p95FrameMs, averageDrawModelMs, averagePresentMs);
    }
#endif
    if (model.IsValid() && !Check(gk::DeleteModel(model), "DeleteModel"))
    {
        failed = true;
    }
    gk::Shutdown();
    return failed ? 1 : 0;
}
