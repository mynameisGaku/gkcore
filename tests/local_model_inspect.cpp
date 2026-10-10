// SPDX-License-Identifier: NOASSERTION
#include "model/ModelLoader.h"
#include "model/animation/FbxAnimation.h"
#include "model/animation/GlbAnimation.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/ModelPose.h"
#include "resources/ResourceIO.h"
#include "foundation/Memory.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <vector>

namespace
{

/**
 * 任意のUTF-8文字列をJSON stringとして出力する。
 */
void PrintJsonString(const char* text)
{
    putchar('"');
    if (text)
    {
        for (const unsigned char* current = reinterpret_cast<const unsigned char*>(text); *current; ++current)
        {
            if (*current == '"' || *current == '\\')
            {
                putchar('\\');
                putchar(*current);
            }
            else if (*current == '\n')
                fputs("\\n", stdout);
            else if (*current == '\r')
                fputs("\\r", stdout);
            else if (*current == '\t')
                fputs("\\t", stdout);
            else if (*current < 0x20)
                printf("\\u%04x", static_cast<unsigned int>(*current));
            else
                putchar(*current);
        }
    }
    putchar('"');
}

/**
 * 有限な値だけをJSON数値として出力する。
 */
void PrintJsonNumber(float value)
{
    if (isfinite(value))
        printf("%.9g", static_cast<double>(value));
    else
        fputs("null", stdout);
}

/**
 * animation source形式を読みやすい固定名へ変換する。
 */
const char* FormatName(gk::model::EModelAnimationFormat format)
{
    if (format == gk::model::EModelAnimationFormat::Glb)
        return "glb";
    if (format == gk::model::EModelAnimationFormat::Fbx)
        return "fbx";
    return "obj-sequence";
}

/**
 * モデルpayloadを読み込み、bounds・材質・骨格・clipをJSONで報告する。
 */
bool Inspect(const char* path)
{
    gk::String error;
    gk::detail::ModelResource* model = gk::detail::LoadModelPayload(path, error);
    if (!model)
    {
        fputs("{\"error\":", stdout);
        PrintJsonString(error.CStr());
        fputs("}\n", stdout);
        return false;
    }

    float minimum[3] = { INFINITY, INFINITY, INFINITY };
    float maximum[3] = { -INFINITY, -INFINITY, -INFINITY };
    for (uint32_t vertex = 0; vertex < model->vertices.Count(); ++vertex)
    {
        const gk::detail::ModelVertex& value = model->vertices.At(vertex);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (value.position[axis] < minimum[axis])
                minimum[axis] = value.position[axis];
            if (value.position[axis] > maximum[axis])
                maximum[axis] = value.position[axis];
        }
    }

    gk::Array<float> worldMatrices;
    gk::model::animation::FModelPose restPose;
    const gk::model::AModelAnimationSource* source = model->animation ? model->animation->source : nullptr;
    const gk::model::animation::FModelSkeleton* skeleton = source ? &source->Skeleton() : nullptr;
    bool haveWorldPose = false;
    if (skeleton)
        haveWorldPose = gk::model::animation::InitializeModelPose(*skeleton, restPose, error) && gk::model::animation::EvaluateModelPose(*skeleton, restPose, worldMatrices, error);

    printf("{\"path\":");
    PrintJsonString(path);
    printf(",\"animationFormat\":");
    if (source)
        PrintJsonString(FormatName(source->Format()));
    else
        fputs("null", stdout);
    printf(",\"vertexCount\":%u,\"indexCount\":%u,\"primitiveCount\":%u,\"materialCount\":%u,\"textureCount\":%u,\"animationClipCount\":%u,\"bounds\":", model->vertices.Count(), model->indices.Count(), model->primitives.Count(), model->materials.Count(), model->textures.Count(), source ? source->ClipCount() : 0u);
    if (model->vertices.Count())
    {
        fputs("{\"min\":[", stdout);
        PrintJsonNumber(minimum[0]);
        putchar(',');
        PrintJsonNumber(minimum[1]);
        putchar(',');
        PrintJsonNumber(minimum[2]);
        fputs("],\"max\":[", stdout);
        PrintJsonNumber(maximum[0]);
        putchar(',');
        PrintJsonNumber(maximum[1]);
        putchar(',');
        PrintJsonNumber(maximum[2]);
        fputs("]}", stdout);
    }
    else
        fputs("null", stdout);

