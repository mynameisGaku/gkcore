// SPDX-License-Identifier: NOASSERTION
#include "../src/model/ModelLoader.h"
#include "../src/model/animation/AModelAnimationSource.h"
#include "../src/model/animation/FModelAnimationAsset.h"
#include "../src/model/Model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace gk::tests
{
namespace
{
/**
 * FBX変換結果を許容誤差付きで比較する。
 */
bool Near(float left, float right, float tolerance = 0.0005f)
{
    return fabsf(left - right) <= tolerance;
}

/**
 * Deform()の入力を保つことを確認するため静的geometryを複製する。
 */
detail::ModelResource* CloneGeometry(const detail::ModelResource& source)
{
    detail::ModelResource* clone = detail::CreateModelResource();
    if (!clone)
    {
        return nullptr;
    }
    if (!clone->vertices.AppendRange(source.vertices.Data(), source.vertices.Count()) || !clone->indices.AppendRange(source.indices.Data(), source.indices.Count()) || !clone->primitives.AppendRange(source.primitives.Data(), source.primitives.Count()))
    {
        Release(&clone->reference);
        return nullptr;
    }
    clone->materials.Clear();
    if (!clone->materials.AppendRange(source.materials.Data(), source.materials.Count()))
    {
        Release(&clone->reference);
        return nullptr;
    }
    return clone;
}

/**
 * 生成FBXを読み込み、clip標本化、任意姿勢変形、所有期間を確認する。
 */
bool CheckTranslationAnimation(const char* fixtureDirectory, const char* fixtureName, const char* clipName, const char* boneName, float expectedPoseOffset, float expectedDeformOffset, float arbitraryDeformOffset, String& failure)
{
    // 読み込むFBX fixtureの完全pathを固定長bufferへ作る。
    char path[1024];
    const int length = snprintf(path, sizeof(path), "%s/%s", fixtureDirectory, fixtureName);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(path))
    {
        failure.Assign("FBX animation fixture path exceeded its bound");
        return false;
    }

    String error;
    detail::ModelResource* model = detail::LoadModelPayload(path, error);
    if (!model)
    {
        failure.Assign("animated FBX geometry failed to load: ");
        failure.Append(error.CStr());
        return false;
    }
    if (!model->animation || !model->animation->source)
    {
        Release(&model->reference);
        failure.Assign("FBX model did not retain its animation source");
        return false;
    }

    gk::model::AModelAnimationSource& source = *model->animation->source;
    const gk::model::animation::FModelSkeleton& skeleton = source.Skeleton();
    if (source.Format() != gk::model::EModelAnimationFormat::Fbx || source.ClipCount() != 1 || !source.ClipName(0) || strcmp(source.ClipName(0), clipName) != 0 || !Near(static_cast<float>(source.ClipDuration(0)), 1.0f) || skeleton.parents.Count() == 0 || skeleton.parents.Count() != skeleton.restLocalTransforms.Count())
    {
        Release(&model->reference);
        failure.Assign("FBX animation clip or parent-first skeleton metadata is incorrect");
        return false;
    }

    uint32_t parentBone = 0xffffffffu;
    for (uint32_t bone = 0; bone < skeleton.parents.Count(); ++bone)
    {
        const char* name = source.BoneName(bone);
        if (name && strcmp(name, boneName) == 0)
        {
            parentBone = bone;
            break;
        }
    }
    if (parentBone == 0xffffffffu)
    {
        Release(&model->reference);
        failure.Assign("FBX animation skeleton did not preserve the animated parent name");
        return false;
    }

    gk::model::animation::FModelPose pose;
    if (!source.Sample(0, 0.5, pose, error) || pose.localTransforms.Count() != skeleton.parents.Count() || !Near(pose.localTransforms.At(parentBone).position[1], expectedPoseOffset))
    {
        // 標本化時刻・node対応の実値を表示する。
        const float sampledY = pose.localTransforms.Count() > parentBone ? pose.localTransforms.At(parentBone).position[1] : -999.0f;
        char diagnostic[160];
        snprintf(diagnostic, sizeof(diagnostic), " (y=%.6f, duration=%.6f, bone=%u, error=%s)", sampledY, source.ClipDuration(0), parentBone, error.CStr());
        Release(&model->reference);
        failure.Assign("FBX clip midpoint did not sample the expected parent translation");
        failure.Append(diagnostic);
        return false;
    }

    const uint32_t originalVertexCount = model->vertices.Count();
    detail::ModelResource* deformed = CloneGeometry(*model);
    if (!deformed)
    {
        Release(&model->reference);
        failure.Assign("could not clone FBX geometry for the deformation test");
        return false;
    }
    if (!source.Deform(pose, *deformed, error) || deformed->vertices.Count() != originalVertexCount || deformed->indices.Count() != model->indices.Count() || deformed->materials.Count() != model->materials.Count())
    {
        Release(&deformed->reference);
        Release(&model->reference);
        failure.Assign("FBX clip deformation failed or changed non-vertex model data");
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }
    for (uint32_t vertex = 0; vertex < originalVertexCount; ++vertex)
    {
        const detail::ModelVertex& original = model->vertices.At(vertex);
        const detail::ModelVertex& result = deformed->vertices.At(vertex);
        if (!Near(result.position[0], original.position[0]) || !Near(result.position[1], original.position[1] + expectedDeformOffset) || !Near(result.position[2], original.position[2]))
        {
            // 頂点ごとの元値と評価値からnode transform適用漏れを見分ける。
            char diagnostic[192];
            snprintf(diagnostic, sizeof(diagnostic), "FBX clip vertex mismatch at %u (result=(%.6f,%.6f,%.6f), source=(%.6f,%.6f,%.6f), sampledParent=(%.6f,%.6f,%.6f), error=%s)", vertex, result.position[0], result.position[1], result.position[2], original.position[0], original.position[1], original.position[2], pose.localTransforms.At(parentBone).position[0], pose.localTransforms.At(parentBone).position[1], pose.localTransforms.At(parentBone).position[2], error.CStr());
            Release(&deformed->reference);
            Release(&model->reference);
            failure.Assign(diagnostic);
            return false;
        }
    }

    // ufbxが無視する暗黙root overrideをsourceが拒否し、出力を保つ。
    uint32_t rootBone = 0xffffffffu;
    gk::model::animation::FModelPose invalidRootPose;
    for (uint32_t bone = 0; bone < skeleton.parents.Count(); ++bone)
    {
        if (skeleton.parents.At(bone) == -1)
        {
            rootBone = bone;
        }
        invalidRootPose.localTransforms.Append(pose.localTransforms.At(bone));
    }
    for (uint32_t morph = 0; morph < pose.morphWeights.Count(); ++morph)
    {
        invalidRootPose.morphWeights.Append(pose.morphWeights.At(morph));
    }
    detail::ModelResource* rootProbe = CloneGeometry(*model);
    if (rootBone == 0xffffffffu || !rootProbe)
    {
        if (rootProbe)
        {
            Release(&rootProbe->reference);
        }
        Release(&deformed->reference);
        Release(&model->reference);
        failure.Assign("could not prepare the implicit-root override contract check");
        return false;
    }
    invalidRootPose.localTransforms.At(rootBone).position[1] += 1.0f;
    const bool rootOverrideRejected = !source.Deform(invalidRootPose, *rootProbe, error);
    bool rootOutputPreserved = rootProbe->vertices.Count() == originalVertexCount;
    for (uint32_t vertex = 0; rootOutputPreserved && vertex < originalVertexCount; ++vertex)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!Near(rootProbe->vertices.At(vertex).position[axis], model->vertices.At(vertex).position[axis]))
            {
                rootOutputPreserved = false;
                break;
            }
        }
    }
    Release(&rootProbe->reference);
    if (!rootOverrideRejected || !rootOutputPreserved)
    {
        Release(&deformed->reference);
        Release(&model->reference);
        failure.Assign("FBX ignored root transform was not rejected atomically");
        return false;
    }

    // 任意pose経路がclip再生と独立して動くことを確認する。
    pose.localTransforms.At(parentBone).position[1] = 6.0f;
    detail::ModelResource* arbitrary = CloneGeometry(*model);
    const bool arbitrarySucceeded = arbitrary && source.Deform(pose, *arbitrary, error);
    bool arbitraryMatches = arbitrarySucceeded && arbitrary->vertices.Count() == originalVertexCount;
    if (arbitraryMatches)
    {
        for (uint32_t vertex = 0; vertex < originalVertexCount; ++vertex)
        {
            if (!Near(arbitrary->vertices.At(vertex).position[1], model->vertices.At(vertex).position[1] + arbitraryDeformOffset))
            {
                // 任意pose結果の単位と頂点番号を診断する。
                char diagnostic[192];
                snprintf(diagnostic, sizeof(diagnostic), "FBX arbitrary pose vertex mismatch at %u (y=%.6f expected=%.6f, boneY=%.6f)", vertex, arbitrary->vertices.At(vertex).position[1], model->vertices.At(vertex).position[1] + arbitraryDeformOffset, pose.localTransforms.At(parentBone).position[1]);
                failure.Assign(diagnostic);
                arbitraryMatches = false;
                break;
            }
        }
    }
    if (arbitrary)
    {
        Release(&arbitrary->reference);
    }
    Release(&deformed->reference);
    Release(&model->reference);
    if (!arbitraryMatches)
    {
        return false;
    }
    return true;
}

