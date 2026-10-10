// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>
#include "examples/support/FHumanoidMapOptions.h"
#include "examples/support/ModelMappingReport.h"
#include "examples/support/FModelArmIkPreview.h"
#include "examples/support/FModelMotionView.h"
#include "examples/support/ModelSecondaryMotionPreview.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{

/**
 * API失敗を操作名と診断付きで出力する。
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
 * event処理の終了要求と失敗を診断で区別する。
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
        fprintf(stderr, "ProcessEvents: %s\n", diagnostic);
    }
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
 * 数値引数を有限のdoubleとして解析する。
 */
bool ParseDouble(const char* text, double& value)
{
    if (!text || !text[0])
    {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = strtod(text, &end);
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
 * UTF-8材質設定を順に適用し、画像handleを直後に解放する。
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
 * 自動判定または手動設定された人型役割の数を返す。
 */
uint32_t CountModelRoles(gk::ModelHandle model)
{
    // 役割が割り当てられたモデル骨の合計。
    uint32_t count = 0;
    // 役割を確認するモデル骨の番号。
    for (uint32_t bone = 0; bone < gk::GetModelBoneCount(model); ++bone)
    {
        if (gk::GetModelBoneRole(model, bone) != gk::EHumanoidBone::None)
        {
            ++count;
        }
    }
    return count;
}

/**
 * 外部motionで自動判定された人型役割の数を返す。
 */
uint32_t CountAnimationRoles(gk::ModelAnimationHandle animation)
{
    // 役割が割り当てられたmotion骨の合計。
    uint32_t count = 0;
    // 役割を確認するmotion骨の番号。
    for (uint32_t bone = 0; bone < gk::GetAnimationBoneCount(animation); ++bone)
    {
        if (gk::GetAnimationBoneRole(animation, bone) != gk::EHumanoidBone::None)
        {
            ++count;
        }
    }
    return count;
}

/**
 * 適用先と外部motionで実際に対応した骨の数を表示する。
 */
bool ReportAnimationMapping(gk::ModelHandle model, uint32_t slot, const char* label)
{
    // 適用先とmotionの間で解決した骨の数。
    uint32_t count = 0;
    // 対応先を照会するモデル骨の番号。
    for (uint32_t bone = 0; bone < gk::GetModelBoneCount(model); ++bone)
    {
        if (gk::GetModelAnimationSourceBone(model, bone, slot) >= 0)
        {
            ++count;
        }
    }
    gk::examples::FModelMappingReport report{};
    if (!gk::examples::BuildModelMappingReport(model, slot, label, report))
    {
        fprintf(stderr, "GetModelAnimationMappingInfo(%s): %s\n", label, gk::GetLastErrorMessage());
        return false;
    }
    gk::examples::PrintModelMappingReport(label, CountModelRoles(model), count, report);
    return true;
}

/**
 * 左右それぞれ4つの脚roleが指定motionの同じ役割へ結び付いたことを確認する。
 */
bool RequireHumanoidLegMappings(gk::ModelHandle model, gk::ModelAnimationHandle animation, uint32_t slot, const char* label)
{
    // 必須とする左右の脚役割。
    const gk::EHumanoidBone roles[] = { gk::EHumanoidBone::LeftUpperLeg, gk::EHumanoidBone::LeftLowerLeg, gk::EHumanoidBone::LeftFoot, gk::EHumanoidBone::LeftToes, gk::EHumanoidBone::RightUpperLeg, gk::EHumanoidBone::RightLowerLeg, gk::EHumanoidBone::RightFoot, gk::EHumanoidBone::RightToes };
    bool passed = true;
    for (uint32_t roleIndex = 0; roleIndex < sizeof(roles) / sizeof(roles[0]); ++roleIndex)
    {
        // 適用先とmotionで指定役割を持つ骨番号。
        int32_t targetBone = -1;
        int32_t sourceBone = -1;
        for (uint32_t bone = 0; bone < gk::GetModelBoneCount(model); ++bone)
            if (gk::GetModelBoneRole(model, bone) == roles[roleIndex])
            {
                targetBone = static_cast<int32_t>(bone);
                break;
            }
        if (targetBone >= 0)
        {
            sourceBone = gk::GetModelAnimationSourceBone(model, static_cast<uint32_t>(targetBone), slot);
        }
        const bool sourceIndexValid = sourceBone >= 0 && static_cast<uint32_t>(sourceBone) < gk::GetAnimationBoneCount(animation);
        const char* targetName = targetBone >= 0 ? gk::GetModelBoneName(model, static_cast<uint32_t>(targetBone)) : nullptr;
        const char* sourceName = sourceIndexValid ? gk::GetAnimationBoneName(animation, static_cast<uint32_t>(sourceBone)) : nullptr;
        const bool mapped = targetBone >= 0 && sourceIndexValid && gk::GetAnimationBoneRole(animation, static_cast<uint32_t>(sourceBone)) == roles[roleIndex];
        printf("animation-leg-map %s: role=%u target=%s[%d] source=%s[%d] sourceRole=%u result=%s\n", label, static_cast<unsigned int>(roles[roleIndex]), targetName ? targetName : "<missing>", targetBone, sourceName ? sourceName : "<missing>", sourceBone, sourceIndexValid ? static_cast<unsigned int>(gk::GetAnimationBoneRole(animation, static_cast<uint32_t>(sourceBone))) : 0u, mapped ? "ok" : "missing");
        if (!mapped)
        {
            passed = false;
        }
    }
    return passed;
}

/**
 * 各frameの骨長、手首の目標到達、肩より内側へ折れない関節位置を確認する。
 */
bool VerifyArmIkPreview(gk::ModelHandle model, const gk::examples::FModelArmIkPreview& preview, const char* mode, uint32_t frameIndex)
{
    // 同じ現在姿勢からIK後の3関節位置をまとめて取得する。
    gk::Vec3 joints[3]{};
    if (!Check(gk::GetModelBonePositions(model, preview.bones, 3, joints), "GetModelBonePositions(IK result)"))
    {
        return false;
    }
    // 腕全体の長さに対する、到達誤差と骨長の許容値。
    const double tolerance = 0.0005 * preview.armLength + 1.0e-6;
    const double errorX = static_cast<double>(joints[2].x) - preview.target.x;
    const double errorY = static_cast<double>(joints[2].y) - preview.target.y;
    const double errorZ = static_cast<double>(joints[2].z) - preview.target.z;
    const double endpointError = sqrt(errorX * errorX + errorY * errorY + errorZ * errorZ);
    bool valid = isfinite(endpointError) && endpointError <= tolerance;
    double lengths[2]{};
    for (uint32_t segment = 0; segment < 2; ++segment)
    {
        const auto& first = joints[segment];
        const auto& second = joints[segment + 1];
        const auto& initialFirst = preview.joints[segment];
        const auto& initialSecond = preview.joints[segment + 1];
        const double x = static_cast<double>(second.x) - first.x;
        const double y = static_cast<double>(second.y) - first.y;
        const double z = static_cast<double>(second.z) - first.z;
        const double initialX = static_cast<double>(initialSecond.x) - initialFirst.x;
        const double initialY = static_cast<double>(initialSecond.y) - initialFirst.y;
        const double initialZ = static_cast<double>(initialSecond.z) - initialFirst.z;
        lengths[segment] = sqrt(x * x + y * y + z * z);
        const double initialLength = sqrt(initialX * initialX + initialY * initialY + initialZ * initialZ);
        valid = valid && isfinite(lengths[segment]) && fabs(lengths[segment] - initialLength) <= tolerance;
    }
    // 肩から体幹の外へ向かう3D方向へ、肘と手首がどれだけ進んだか調べる。
    const double outwardLength = sqrt(static_cast<double>(preview.outward.x) * preview.outward.x + static_cast<double>(preview.outward.y) * preview.outward.y + static_cast<double>(preview.outward.z) * preview.outward.z);
    if (!isfinite(outwardLength) || outwardLength <= 0.0)
    {
        return false;
    }
    const double elbowOutside = (static_cast<double>(joints[1].x - joints[0].x) * preview.outward.x + static_cast<double>(joints[1].y - joints[0].y) * preview.outward.y + static_cast<double>(joints[1].z - joints[0].z) * preview.outward.z) / outwardLength;
    const double handOutside = (static_cast<double>(joints[2].x - joints[0].x) * preview.outward.x + static_cast<double>(joints[2].y - joints[0].y) * preview.outward.y + static_cast<double>(joints[2].z - joints[0].z) * preview.outward.z) / outwardLength;
    const bool motionFollowing = strcmp(mode, "external-ik") == 0 || strcmp(mode, "external-blend-ik") == 0;
    if (motionFollowing)
    {
        // 動くsampleは元の腕振りを残し、手先の補正量を腕長の5%以内に保つ。
        const double motionX = static_cast<double>(joints[2].x) - preview.joints[2].x;
        const double motionY = static_cast<double>(joints[2].y) - preview.joints[2].y;
        const double motionZ = static_cast<double>(joints[2].z) - preview.joints[2].z;
        valid = valid && sqrt(motionX * motionX + motionY * motionY + motionZ * motionZ) <= 0.05 * preview.armLength + tolerance;
    }
    else
    {
        valid = valid && isfinite(elbowOutside) && elbowOutside >= -tolerance && isfinite(handOutside) && handOutside > 0.0;
    }
    // 2ボーンIKでは、肘が指定したpoleと同じ曲げ方向にあることも確認する。
    if (strcmp(mode, "ik") == 0 || strcmp(mode, "external-ik") == 0 || strcmp(mode, "external-blend-ik") == 0)
    {
        const double dx = preview.target.x - joints[0].x, dy = preview.target.y - joints[0].y, dz = preview.target.z - joints[0].z;
        const double ex = joints[1].x - joints[0].x, ey = joints[1].y - joints[0].y, ez = joints[1].z - joints[0].z;
        const double px = preview.pole.x - joints[0].x, py = preview.pole.y - joints[0].y, pz = preview.pole.z - joints[0].z;
        const double projection = (ex * dx + ey * dy + ez * dz) * (px * dx + py * dy + pz * dz) / (dx * dx + dy * dy + dz * dz);
        valid = valid && ex * px + ey * py + ez * pz - projection >= -tolerance * preview.armLength;
    }
    printf("ik-preview mode=%s frame=%u target=(%.6f,%.6f,%.6f) hand=(%.6f,%.6f,%.6f) armLength=%.6f endpointError=%.9g lengths=(%.6f,%.6f) elbowOutside=%.6f handOutside=%.6f tolerance=%.9g valid=%s\n", mode, frameIndex, preview.target.x, preview.target.y, preview.target.z, joints[2].x, joints[2].y, joints[2].z, preview.armLength, endpointError, lengths[0], lengths[1], elbowOutside, handOutside, tolerance, valid ? "true" : "false");
    return valid;
}

/**
 * mode名を確認し、描画回数を返す。
 */
bool ParseMode(const char* text, const char*& mode, uint32_t& frameCount)
{
    mode = text ? text : "static";
    if (strcmp(mode, "static") == 0 || strcmp(mode, "static-unlit") == 0 || strcmp(mode, "preview") == 0 || strcmp(mode, "front") == 0 || strcmp(mode, "ik") == 0 || strcmp(mode, "chain") == 0 || strcmp(mode, "blend") == 0)
    {
        frameCount = 1;
        return true;
    }
    if (strcmp(mode, "rotate") == 0 || strcmp(mode, "animate") == 0 || strcmp(mode, "external") == 0 || strcmp(mode, "external-blend") == 0)
    {
        frameCount = 6;
        return true;
    }
    if (strcmp(mode, "external-ik") == 0 || strcmp(mode, "external-blend-ik") == 0)
    {
        frameCount = 12;
        return true;
    }
    return false;
}

/**
 * 外部IK captureのframe数を2から120の範囲で読み取る。
 */
bool ReadAnimationFrameCount(uint32_t& frameCount)
{
    // capture枚数を上書きする環境変数。
    const char* text = getenv("GKCORE_TEST_ANIMATION_FRAMES");
    if (!text || !text[0])
    {
        return true;
    }
    errno = 0;
    // 数値変換後の読み取り位置。
    char* end = nullptr;
    // 範囲検査前の整数値。
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed < 2 || parsed > 120)
    {
        return false;
    }
    frameCount = static_cast<uint32_t>(parsed);
    return true;
}

/**
 * capture用の背景、モデル、UIを1frameへ記録する。
 */
bool DrawFrame(gk::ModelHandle model, uint32_t frameIndex, uint32_t frameCount, float scale, const float center[3], const char* mode, const gk::examples::FModelArmIkPreview* ikPreview, float viewRotation, uint32_t windowWidth, uint32_t windowHeight)
{
    // 実際の描画解像度に背景とUIを合わせ、4:3比率は保つ。
    const float frameWidth = static_cast<float>(windowWidth);
    const float frameHeight = static_cast<float>(windowHeight);
    const float angle = viewRotation + (strcmp(mode, "rotate") == 0 ? (static_cast<float>(frameIndex % 3) * 0.523598776f) : (strcmp(mode, "front") == 0 ? 3.141592654f : 0.0f));
    // 診断modeでは指向性ライトを切る。
    const float lightIntensity = strcmp(mode, "static-unlit") == 0 ? 0.0f : 2.5f;
    // 診断modeでは材質色を環境光だけで確認する。
    const float ambientIntensity = strcmp(mode, "static-unlit") == 0 ? 1.0f : 0.22f;
    const gk::Vec3 lightDirection{ sinf(0.35f), -0.45f, -cosf(0.35f) };
    const gk::Vec3 rotation{ 0.0f, angle, 0.0f };
    // model中心を軸に回転し、目標マーカーと同じ表示変換を使う。
    const gk::Vec3 position{ -scale * (center[0] * cosf(angle) + center[2] * sinf(angle)), -scale * center[1], -scale * (-center[0] * sinf(angle) + center[2] * cosf(angle)) };
    if (!Check(gk::SetModelPosition(model, position), "SetModelPosition") || !Check(gk::SetModelRotation(model, rotation), "SetModelRotation") || !Check(gk::SetAmbientLight(ambientIntensity), "SetAmbientLight") || !Check(gk::SetDirectionalLight(lightDirection, lightIntensity), "SetDirectionalLight") || !Check(gk::BeginFrame(), "BeginFrame") || !Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)") || !Check(gk::DrawRect(0.0f, 0.0f, frameWidth, frameHeight, gk::ColorRGB(28, 34, 48), true), "DrawRect(background)") || !Check(gk::DrawModel(model), "DrawModel"))
    {
        return false;
    }
    if (!Check(gk::SetDrawLayer(gk::DrawLayer::UI), "SetDrawLayer(UI)") || !Check(gk::DrawRect(frameWidth - 46.0f, 20.0f, 24.0f, 24.0f, gk::ColorRGB(0, 255, 0), true), "DrawRect(UI marker)"))
    {
        return false;
    }
    // 目標が手首の形状に隠れないよう、UI layerで位置を重ねて示す。
    if (ikPreview && !gk::examples::DrawModelArmIkTarget(*ikPreview, scale, { center[0], center[1], center[2] }, angle))
    {
        return false;
    }
    if (!Check(gk::DrawString(20.0f, 20.0f, "実モデル capture  /  Escapeで終了", gk::ColorRGB(255, 255, 255)), "DrawString(title)") || !Check(gk::DrawString(20.0f, 50.0f, mode, gk::ColorRGB(210, 220, 240)), "DrawString(mode)"))
    {
        return false;
    }
    // 最終frameの予約後、Present前に解放してsnapshotの寿命を検証する。
    if (frameIndex + 1 == frameCount && !Check(gk::DeleteModel(model), "DeleteModel before Present"))
    {
        return false;
    }
    return Check(gk::Present(), "Present");
}

}