    fputs(",\"textures\":[", stdout);
    for (uint32_t texture = 0; texture < model->textures.Count(); ++texture)
    {
        if (texture)
            putchar(',');
        const gk::detail::ImageResource* image = model->textures.At(texture);
        if (!image)
            fputs("null", stdout);
        else
            printf("{\"width\":%u,\"height\":%u,\"rgbaBytes\":%u}", image->width, image->height, image->rgba.Count());
    }
    fputs("],\"materials\":[", stdout);
    for (uint32_t materialIndex = 0; materialIndex < model->materials.Count(); ++materialIndex)
    {
        if (materialIndex)
            putchar(',');
        const gk::detail::ModelMaterial& material = model->materials.At(materialIndex);
        printf("{\"baseColor\":[%.9g,%.9g,%.9g,%.9g],\"alphaMask\":%s,\"alphaBlend\":%s,\"alphaCutoff\":%.9g,\"baseTexture\":%d,\"metallicRoughnessTexture\":%d,\"normalTexture\":%d,\"emissiveTexture\":%d,\"occlusionTexture\":%d}", material.baseColorFactor[0], material.baseColorFactor[1], material.baseColorFactor[2], material.baseColorFactor[3], material.alphaMask ? "true" : "false", material.alphaBlend ? "true" : "false", material.alphaCutoff, material.baseColorTextureIndex, material.metallicRoughnessTextureIndex, material.normalTextureIndex, material.emissiveTextureIndex, material.occlusionTextureIndex);
    }
    fputs("],\"bones\":[", stdout);
    if (source && skeleton)
    {
        for (uint32_t bone = 0; bone < skeleton->parents.Count(); ++bone)
        {
            if (bone)
                putchar(',');
            const char* name = source->BoneName(bone);
            const int32_t parent = skeleton->parents.At(bone);
            printf("{\"index\":%u,\"name\":", bone);
            PrintJsonString(name);
            printf(",\"parent\":%d,\"writable\":%s,\"restLocal\":[", parent, source->BoneWritable(bone) ? "true" : "false");
            const gk::model::animation::FModelBoneTransform& local = skeleton->restLocalTransforms.At(bone);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (axis)
                    putchar(',');
                PrintJsonNumber(local.position[axis]);
            }
            fputs("],\"restScale\":[", stdout);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (axis)
                    putchar(',');
                PrintJsonNumber(local.scale[axis]);
            }
            fputs("],\"restWorld\":[", stdout);
            if (haveWorldPose && (bone + 1) * 16 <= worldMatrices.Count())
            {
                const float* matrix = worldMatrices.Data() + bone * 16;
                PrintJsonNumber(matrix[12]);
                putchar(',');
                PrintJsonNumber(matrix[13]);
                putchar(',');
                PrintJsonNumber(matrix[14]);
            }
            else
                fputs("null,null,null", stdout);
            fputs("]}", stdout);
        }
    }
    fputs("],\"morphs\":[", stdout);
    if (source && skeleton)
    {
        for (uint32_t morph = 0; morph < skeleton->restMorphWeights.Count(); ++morph)
        {
            if (morph)
                putchar(',');
            printf("{\"index\":%u,\"name\":", morph);
            PrintJsonString(source->MorphName(morph));
            fputs(",\"restWeight\":", stdout);
            PrintJsonNumber(skeleton->restMorphWeights.At(morph));
            putchar('}');
        }
    }
    fputs("],\"clips\":[", stdout);
    if (source)
    {
        for (uint32_t clip = 0; clip < source->ClipCount(); ++clip)
        {
            if (clip)
                putchar(',');
            printf("{\"index\":%u,\"name\":", clip);
            PrintJsonString(source->ClipName(clip));
            printf(",\"duration\":%.9g}", source->ClipDuration(clip));
        }
    }
    fputs("],\"restPoseEvaluated\":", stdout);
    fputs(haveWorldPose || !skeleton ? "true" : "false", stdout);
    if (skeleton && !haveWorldPose)
    {
        fputs(",\"poseError\":", stdout);
        PrintJsonString(error.CStr());
    }
    fputs("}\n", stdout);
    gk::Release(&model->reference);
    return haveWorldPose || !skeleton;
}