/**
 * blend shapeの係数をsampleし、flatten後の全頂点へ反映する。
 */
bool CheckMorphAnimation(const char* fixtureDirectory, String& failure)
{
    // 読み込むmorph fixtureの完全pathを固定長bufferへ作る。
    char path[1024];
    const int length = snprintf(path, sizeof(path), "%s/fbx-animation-morph-weight.fbx", fixtureDirectory);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(path))
    {
        failure.Assign("FBX morph fixture path exceeded its bound");
        return false;
    }
    String error;
    detail::ModelResource* model = detail::LoadModelPayload(path, error);
    if (!model || !model->animation || !model->animation->source)
    {
        if (model)
        {
            Release(&model->reference);
        }
        failure.Assign("FBX blend shape fixture failed to load: ");
        failure.Append(error.CStr());
        return false;
    }
    gk::model::AModelAnimationSource& source = *model->animation->source;
    const gk::model::animation::FModelSkeleton& skeleton = source.Skeleton();
    if (source.ClipCount() != 1 || !source.ClipName(0) || strcmp(source.ClipName(0), "MorphLift") != 0 || skeleton.restMorphWeights.Count() != 1 || !source.MorphName(0) || strcmp(source.MorphName(0), "QuadLift") != 0)
    {
        Release(&model->reference);
        failure.Assign("FBX blend shape metadata or clip name is incorrect");
        return false;
    }
    gk::model::animation::FModelPose pose;
    if (!source.Sample(0, 0.5, pose, error) || pose.morphWeights.Count() != 1 || !Near(pose.morphWeights.At(0), 0.5f))
    {
        Release(&model->reference);
        failure.Assign("FBX morph clip did not sample a normalized half weight");
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }
    detail::ModelResource* deformed = CloneGeometry(*model);
    if (!deformed || !source.Deform(pose, *deformed, error))
    {
        if (deformed)
        {
            Release(&deformed->reference);
        }
        Release(&model->reference);
        failure.Assign("FBX blend shape evaluation failed: ");
        failure.Append(error.CStr());
        return false;
    }
    bool matches = deformed->vertices.Count() == model->vertices.Count();
    for (uint32_t vertex = 0; matches && vertex < model->vertices.Count(); ++vertex)
    {
        if (!Near(deformed->vertices.At(vertex).position[1], model->vertices.At(vertex).position[1] + 0.005f))
        {
            matches = false;
        }
    }
    Release(&deformed->reference);
    Release(&model->reference);
    if (!matches)
    {
        failure.Assign("FBX blend shape did not add the expected Y offset to every vertex");
        return false;
    }
    return true;
}