/**
 * 実モデルを読み込み、static・回転・骨格機能のcaptureを作る。
 */
int main(int argc, char** argv)
{
    // 描画側の診断出力とframe記録が途中で混ざらないよう、その場で書き出す。
    setvbuf(stdout, nullptr, _IONBF, 0);
    // 人型bone対応表optionを先に除き、位置引数の並びを保つ。
    gk::examples::FHumanoidMapOptions humanoidMapOptions{};
    const char* humanoidMapError = nullptr;
    if (!gk::examples::ParseHumanoidMapOptions(argc, argv, humanoidMapOptions, &humanoidMapError))
    {
        fprintf(stderr, "invalid humanoid bone options: %s\n", humanoidMapError ? humanoidMapError : "unknown error");
        return 2;
    }
    // 大きく移動するmotionを画面内で見るための任意指定。
    const char* secondaryMotionPath = nullptr;
    if (!gk::examples::ParseSecondaryMotionOptions(argc, argv, secondaryMotionPath))
    {
        fprintf(stderr, "invalid or duplicate --secondary-motion option\n");
        return 2;
    }
    bool followMotion = false;
    if (!gk::examples::ParseMotionViewOptions(argc, argv, followMotion))
    {
        fprintf(stderr, "invalid or duplicate --follow-motion option\n");
        return 2;
    }
    if (argc < 6 || argc > 11)
    {
        fprintf(stderr, "usage: real_model_capture_tests <model-file> <scale> <centerX> <centerY> <centerZ> [static|static-unlit|preview|front|rotate|animate|ik|chain|blend|external|external-blend|external-ik|external-blend-ik] [motion paths] [--materials <config-file>] [--model-bones <path>] [--motion-bones <path>] [--blend-bones <path>] [--follow-motion] [--secondary-motion <config-file>]\n");
        return 2;
    }
    float scale = 0.0f;
    float center[3]{};
    const char* mode = nullptr;
    uint32_t frameCount = 0;
    // 指定されたcapture動作名。
    // 材質設定optionを引数末尾から取り出す。
    const bool hasMaterialConfig = argc >= 8 && strcmp(argv[argc - 2], "--materials") == 0;
    const char* materialConfigPath = hasMaterialConfig ? argv[argc - 1] : nullptr;
    const int effectiveArgc = argc - (hasMaterialConfig ? 2 : 0);
    // 材質設定optionより前に指定されたcapture動作名。
    const char* requestedMode = effectiveArgc >= 7 ? argv[6] : nullptr;
    // 外部motionを1本読み込むmodeかを示す。
    const bool externalIkMode = requestedMode && (strcmp(requestedMode, "external-ik") == 0 || strcmp(requestedMode, "external-blend-ik") == 0);
    // 同じmotion時刻のIK有無を比較するための検査専用指定。
    const char* disableIkText = getenv("GKCORE_TEST_DISABLE_IK");
    const bool applyExternalIk = externalIkMode && !(disableIkText && strcmp(disableIkText, "1") == 0);
    const bool externalMode = requestedMode && (strcmp(requestedMode, "external") == 0 || strcmp(requestedMode, "external-ik") == 0);
    // 外部motionを2本blendするmodeかを示す。
    const bool externalBlendMode = requestedMode && (strcmp(requestedMode, "external-blend") == 0 || strcmp(requestedMode, "external-blend-ik") == 0);
    // modeごとの引数個数が正しいかを示す。
    const bool modeArgumentsValid = (externalMode && !externalBlendMode && effectiveArgc == 8) || (externalBlendMode && effectiveArgc == 9) || (!externalMode && !externalBlendMode && effectiveArgc <= 7);
    if (!ParseFloat(argv[2], scale) || !(scale > 0.0f) || !ParseFloat(argv[3], center[0]) || !ParseFloat(argv[4], center[1]) || !ParseFloat(argv[5], center[2]) || !ParseMode(requestedMode, mode, frameCount) || (externalIkMode && !ReadAnimationFrameCount(frameCount)) || !modeArgumentsValid || (hasMaterialConfig && !materialConfigPath[0]) || !gk::examples::ValidateHumanoidMapOptions(humanoidMapOptions, externalMode || externalBlendMode, externalBlendMode, &humanoidMapError))
    {
        fprintf(stderr, "invalid scale, center, capture mode, or humanoid bone options: %s\n", humanoidMapError ? humanoidMapError : "invalid arguments");
        return 2;
    }
    // external IK captureだけに使う連続時刻の開始値とstep。
    double animationStartSeconds = 0.0;
    double animationStepSeconds = 0.0;
    const char* animationStartText = externalIkMode ? getenv("GKCORE_TEST_ANIMATION_START_SECONDS") : nullptr;
    const char* animationStepText = externalIkMode ? getenv("GKCORE_TEST_ANIMATION_STEP_SECONDS") : nullptr;
    const bool hasAnimationStep = animationStepText && animationStepText[0];
    if ((animationStartText && animationStartText[0] && (!ParseDouble(animationStartText, animationStartSeconds) || animationStartSeconds < 0.0)) || (hasAnimationStep && (!ParseDouble(animationStepText, animationStepSeconds) || animationStepSeconds <= 0.0)))
    {
        fprintf(stderr, "GKCORE_TEST_ANIMATION_START_SECONDS must be finite and non-negative; GKCORE_TEST_ANIMATION_STEP_SECONDS must be finite and positive\n");
        return 2;
    }
    // 斜め・側面の目視検査用に、model全体のY回転を指定できる。
    float viewRotation = 0.0f;
    const char* viewRotationText = getenv("GKCORE_TEST_VIEW_ROTATION");
    if (viewRotationText && !ParseFloat(viewRotationText, viewRotation))
    {
        fprintf(stderr, "GKCORE_TEST_VIEW_ROTATION must be finite\n");
        return 2;
    }
    // 袖や手先など、低解像度では判断しにくい部分だけ拡大captureする。
    const char* highResolutionText = getenv("GKCORE_TEST_CAPTURE_HIGH_RES");
    const bool highResolution = highResolutionText && strcmp(highResolutionText, "1") == 0;
    const uint32_t windowWidth = highResolution ? 2560u : strcmp(mode, "preview") == 0 ? 1280u : 640u;
    const uint32_t windowHeight = highResolution ? 1920u : strcmp(mode, "preview") == 0 ? 960u : 480u;
    if (!Check(gk::SetWindowSize(windowWidth, windowHeight), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }
    const gk::ModelHandle model = gk::LoadModel(argv[1]);
    bool passed = model.IsValid();
    if (!passed)
    {
        fprintf(stderr, "LoadModel: %s\n", gk::GetLastErrorMessage());
    }
    if (passed && hasMaterialConfig)
    {
        passed = ApplyMaterialConfig(model, materialConfigPath);
    }
    if (passed && humanoidMapOptions.modelPath)
    {
        passed = Check(gk::SetModelHumanoidBoneMap(model, humanoidMapOptions.modelPath), "SetModelHumanoidBoneMap");
    }
    // IKとchainも役割対応を使うため、手動表または自動判定を適用する。
    const bool humanoidIkMode = strcmp(mode, "ik") == 0 || strcmp(mode, "chain") == 0 || externalIkMode;
    if (passed && !humanoidMapOptions.modelPath && (externalMode || externalBlendMode || humanoidIkMode || followMotion))
    {
        passed = Check(gk::AutoMapModelHumanoidBones(model), "AutoMapModelHumanoidBones");
    }
    const gk::Vec3 cameraPosition{ 0.0f, 0.0f, 3.0f };
    const gk::Vec3 cameraTarget{ 0.0f, 0.0f, 0.0f };
    // 高解像度表示用のtone mappingとFXAAを有効にする。
    const bool preview = strcmp(mode, "preview") == 0;
    passed = passed && Check(gk::SetCamera(cameraPosition, cameraTarget), "SetCamera") && Check(gk::SetAmbientLight(0.22f), "SetAmbientLight") && Check(gk::SetBloomEnabled(false), "SetBloomEnabled") && Check(gk::SetBloomIntensity(0.0f), "SetBloomIntensity") && Check(gk::SetToneMappingEnabled(preview), "SetToneMappingEnabled") && Check(gk::SetFxaaEnabled(preview), "SetFxaaEnabled") && Check(gk::SetExposure(1.0f), "SetExposure") && Check(gk::SetSaturation(1.0f), "SetSaturation") && Check(gk::SetContrast(1.0f), "SetContrast");
    // 主motionのclip長を保持する。
    double externalDuration = 0.0;
    // blend用motionのclip長を保持する。
    double secondaryDuration = 0.0;
    // 環境変数で脚roleの対応を必須にする。
    const char* requireHumanoidLegs = getenv("GKCORE_TEST_REQUIRE_HUMANOID_LEGS");
    const bool requireHumanoidLegMappings = requireHumanoidLegs && strcmp(requireHumanoidLegs, "1") == 0;
    // 適用後に解放する主motionのhandle。
    gk::ModelAnimationHandle externalAnimation{};
    // 再生前の腰を基準に、表示中心だけを追従させる。
    gk::examples::FModelMotionView motionView;
    if (passed && followMotion && !motionView.Initialize(model, { center[0], center[1], center[2] }))
    {
        fprintf(stderr, "follow-motion requires a unique Hips role and valid model position\n");
        passed = false;
    }
    // 適用後に解放するblend motionのhandle。
    gk::ModelAnimationHandle secondaryAnimation{};
    if (passed && (externalMode || externalBlendMode))
    {
        externalAnimation = gk::LoadModelAnimation(argv[7]);
        if (!externalAnimation.IsValid() || gk::GetAnimationClipCount(externalAnimation) == 0)
        {
            fprintf(stderr, "LoadModelAnimation(primary): %s\n", gk::GetLastErrorMessage());
            passed = false;
        }
        else
        {
            externalDuration = gk::GetAnimationClipDuration(externalAnimation, 0);
            passed = isfinite(externalDuration) && externalDuration > 0.0 && (humanoidMapOptions.motionPath ? Check(gk::SetAnimationHumanoidBoneMap(externalAnimation, humanoidMapOptions.motionPath), "SetAnimationHumanoidBoneMap(primary)") : Check(gk::AutoMapAnimationHumanoidBones(externalAnimation), "AutoMapAnimationHumanoidBones(primary)"));
            if (passed)
            {
                printf("animation-map primary: sourceRoles=%u duration=%.6f\n", CountAnimationRoles(externalAnimation), externalDuration);
            }
            passed = passed && Check(gk::ApplyModelAnimation(model, externalAnimation, 0, true), "ApplyModelAnimation(primary)");
            if (passed)
            {
                passed = ReportAnimationMapping(model, 0, "primary");
            }
            if (passed && requireHumanoidLegMappings)
            {
                passed = RequireHumanoidLegMappings(model, externalAnimation, 0, "primary") && passed;
            }
        }
        if (passed && externalBlendMode)
        {
            secondaryAnimation = gk::LoadModelAnimation(argv[8]);
            if (!secondaryAnimation.IsValid() || gk::GetAnimationClipCount(secondaryAnimation) == 0)
            {
                fprintf(stderr, "LoadModelAnimation(secondary): %s\n", gk::GetLastErrorMessage());
                passed = false;
            }
            else
            {
                secondaryDuration = gk::GetAnimationClipDuration(secondaryAnimation, 0);
                passed = isfinite(secondaryDuration) && secondaryDuration > 0.0 && (humanoidMapOptions.blendPath ? Check(gk::SetAnimationHumanoidBoneMap(secondaryAnimation, humanoidMapOptions.blendPath), "SetAnimationHumanoidBoneMap(secondary)") : Check(gk::AutoMapAnimationHumanoidBones(secondaryAnimation), "AutoMapAnimationHumanoidBones(secondary)"));
                if (passed)
                {
                    printf("animation-map secondary: sourceRoles=%u duration=%.6f\n", CountAnimationRoles(secondaryAnimation), secondaryDuration);
                }
                passed = passed && Check(gk::SetModelAnimationBlend(model, secondaryAnimation, 0, 0.5f), "SetModelAnimationBlend(external)");
                if (passed)
                {
                    passed = ReportAnimationMapping(model, 1, "secondary");
                }
                if (passed && requireHumanoidLegMappings)
                {
                    passed = RequireHumanoidLegMappings(model, secondaryAnimation, 1, "secondary") && passed;
                }
            }
        }
        if (externalAnimation.IsValid() && !Check(gk::DeleteModelAnimation(externalAnimation), "DeleteModelAnimation(primary after apply)"))
        {
            passed = false;
        }
        if (secondaryAnimation.IsValid() && !Check(gk::DeleteModelAnimation(secondaryAnimation), "DeleteModelAnimation(secondary after blend)"))
        {
            passed = false;
        }
    }
    // 腕の長さと肩の側から、肩より外側のsample目標を作る。
    gk::examples::FModelArmIkPreview armIkPreview{};
    // このsampleは起動後に役割表を変更しないため、動くIKの6骨を一度だけ解決する。
    uint32_t animatedIkBones[6]{};
    if (passed && applyExternalIk && !gk::examples::ResolveAnimatedModelArmIkBones(model, animatedIkBones))
    {
        fprintf(stderr, "animated IK requires configured arm and torso roles\n");
        passed = false;
    }
    if (passed && humanoidIkMode && !externalIkMode)
    {
        if (!gk::examples::BuildModelArmIkPreview(model, armIkPreview))
        {
            fprintf(stderr, "IK preview requires arm and torso roles with valid joint positions: %s\n", gk::GetLastErrorMessage());
            passed = false;
        }
        else
        {
            const int result = strcmp(mode, "ik") == 0 ? gk::SetModelHumanoidTwoBoneIk(model, gk::EHumanoidBone::RightUpperArm, gk::EHumanoidBone::RightLowerArm, gk::EHumanoidBone::RightHand, armIkPreview.target, armIkPreview.pole) : gk::SetModelIkChain(model, armIkPreview.bones, 3, armIkPreview.target);
            if (!Check(result, "SetModelIk(preview)"))
            {
                passed = false;
            }
        }
    }
    if (passed && humanoidIkMode && !externalIkMode)
    {
        passed = VerifyArmIkPreview(model, armIkPreview, mode, 0);
    }
    if (passed && strcmp(mode, "blend") == 0)
    {
        if (gk::GetModelAnimationCount(model) < 2)
        {
            fprintf(stderr, "blend mode requires at least two embedded animation clips\n");
            passed = false;
        }
        else
        {
            passed = Check(gk::PlayModelAnimation(model, 0, true), "PlayModelAnimation") && Check(gk::SetModelAnimationBlend(model, 1, 0.5f), "SetModelAnimationBlend") && Check(gk::SetModelAnimationTime(model, 0.5, 0), "SetModelAnimationTime(primary)") && Check(gk::SetModelAnimationTime(model, 0.5, 1), "SetModelAnimationTime(secondary)") && passed;
        }
    }
    if (passed && strcmp(mode, "animate") == 0)
    {
        if (gk::GetModelAnimationCount(model) < 1)
        {
            fprintf(stderr, "animate mode requires at least one embedded animation clip\n");
            passed = false;
        }
        else
            passed = Check(gk::PlayModelAnimation(model, 0, true), "PlayModelAnimation");
    }
    if (passed && !Check(gk::SetModelScale(model, gk::Vec3{ scale, scale, scale }), "SetModelScale"))
    {
        passed = false;
    }
    if (passed && secondaryMotionPath)
    {
        passed = gk::examples::ApplySecondaryMotionPreview(model, secondaryMotionPath);
    }
    if (passed)
    {
        // frameごとに指定するanimation時刻を求める。
        for (uint32_t frame = 0; passed && frame < frameCount; ++frame)
        {
            const double sampleTime = static_cast<double>(frame % 4) * 0.5;
            // 対象modeの再生枠へ必要な時刻を設定する。
            const double externalTimeRatio = static_cast<double>(frame % 5) / 5.0;
            const double animatedIkSampleSeconds = externalIkMode ? (hasAnimationStep ? animationStartSeconds + static_cast<double>(frame) * animationStepSeconds : animationStartSeconds + externalDuration * static_cast<double>(frame) / static_cast<double>(frameCount - 1)) : 0.0;
            if (externalIkMode && !isfinite(animatedIkSampleSeconds))
            {
                fprintf(stderr, "external IK capture animation time exceeded the finite range at frame %u\n", frame);
                passed = false;
                break;
            }
            bool setAnimationTime = true;
            if (strcmp(mode, "animate") == 0)
            {
                setAnimationTime = Check(gk::SetModelAnimationTime(model, sampleTime), "SetModelAnimationTime");
            }
            else if (externalIkMode)
            {
                setAnimationTime = Check(gk::SetModelAnimationTime(model, animatedIkSampleSeconds, 0), "SetModelAnimationTime(primary)") && (!externalBlendMode || Check(gk::SetModelAnimationTime(model, animatedIkSampleSeconds, 1), "SetModelAnimationTime(secondary)"));
                if (setAnimationTime)
                {
                    printf("external-ik-time frame=%u input=%.9f primary=%.9f", frame, animatedIkSampleSeconds, gk::GetModelAnimationTime(model, 0));
                    if (externalBlendMode)
                    {
                        printf(" secondary=%.9f", gk::GetModelAnimationTime(model, 1));
                    }
                    else
                        printf(" secondary=unused");
                    printf("\n");
                }
            }
            else if (externalMode || externalBlendMode)
                setAnimationTime = Check(gk::SetModelAnimationTime(model, externalDuration * externalTimeRatio, 0), "SetModelAnimationTime(primary)") && (!externalBlendMode || Check(gk::SetModelAnimationTime(model, secondaryDuration * externalTimeRatio, 1), "SetModelAnimationTime(secondary)"));
            passed = CheckEvents() && setAnimationTime;
            if (passed && applyExternalIk)
            {
                // 前frameのIK結果を消し、現在のmotion姿勢から目標を作る。
                passed = Check(gk::ClearModelIk(model), "ClearModelIk(animated external IK)");
                gk::examples::FModelArmIkPreview currentArmIkPreview{};
                if (passed && !gk::examples::BuildAnimatedModelArmIkPreview(model, animatedIkBones, currentArmIkPreview))
                {
                    fprintf(stderr, "animated external IK preview requires arm and torso roles with valid joint positions: %s\n", gk::GetLastErrorMessage());
                    passed = false;
                }
                if (passed)
                {
                    passed = Check(gk::SetModelTwoBoneIk(model, currentArmIkPreview.bones[0], currentArmIkPreview.bones[1], currentArmIkPreview.bones[2], currentArmIkPreview.target, currentArmIkPreview.pole), "SetModelTwoBoneIk(animated external)");
                    if (passed)
                    {
                        armIkPreview = currentArmIkPreview;
                    }
                }
                if (passed)
                {
                    passed = VerifyArmIkPreview(model, armIkPreview, mode, frame);
                }
            }
            if (passed && secondaryMotionPath)
            {
                passed = Check(gk::UpdateModelSecondaryMotion(model, hasAnimationStep ? animationStepSeconds : 1.0 / 60.0), "UpdateModelSecondaryMotion");
            }
            gk::Vec3 displayCenter{ center[0], center[1], center[2] };
            if (passed && followMotion && !motionView.GetCenter(displayCenter))
            {
                fprintf(stderr, "follow-motion center evaluation failed\n");
                passed = false;
            }
            const float frameCenter[3] = { displayCenter.x, displayCenter.y, displayCenter.z };
            passed = passed && DrawFrame(model, frame, frameCount, scale, frameCenter, mode, (humanoidIkMode && (!externalIkMode || applyExternalIk)) ? &armIkPreview : nullptr, viewRotation, windowWidth, windowHeight);
        }
    }
    else if (model.IsValid())
    {
        Check(gk::DeleteModel(model), "DeleteModel");
    }
    gk::Shutdown();
    return passed ? 0 : 1;
}