/**
 * geometryを持たない外部motionを読み、骨格とclipをJSONで報告する。
 */
bool InspectAnimation(const char* path)
{
    gk::String error;
    // 読み込んだ外部motionのbyte列。
    uint8_t* bytes = nullptr;
    // 読み込んだbyte列の長さ。
    uint32_t size = 0;
    if (!gk::detail::ReadResourceFile(path, 64u * 1024u * 1024u, bytes, size, error))
    {
        fputs("{\"error\":", stdout);
        PrintJsonString(error.CStr());
        fputs("}\n", stdout);
        return false;
    }
    // 拡張子ではなく先頭byteから読み込み形式を選ぶ。
    gk::model::FModelAnimationAsset* asset = size >= 4 && memcmp(bytes, "glTF", 4) == 0 ? gk::model::LoadGlbAnimation(bytes, size, error) : gk::model::LoadFbxAnimation(bytes, size, path, error);
    gk::Deallocate(bytes);
    if (!asset || !asset->source)
    {
        fputs("{\"error\":", stdout);
        PrintJsonString(error.CStr());
        fputs("}\n", stdout);
        if (asset)
            gk::Release(&asset->reference);
        return false;
    }

    // 形式に依存しない骨格とclipの参照。
    const gk::model::AModelAnimationSource& source = *asset->source;
    // 出力する共通rest骨格。
    const gk::model::animation::FModelSkeleton& skeleton = source.Skeleton();
    // rest姿勢の親子変換行列。
    gk::Array<float> worldMatrices;
    // rest位置とmorph係数から作る初期姿勢。
    gk::model::animation::FModelPose restPose;
    // 全骨のrest行列が正しく評価できたかを示す。
    const bool haveWorldPose = gk::model::animation::InitializeModelPose(skeleton, restPose, error) && gk::model::animation::EvaluateModelPose(skeleton, restPose, worldMatrices, error);
    printf("{\"path\":");
    PrintJsonString(path);
    printf(",\"animationFormat\":");
    PrintJsonString(FormatName(source.Format()));
    printf(",\"animationOnly\":true,\"boneCount\":%u,\"morphCount\":%u,\"animationClipCount\":%u,\"bones\":[", skeleton.parents.Count(), skeleton.restMorphWeights.Count(), source.ClipCount());
    // JSONへ書き出す骨の番号。
    for (uint32_t bone = 0; bone < skeleton.parents.Count(); ++bone)
    {
        if (bone)
            putchar(',');
        printf("{\"index\":%u,\"name\":", bone);
        PrintJsonString(source.BoneName(bone));
        printf(",\"parent\":%d,\"writable\":%s,\"restLocal\":[", skeleton.parents.At(bone), source.BoneWritable(bone) ? "true" : "false");
        // 親基準の初期位置。
        const gk::model::animation::FModelBoneTransform& local = skeleton.restLocalTransforms.At(bone);
        // 位置軸を順に書き出す。
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (axis)
                putchar(',');
            PrintJsonNumber(local.position[axis]);
        }
        fputs("],\"restScale\":[", stdout);
        // 親基準の休止scaleを3軸で書き出す。
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (axis)
                putchar(',');
            PrintJsonNumber(local.scale[axis]);
        }
        fputs("],\"restWorld\":[", stdout);
        if (haveWorldPose && (bone + 1) * 16 <= worldMatrices.Count())
        {
            // 対象骨の親子変換行列。
            const float* matrix = worldMatrices.Data() + bone * 16;
            PrintJsonNumber(matrix[12]);
            putchar(',');
            PrintJsonNumber(matrix[13]);
            putchar(',');
            PrintJsonNumber(matrix[14]);
        }
        else
            fputs("null,null,null", stdout);
        fputs("]}", stdout);
    }
    fputs("],\"morphs\":[", stdout);
    // JSONへ書き出すmorphの番号。
    for (uint32_t morph = 0; morph < skeleton.restMorphWeights.Count(); ++morph)
    {
        if (morph)
            putchar(',');
        printf("{\"index\":%u,\"name\":", morph);
        PrintJsonString(source.MorphName(morph));
        fputs(",\"restWeight\":", stdout);
        PrintJsonNumber(skeleton.restMorphWeights.At(morph));
        putchar('}');
    }
    fputs("],\"clips\":[", stdout);
    // JSONへ書き出すclipの番号。
    for (uint32_t clip = 0; clip < source.ClipCount(); ++clip)
    {
        if (clip)
            putchar(',');
        printf("{\"index\":%u,\"name\":", clip);
        PrintJsonString(source.ClipName(clip));
        printf(",\"duration\":%.9g}", source.ClipDuration(clip));
    }
    fputs("],\"restPoseEvaluated\":", stdout);
    fputs(haveWorldPose ? "true" : "false", stdout);
    if (!haveWorldPose)
    {
        fputs(",\"poseError\":", stdout);
        PrintJsonString(error.CStr());
    }
    fputs("}\n", stdout);
    gk::Release(&asset->reference);
    return haveWorldPose;
}