/**
 * FBXの初期morph係数が読み込み直後の頂点形状へ反映される。
 */
bool CheckDefaultMorphWeight(const char* fixtureDirectory, String& failure)
{
    char defaultPath[1024];
    char baselinePath[1024];
    const int defaultLength = snprintf(defaultPath, sizeof(defaultPath), "%s/fbx-animation-morph-default-weight.fbx", fixtureDirectory);
    const int baselineLength = snprintf(baselinePath, sizeof(baselinePath), "%s/fbx-animation-morph-weight.fbx", fixtureDirectory);
    if (defaultLength <= 0 || static_cast<size_t>(defaultLength) >= sizeof(defaultPath) || baselineLength <= 0 || static_cast<size_t>(baselineLength) >= sizeof(baselinePath))
    {
        failure.Assign("FBX default morph fixture path exceeded its bound");
        return false;
    }
    String error;
    detail::ModelResource* weighted = detail::LoadModelPayload(defaultPath, error);
    detail::ModelResource* baseline = detail::LoadModelPayload(baselinePath, error);
    if (!weighted || !baseline || !weighted->animation || !weighted->animation->source || !baseline->animation || !baseline->animation->source)
    {
        if (weighted)
        {
            Release(&weighted->reference);
        }
        if (baseline)
        {
            Release(&baseline->reference);
        }
        failure.Assign("FBX default morph fixture failed to load: ");
        failure.Append(error.CStr());
        return false;
    }
    const gk::model::animation::FModelSkeleton& skeleton = weighted->animation->source->Skeleton();
    bool matches = skeleton.restMorphWeights.Count() == 1 && Near(skeleton.restMorphWeights.At(0), 0.5f) && weighted->vertices.Count() == baseline->vertices.Count();
    for (uint32_t vertex = 0; matches && vertex < weighted->vertices.Count(); ++vertex)
    {
        matches = Near(weighted->vertices.At(vertex).position[1], baseline->vertices.At(vertex).position[1] + 0.005f);
    }
    Release(&weighted->reference);
    Release(&baseline->reference);
    if (!matches)
    {
        failure.Assign("FBX default morph weight was not applied to initial model vertices");
        return false;
    }
    return true;
}
}
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: model_fbx_animation_tests <fixture-directory>\n");
        return 2;
    }
    gk::String failure;
    if (gk::tests::CheckTranslationAnimation(argv[1], "fbx-animation-node-translation.fbx", "ParentMove", "Parent", 0.02f, 0.02f, 6.0f, failure) && gk::tests::CheckTranslationAnimation(argv[1], "fbx-animation-skin-translation.fbx", "SkinBoneMove", "SkinBone", 2.0f, 0.02f, 0.06f, failure) && gk::tests::CheckMorphAnimation(argv[1], failure) && gk::tests::CheckDefaultMorphWeight(argv[1], failure))
    {
        return 0;
    }
    fprintf(stderr, "%s\n", failure.CStr());
    return 1;
}