/**
 * 変形先に必要なgeometryと材質の配列を複製する。
 */
gk::detail::ModelResource* CloneDeformTarget(const gk::detail::ModelResource& source)
{
    // 元モデルの描画配列を持つ独立した変形先。
    gk::detail::ModelResource* target = gk::detail::CreateModelResource();
    if (!target)
    {
        return nullptr;
    }
    target->materials.Clear();
    if (!target->vertices.AppendRange(source.vertices.Data(), source.vertices.Count()) || !target->indices.AppendRange(source.indices.Data(), source.indices.Count()) || !target->primitives.AppendRange(source.primitives.Data(), source.primitives.Count()) || !target->materials.AppendRange(source.materials.Data(), source.materials.Count()))
    {
        gk::Release(&target->reference);
        return nullptr;
    }
    return target;
}

/**
 * 対象骨に小さな移動と回転を加え、同じsceneを連続評価するposeを作る。
 */
bool MakeBenchmarkPose(const gk::model::AModelAnimationSource& source, const gk::model::animation::FModelSkeleton& skeleton, const gk::model::animation::FModelPose& restPose, uint32_t frame, gk::model::animation::FModelPose& output, uint32_t& motionBone, gk::String& error)
{
    // rest姿勢を保ったまま配列領域を再利用する。
    if (!output.localTransforms.Reserve(restPose.localTransforms.Count()) || !output.morphWeights.Reserve(restPose.morphWeights.Count()))
    {
        error.Assign("benchmark pose allocation failed");
        return false;
    }
    output.localTransforms.Clear();
    output.morphWeights.Clear();
    if (!output.localTransforms.AppendRange(restPose.localTransforms.Data(), restPose.localTransforms.Count()) || !output.morphWeights.AppendRange(restPose.morphWeights.Data(), restPose.morphWeights.Count()))
    {
        error.Assign("benchmark pose copy failed");
        return false;
    }
    if (motionBone == UINT32_MAX)
    {
        for (uint32_t bone = 0; bone < skeleton.parents.Count(); ++bone)
        {
            const char* name = source.BoneName(bone);
            if (name && strstr(name, "Hips"))
            {
                motionBone = bone;
                break;
            }
        }
    }
    if (motionBone == UINT32_MAX)
    {
        for (uint32_t bone = 0; bone < skeleton.parents.Count(); ++bone)
        {
            if (source.BoneWritable(bone))
            {
                motionBone = bone;
                break;
            }
        }
    }
    if (motionBone == UINT32_MAX || motionBone >= output.localTransforms.Count())
    {
        error.Assign("animation source has no writable bone for the benchmark pose");
        return false;
    }
    // frameごとの小さな移動とY軸回転。
    // frame番号から決める周期位相。
    const double phase = static_cast<double>(frame) * 0.11;
    // 検査用の小さな角度と移動量。
    const double halfAngle = sin(phase) * 0.025;
    const float deltaY = static_cast<float>(sin(phase) * 0.012);
    const float sine = static_cast<float>(sin(halfAngle));
    const float cosine = static_cast<float>(cos(halfAngle));
    const gk::model::animation::FModelBoneTransform& rest = restPose.localTransforms.At(motionBone);
    gk::model::animation::FModelBoneTransform& moving = output.localTransforms.At(motionBone);
    moving.position[1] += deltaY;
    moving.rotation[0] = rest.rotation[0] * cosine + rest.rotation[2] * sine;
    moving.rotation[1] = rest.rotation[1] * cosine + rest.rotation[3] * sine;
    moving.rotation[2] = rest.rotation[2] * cosine + rest.rotation[0] * -sine;
    moving.rotation[3] = rest.rotation[3] * cosine + rest.rotation[1] * -sine;
    error.Clear();
    return true;
}

/**
 * fast pathと従来ufbx評価の実時間および全頂点一致を測る。
 */
bool BenchmarkDeform(const char* path, uint32_t frameCount, bool benchmarkLegacy, bool benchmarkSparse)
{
    // 対象FBXと共通animation sourceを所有するmodel resource。
    gk::String error;
    // 読み込んだmodelとそのanimation source。
    gk::detail::ModelResource* model = gk::detail::LoadModelPayload(path, error);
    if (!model || !model->animation || !model->animation->source || model->animation->source->Format() != gk::model::EModelAnimationFormat::Fbx)
    {
        fputs("{\"error\":", stdout);
        PrintJsonString(model ? "deformation benchmark requires an animated FBX model" : error.CStr());
        fputs("}\n", stdout);
        if (model)
        {
            gk::Release(&model->reference);
        }
        return false;
    }
    // 共通姿勢・骨格と比較対象の二つの書き込み先。
    // 形式共通のsource、親子骨格、基準姿勢。
    const gk::model::AModelAnimationSource& source = *model->animation->source;
    const gk::model::animation::FModelSkeleton& skeleton = source.Skeleton();
    gk::model::animation::FModelPose restPose;
    gk::model::animation::FModelPose pose;
    gk::model::animation::FModelSparsePoseGeometry sparse;
    const gk::Array<gk::model::animation::FModelSparseVertexMap>* sparseMap = source.SparseVertexMap();
    gk::detail::ModelResource* fastTarget = CloneDeformTarget(*model);
    gk::detail::ModelResource* legacyTarget = CloneDeformTarget(*model);
    if (!fastTarget || !legacyTarget || (benchmarkSparse && !sparseMap) || !gk::model::animation::InitializeModelPose(skeleton, restPose, error))
    {
        fputs("{\"error\":", stdout);
        PrintJsonString(error.Empty() ? "could not prepare FBX deformation benchmark resources" : error.CStr());
        fputs("}\n", stdout);
        if (fastTarget)
        {
            gk::Release(&fastTarget->reference);
        }
        if (legacyTarget)
        {
            gk::Release(&legacyTarget->reference);
        }
        gk::Release(&model->reference);
        return false;
    }

    // 計測から除外する初期frame数と全呼び出し数。
    const uint32_t warmupCount = 10;
    const uint32_t totalCount = warmupCount + frameCount;
    // 測定frameのCPU時間だけを保存する。
    std::vector<double> samples;
    samples.reserve(frameCount);
    // 高速変形の段階別時間をframeごとに保存する。
    std::vector<double> stageSamples[7];
    const char* stageNames[7] = { "scratch", "nodeMatrices", "clusterMatrices", "skinPositions", "generatedNormals", "cornerScatter", "outputCommit" };
    const char* sparseStageNames[7] = { "scratch", "nodeMatrices", "clusterMatrices", "skinPositions", "generatedNormals", "outputConversion", "outputCommit" };
    for (uint32_t stage = 0; stage < 7; ++stage)
    {
        stageSamples[stage].reserve(frameCount);
    }
    uint32_t profileCounts[6] = {};
    // 動かすHips骨と、二経路の比較結果。
    uint32_t motionBone = UINT32_MAX;
    bool equivalent = true;
    bool measuredFastPathUsed = false;
    bool referenceFastPathUsed = false;
    // warmup後も各frameで二経路の全頂点結果を比較する。
    for (uint32_t frame = 0; frame < totalCount; ++frame)
    {
        if (!MakeBenchmarkPose(source, skeleton, restPose, frame, pose, motionBone, error))
        {
            equivalent = false;
            break;
        }
        // 選択した経路の変形時間だけを計る。
        const auto start = std::chrono::steady_clock::now();
        const bool measuredSucceeded = benchmarkLegacy ? gk::model::DeformFbxAnimationWithUfbxForTesting(*model->animation->source, pose, *legacyTarget, error) : (benchmarkSparse ? source.DeformSparse(pose, sparse, error) : source.Deform(pose, *fastTarget, error));
        const auto stop = std::chrono::steady_clock::now();
        if (!measuredSucceeded)
        {
            equivalent = false;
            break;
        }
        bool frameFastPathUsed = false;
        if (!benchmarkLegacy)
        {
            frameFastPathUsed = benchmarkSparse || gk::model::FbxLastDeformUsedFastPathForTesting();
            measuredFastPathUsed = measuredFastPathUsed || frameFastPathUsed;
        }
        bool referenceSucceeded = false;
        if (benchmarkLegacy)
        {
            referenceSucceeded = source.Deform(pose, *fastTarget, error);
            frameFastPathUsed = gk::model::FbxLastDeformUsedFastPathForTesting();
            referenceFastPathUsed = referenceFastPathUsed || frameFastPathUsed;
        }
        else
        {
            referenceSucceeded = gk::model::DeformFbxAnimationWithUfbxForTesting(*model->animation->source, pose, *legacyTarget, error);
        }
        if (!referenceSucceeded || fastTarget->vertices.Count() != legacyTarget->vertices.Count())
        {
            equivalent = false;
            break;
        }
        // 頂点ごとの位置と法線を独立経路間で比較する。
        for (uint32_t vertex = 0; vertex < model->vertices.Count() && equivalent; ++vertex)
        {
            const gk::detail::ModelVertex& legacy = legacyTarget->vertices.At(vertex);
            gk::detail::ModelVertex fast{};
            if (benchmarkSparse)
            {
                if (vertex >= sparseMap->Count())
                {
                    equivalent = false;
                    break;
                }
                const gk::model::animation::FModelSparseVertexMap& mapping = sparseMap->At(vertex);
                if (mapping.positionIndex >= sparse.positions.Count() || mapping.normalIndex >= sparse.normals.Count())
                {
                    equivalent = false;
                    break;
                }
                const gk::model::animation::FModelSparsePoseGeometry::FModelVector4& position = sparse.positions.At(mapping.positionIndex);
                const gk::model::animation::FModelSparsePoseGeometry::FModelVector4& normal = sparse.normals.At(mapping.normalIndex);
                memcpy(fast.position, position.value, sizeof(fast.position));
                memcpy(fast.normal, normal.value, sizeof(fast.normal));
            }
            else
            {
                fast = fastTarget->vertices.At(vertex);
            }
            // XYZ全軸を有限値と許容誤差で検査する。
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (!isfinite(fast.position[axis]) || !isfinite(legacy.position[axis]) || !isfinite(fast.normal[axis]) || !isfinite(legacy.normal[axis]) || fabs(static_cast<double>(fast.position[axis]) - legacy.position[axis]) > 0.0005 || fabs(static_cast<double>(fast.normal[axis]) - legacy.normal[axis]) > 0.0005)
                {
                    equivalent = false;
                    break;
                }
            }
        }
        if (!equivalent)
        {
            if (error.Empty())
            {
                error.Assign("fast and legacy FBX deformation differ");
            }
            break;
        }
        if (frame >= warmupCount)
        {
            const double elapsedMs = std::chrono::duration<double, std::milli>(stop - start).count();
            samples.push_back(elapsedMs);
            if (frameFastPathUsed)
            {
                double stageMilliseconds[7] = {};
                gk::model::GetLastFbxDeformProfileForTesting(stageMilliseconds, profileCounts);
                for (uint32_t stage = 0; stage < 7; ++stage)
                {
                    stageSamples[stage].push_back(stageMilliseconds[stage]);
                }
            }
        }
    }
    if (!equivalent || samples.size() != frameCount)
    {
        fputs("{\"error\":", stdout);
        PrintJsonString(error.Empty() ? "FBX benchmark did not complete every measured frame" : error.CStr());
        fputs("}\n", stdout);
        gk::Release(&legacyTarget->reference);
        gk::Release(&fastTarget->reference);
        gk::Release(&model->reference);
        return false;
    }
    std::sort(samples.begin(), samples.end());
    // 平均と最近順位方式の95 percentileを出す。
    double totalMs = 0.0;
    for (double sample : samples)
    {
        totalMs += sample;
    }
    const double meanMs = totalMs / static_cast<double>(samples.size());
    const size_t p95Index = static_cast<size_t>(ceil(static_cast<double>(samples.size()) * 0.95)) - 1;
    printf("{\"path\":");
    PrintJsonString(path);
    printf(",\"mode\":\"%s\",\"frames\":%u,\"warmupFrames\":%u,\"vertexCount\":%u,\"motionBoneIndex\":%u,\"motionBoneName\":", benchmarkLegacy ? "legacy" : (benchmarkSparse ? "sparse" : "fast"), frameCount, warmupCount, model->vertices.Count(), motionBone);
    PrintJsonString(source.BoneName(motionBone));
    if (benchmarkSparse)
    {
        printf(",\"sparsePositionCount\":%u,\"sparseNormalCount\":%u,\"sparseMapCount\":%u", sparse.positions.Count(), sparse.normals.Count(), sparseMap ? sparseMap->Count() : 0u);
    }
    printf(",\"measuredFastPathUsed\":%s,\"referenceFastPathUsed\":%s,\"allVertexPositionNormalMatches\":true,\"meanMs\":%.6f,\"p95Ms\":%.6f,\"fastProfile\":", measuredFastPathUsed ? "true" : "false", referenceFastPathUsed ? "true" : "false", meanMs, samples[p95Index]);
    if (stageSamples[0].size() == frameCount)
    {
        printf("{\"counts\":{\"nodes\":%u,\"clusters\":%u,\"influences\":%u,\"sourceVertices\":%u,\"corners\":%u,\"normalValues\":%u},\"stages\":{", profileCounts[0], profileCounts[1], profileCounts[2], profileCounts[3], profileCounts[4], profileCounts[5]);
        for (uint32_t stage = 0; stage < 7; ++stage)
        {
            std::sort(stageSamples[stage].begin(), stageSamples[stage].end());
            double stageTotalMs = 0.0;
            for (double sample : stageSamples[stage])
            {
                stageTotalMs += sample;
            }
            const double stageMeanMs = stageTotalMs / static_cast<double>(stageSamples[stage].size());
            const size_t stageP95Index = static_cast<size_t>(ceil(static_cast<double>(stageSamples[stage].size()) * 0.95)) - 1;
            printf("%s\"%s\":{\"meanMs\":%.6f,\"p95Ms\":%.6f}", stage ? "," : "", benchmarkSparse ? sparseStageNames[stage] : stageNames[stage], stageMeanMs, stageSamples[stage][stageP95Index]);
        }
        fputs("}}", stdout);
    }
    else
    {
        fputs("null", stdout);
    }
    fputs("}\n", stdout);
    gk::Release(&legacyTarget->reference);
    gk::Release(&fastTarget->reference);
    gk::Release(&model->reference);
    return true;
}

/**
 * ベンチマークframe数を安全な範囲で読む。
 */
bool ParseFrameCount(const char* text, uint32_t& frameCount)
{
    if (!text || !*text)
    {
        return false;
    }
    // strtolが消費した末尾と数値化したframe数。
    char* end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (*end != '\0' || parsed < 1 || parsed > 1000)
    {
        return false;
    }
    frameCount = static_cast<uint32_t>(parsed);
    return true;
}

}

/**
 * 1個の実モデルを読み込み、機械可読な調査結果を出力する。
 */
int main(int argc, char** argv)
{
    if (argc >= 3 && (strcmp(argv[2], "--benchmark-deform") == 0 || strcmp(argv[2], "--benchmark-deform-legacy") == 0 || strcmp(argv[2], "--benchmark-deform-sparse") == 0))
    {
        uint32_t frameCount = 32;
        if (argc > 4 || (argc == 4 && !ParseFrameCount(argv[3], frameCount)))
        {
            fprintf(stderr, "usage: local_model_inspect <model-file> [--benchmark-deform [frames]|--benchmark-deform-sparse [frames]|--benchmark-deform-legacy [frames]]\n");
            return 2;
        }
        return BenchmarkDeform(argv[1], frameCount, strcmp(argv[2], "--benchmark-deform-legacy") == 0, strcmp(argv[2], "--benchmark-deform-sparse") == 0) ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[2], "--animation") == 0)
        return InspectAnimation(argv[1]) ? 0 : 1;
    if (argc != 2)
    {
        fprintf(stderr, "usage: local_model_inspect <model-file> [--animation|--benchmark-deform [frames]|--benchmark-deform-sparse [frames]|--benchmark-deform-legacy [frames]]\n");
        return 2;
    }
    return Inspect(argv[1]) ? 0 : 1;
}
