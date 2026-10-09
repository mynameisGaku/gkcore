// SPDX-License-Identifier: NOASSERTION
#include "model/ModelLoader.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/FbxAnimation.h"
#include "model/Model.h"
#include "foundation/Memory.h"

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
 * FBX固定入力をCPU参照skinと法線計算へ通し、既存sparse出力と比較する。
 */
bool CheckGpuSkinningCpuReference(const gk::model::AModelAnimationSource& source, const gk::model::animation::FModelPose& pose, const gk::model::animation::FModelSparsePoseGeometry& expected, float& maxPositionError, float& maxNormalError, String& failure)
{
    using FGeometry = gk::model::animation::FModelGpuSkinningGeometry;
    const FGeometry* geometry = source.GpuSkinningGeometry();
    maxPositionError = 0.0f;
    maxNormalError = 0.0f;
    gk::Array<FGeometry::FMatrix> matrices;
    if (!geometry || !source.EvaluateGpuSkinningMatrices(pose, matrices, failure) || geometry->positions.Count() == 0 || geometry->influenceRanges.Count() != geometry->positions.Count() || geometry->clusters.Count() != matrices.Count() || geometry->normalGroupRanges.Count() != expected.normals.Count())
    {
        failure.Assign("FBX GPU skinning source geometry or pose matrices are incomplete");
        return false;
    }
    gk::Array<FGeometry::FPosition> positions;
    if (!positions.Reserve(geometry->positions.Count()))
    {
        failure.Assign("FBX GPU skinning CPU reference position allocation failed");
        return false;
    }
    for (uint32_t positionIndex = 0; positionIndex < geometry->positions.Count(); ++positionIndex)
    {
        const FGeometry::FPosition& bind = geometry->positions.At(positionIndex);
        const FGeometry::FInfluenceRange& range = geometry->influenceRanges.At(positionIndex);
        if (range.firstInfluence > geometry->influences.Count() || range.influenceCount > geometry->influences.Count() - range.firstInfluence)
        {
            failure.Assign("FBX GPU skinning influence range is invalid");
            return false;
        }
        double result[3]{};
        double totalWeight = 0.0;
        for (uint32_t influenceIndex = 0; influenceIndex < range.influenceCount; ++influenceIndex)
        {
            const FGeometry::FInfluence& influence = geometry->influences.At(range.firstInfluence + influenceIndex);
            if (influence.clusterIndex >= matrices.Count() || !isfinite(influence.weight))
            {
                failure.Assign("FBX GPU skinning influence references an invalid matrix");
                return false;
            }
            totalWeight += influence.weight;
            const double* matrix = matrices.At(influence.clusterIndex).value;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                result[axis] += (matrix[axis * 4] * bind.value[0] + matrix[axis * 4 + 1] * bind.value[1] + matrix[axis * 4 + 2] * bind.value[2] + matrix[axis * 4 + 3]) * influence.weight;
            }
        }
        if (!isfinite(totalWeight))
        {
            failure.Assign("FBX GPU skinning influence weight is non-finite");
            return false;
        }
        if (totalWeight <= 0.0)
        {
            const FGeometry::FSegment* segment = nullptr;
            for (uint32_t segmentIndex = 0; segmentIndex < geometry->segments.Count(); ++segmentIndex)
            {
                const auto& candidateSegment = geometry->segments.At(segmentIndex);
                if (positionIndex >= candidateSegment.firstPosition && positionIndex - candidateSegment.firstPosition < candidateSegment.positionCount)
                {
                    segment = &candidateSegment;
                    break;
                }
            }
            if (!segment || segment->clusterCount == 0 || segment->firstCluster > matrices.Count() || segment->clusterCount > matrices.Count() - segment->firstCluster)
            {
                failure.Assign("FBX GPU skinning fallback segment is invalid");
                return false;
            }
            const double* matrix = matrices.At(segment->firstCluster + segment->clusterCount - 1).value;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                result[axis] = matrix[axis * 4] * bind.value[0] + matrix[axis * 4 + 1] * bind.value[1] + matrix[axis * 4 + 2] * bind.value[2] + matrix[axis * 4 + 3];
            }
        }
        FGeometry::FPosition output{};
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(result[axis]))
            {
                failure.Assign("FBX GPU skinning CPU reference position is non-finite");
                return false;
            }
            output.value[axis] = result[axis];
        }
        if (!positions.Append(output))
        {
            failure.Assign("FBX GPU skinning CPU reference position allocation failed");
            return false;
        }
    }
    const gk::Array<gk::model::animation::FModelSparseVertexMap>* sparseMap = source.SparseVertexMap();
    if (!sparseMap || sparseMap->Count() != geometry->corners.Count())
    {
        failure.Assign("FBX GPU skinning source corner map is incomplete");
        return false;
    }
    for (uint32_t corner = 0; corner < sparseMap->Count(); ++corner)
    {
        const auto& mapping = sparseMap->At(corner);
        if (mapping.positionIndex >= positions.Count() || mapping.positionIndex >= expected.positions.Count())
        {
            failure.Assign("FBX GPU skinning position map contains an invalid index");
            return false;
        }
        const FGeometry::FPosition& actual = positions.At(mapping.positionIndex);
        const auto& expectedPosition = expected.positions.At(mapping.positionIndex);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const float actualValue = static_cast<float>(actual.value[axis]);
            maxPositionError = fmaxf(maxPositionError, fabsf(actualValue - expectedPosition.value[axis]));
            if (!Near(actualValue, expectedPosition.value[axis], 0.0005f))
            {
                char diagnostic[256];
                snprintf(diagnostic, sizeof(diagnostic), "FBX GPU skinning FP64 matrix path differs at position=%u axis=%u actual=%.7f expected=%.7f", mapping.positionIndex, axis, actualValue, expectedPosition.value[axis]);
                failure.Assign(diagnostic);
                return false;
            }
        }
    }
    const auto faceNormal = [&](uint32_t faceIndex, double result[3]) -> bool
    {
        if (faceIndex >= geometry->faces.Count())
            return false;
        const FGeometry::FFace& face = geometry->faces.At(faceIndex);
        if (face.cornerCount < 3 || face.firstCorner > geometry->corners.Count() || face.cornerCount > geometry->corners.Count() - face.firstCorner)
            return false;
        const auto point = [&](uint32_t cornerIndex, double pointValue[3]) -> bool
        {
            const uint32_t index = geometry->corners.At(face.firstCorner + cornerIndex).positionIndex;
            if (index >= positions.Count())
                return false;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                pointValue[axis] = positions.At(index).value[axis];
            }
            return true;
        };
        double a[3]{};
        double b[3]{};
        double c[3]{};
        if (!point(0, a) || !point(1, b) || !point(2, c))
            return false;
        if (face.cornerCount == 3)
        {
            const double ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            const double ac[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            result[0] = ab[1] * ac[2] - ab[2] * ac[1];
            result[1] = ab[2] * ac[0] - ab[0] * ac[2];
            result[2] = ab[0] * ac[1] - ab[1] * ac[0];
        }
        else if (face.cornerCount == 4)
        {
            double d[3]{};
            if (!point(3, d))
                return false;
            const double ca[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            const double db[3] = { d[0] - b[0], d[1] - b[1], d[2] - b[2] };
            result[0] = ca[1] * db[2] - ca[2] * db[1];
            result[1] = ca[2] * db[0] - ca[0] * db[2];
            result[2] = ca[0] * db[1] - ca[1] * db[0];
        }
        else
        {
            result[0] = result[1] = result[2] = 0.0f;
            for (uint32_t corner = 0; corner < face.cornerCount; ++corner)
            {
                double current[3]{};
                double next[3]{};
                if (!point(corner, current) || !point((corner + 1) % face.cornerCount, next))
                    return false;
                result[0] += (current[1] - next[1]) * (current[2] + next[2]);
                result[1] += (current[2] - next[2]) * (current[0] + next[0]);
                result[2] += (current[0] - next[0]) * (current[1] + next[1]);
            }
        }
        return true;
    };
    for (uint32_t normalIndex = 0; normalIndex < geometry->normalGroupRanges.Count(); ++normalIndex)
    {
        const FGeometry::FNormalGroupRange& range = geometry->normalGroupRanges.At(normalIndex);
        if (range.firstFace > geometry->normalFaceIds.Count() || range.faceCount > geometry->normalFaceIds.Count() - range.firstFace)
        {
            failure.Assign("FBX GPU skinning normal incidence range is invalid");
            return false;
        }
        double sum[3]{};
        for (uint32_t incidence = 0; incidence < range.faceCount; ++incidence)
        {
            double normal[3]{};
            if (!faceNormal(geometry->normalFaceIds.At(range.firstFace + incidence), normal))
            {
                failure.Assign("FBX GPU skinning face geometry is invalid");
                return false;
            }
            sum[0] += normal[0];
            sum[1] += normal[1];
            sum[2] += normal[2];
        }
        const double length = sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
        if (!isfinite(length) || length <= 1.0e-15f)
        {
            failure.Assign("FBX GPU skinning CPU reference produced a degenerate normal");
            return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const float actualValue = static_cast<float>(sum[axis] / length);
            maxNormalError = fmaxf(maxNormalError, fabsf(actualValue - expected.normals.At(normalIndex).value[axis]));
            if (!Near(actualValue, expected.normals.At(normalIndex).value[axis], 0.0005f))
            {
                char diagnostic[256];
                snprintf(diagnostic, sizeof(diagnostic), "FBX GPU skinning FP64 CSR normal differs at group=%u axis=%u actual=(%.7f,%.7f,%.7f) expected=(%.7f,%.7f,%.7f) raw=(%.7f,%.7f,%.7f) faces=%u", normalIndex, axis, static_cast<float>(sum[0] / length), static_cast<float>(sum[1] / length), static_cast<float>(sum[2] / length), expected.normals.At(normalIndex).value[0], expected.normals.At(normalIndex).value[1], expected.normals.At(normalIndex).value[2], sum[0], sum[1], sum[2], range.faceCount);
                failure.Assign(diagnostic);
                return false;
            }
        }
    }
    fprintf(stdout, "gkcore_fbx_gpu_skin_reference positions=%u normals=%u maxPositionError=%.8f maxNormalError=%.8f\n", geometry->positions.Count(), geometry->normalGroupRanges.Count(), maxPositionError, maxNormalError);
    failure.Clear();
    return true;
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

    // scene標本化が小さな一時配列以外を確保しないことを確認する。
    gk::model::animation::FModelPose boundedAllocationPose;
    String boundedAllocationError;
    gk::SetAllocationFailureAfterForTesting(1);
    const bool boundedAllocationSucceeded = source.Sample(0, 0.5, boundedAllocationPose, boundedAllocationError);
    gk::ResetAllocationFailureForTesting();
    if (!boundedAllocationSucceeded || boundedAllocationPose.localTransforms.Count() != skeleton.parents.Count() || !Near(boundedAllocationPose.localTransforms.At(parentBone).position[1], expectedPoseOffset))
    {
        Release(&model->reference);
        failure.Assign("FBX sample exceeded its bounded per-call allocation contract");
        failure.Append(" (");
        failure.Append(boundedAllocationError.CStr());
        failure.Append(")");
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

    // fast path対象ならcorner map経由のsparse位置・法線も確認する。
    const gk::Array<gk::model::animation::FModelSparseVertexMap>* sparseMap = source.SparseVertexMap();
    if (sparseMap)
    {
        gk::model::animation::FModelSparsePoseGeometry sparse;
        bool sparseMatches = sparseMap->Count() == originalVertexCount && source.DeformSparse(pose, sparse, error);
        for (uint32_t vertex = 0; vertex < originalVertexCount && sparseMatches; ++vertex)
        {
            const gk::model::animation::FModelSparseVertexMap& mapping = sparseMap->At(vertex);
            if (mapping.positionIndex >= sparse.positions.Count() || mapping.normalIndex >= sparse.normals.Count())
            {
                sparseMatches = false;
                break;
            }
            const gk::model::animation::FModelSparsePoseGeometry::FModelVector4& position = sparse.positions.At(mapping.positionIndex);
            const gk::model::animation::FModelSparsePoseGeometry::FModelVector4& normal = sparse.normals.At(mapping.normalIndex);
            const detail::ModelVertex& expected = deformed->vertices.At(vertex);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (!Near(position.value[axis], expected.position[axis]) || !Near(normal.value[axis], expected.normal[axis]))
                {
                    sparseMatches = false;
                    break;
                }
            }
        }
        if (!sparseMatches)
        {
            Release(&deformed->reference);
            Release(&model->reference);
            failure.Assign("FBX node-only sparse deformation does not match its full vertex path: ");
            failure.Append(error.CStr());
            return false;
        }
    }

    if (strstr(fixtureName, "skin"))
    {
        // 同じposeをufbx従来経路へ渡し、全頂点の位置と法線を比較する。
        detail::ModelResource* referenceDeform = CloneGeometry(*model);
        if (!referenceDeform || !gk::model::DeformFbxAnimationWithUfbxForTesting(source, pose, *referenceDeform, error))
        {
            if (referenceDeform)
            {
                Release(&referenceDeform->reference);
            }
            Release(&deformed->reference);
            Release(&model->reference);
            failure.Assign("FBX evaluator reference deformation failed: ");
            failure.Append(error.CStr());
            return false;
        }
        bool deformationMatchesReference = true;
        for (uint32_t vertex = 0; vertex < originalVertexCount && deformationMatchesReference; ++vertex)
        {
            const detail::ModelVertex& actual = deformed->vertices.At(vertex);
            const detail::ModelVertex& reference = referenceDeform->vertices.At(vertex);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (!Near(actual.position[axis], reference.position[axis]) || !Near(actual.normal[axis], reference.normal[axis]))
                {
                    deformationMatchesReference = false;
                    break;
                }
            }
        }
        Release(&referenceDeform->reference);
        if (!deformationMatchesReference)
        {
            Release(&deformed->reference);
            Release(&model->reference);
            failure.Assign("FBX linear skin output differs from the ufbx evaluator reference");
            return false;
        }

        // skin deformationが少数の一時配列だけで完了することを確認する。
        detail::ModelResource* boundedDeform = CloneGeometry(*model);
        if (!boundedDeform)
        {
            Release(&deformed->reference);
            Release(&model->reference);
            failure.Assign("could not clone FBX geometry for the bounded deformation test");
            return false;
        }
        gk::SetAllocationFailureAfterForTesting(5);
        const bool boundedDeformSucceeded = source.Deform(pose, *boundedDeform, error);
        gk::ResetAllocationFailureForTesting();
        if (!boundedDeformSucceeded || boundedDeform->vertices.Count() != originalVertexCount || !Near(boundedDeform->vertices.At(0).position[1], model->vertices.At(0).position[1] + expectedDeformOffset))
        {
            Release(&boundedDeform->reference);
            Release(&deformed->reference);
            Release(&model->reference);
            failure.Assign("FBX deformation exceeded its bounded per-call allocation contract");
            failure.Append(" (");
            failure.Append(error.CStr());
            failure.Append(")");
            return false;
        }
        Release(&boundedDeform->reference);
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

/**
 * 空のclusterは混在時に許可し、skin全体が空なら読み込みを拒否する。
 */
bool CheckEmptySkinClusters(const char* fixtureDirectory, String& failure)
{
    // 2種類のskin fixtureを切り替えて読む固定長path buffer。
    char path[1024];
    // 空clusterと有効clusterが共存するfixtureのpath長。
    const int validLength = snprintf(path, sizeof(path), "%s/fbx-animation-skin-empty-cluster.fbx", fixtureDirectory);
    if (validLength <= 0 || static_cast<size_t>(validLength) >= sizeof(path))
    {
        failure.Assign("FBX empty-cluster fixture path exceeded its bound");
        return false;
    }
    // fixture読み込み時のdiagnosticを受け取る文字列。
    String error;
    // 有効clusterを含むfixtureの読み込み結果。
    detail::ModelResource* model = detail::LoadModelPayload(path, error);
    if (!model || !model->animation || !model->animation->source || model->animation->source->ClipCount() != 1)
    {
        if (model)
        {
            Release(&model->reference);
        }
        failure.Assign("FBX skin with an unused empty cluster failed to load: ");
        failure.Append(error.CStr());
        return false;
    }
    Release(&model->reference);

    // 全clusterが空のfixtureのpath長。
    const int emptyLength = snprintf(path, sizeof(path), "%s/fbx-animation-skin-empty-only.fbx", fixtureDirectory);
    if (emptyLength <= 0 || static_cast<size_t>(emptyLength) >= sizeof(path))
    {
        failure.Assign("FBX empty-only skin fixture path exceeded its bound");
        return false;
    }
    model = detail::LoadModelPayload(path, error);
    if (model)
    {
        Release(&model->reference);
        failure.Assign("FBX skin with no weighted clusters was accepted");
        return false;
    }
    if (!strstr(error.CStr(), "no weighted clusters"))
    {
        failure.Assign("FBX all-empty skin did not report the missing weighted clusters: ");
        failure.Append(error.CStr());
        return false;
    }
    return true;
}

/**
 * 高速skin結果を同じposeのufbx従来評価と全頂点で比較する。
 */
bool CheckFastSkinReference(const char* fixtureDirectory, const char* fixtureName, String& failure, double sampleTime = 0.5, bool requireFallbackInfluence = false)
{
    // 読み込むskin fixtureの完全pathを固定長bufferへ作る。
    char path[1024];
    const int pathLength = snprintf(path, sizeof(path), "%s/%s", fixtureDirectory, fixtureName);
    if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(path))
    {
        failure.Assign("FBX skin reference fixture path exceeded its bound");
        return false;
    }
    // fixture読込時の診断を保持する。
    String error;
    detail::ModelResource* model = detail::LoadModelPayload(path, error);
    if (!model || !model->animation || !model->animation->source || model->animation->source->ClipCount() == 0)
    {
        if (model)
        {
            Release(&model->reference);
        }
        failure.Assign("FBX transformed skin fixture failed to load: ");
        failure.Append(error.CStr());
        return false;
    }
    const gk::model::AModelAnimationSource& source = *model->animation->source;
    gk::model::animation::FModelPose pose;
    if (!source.Sample(0, sampleTime, pose, error))
    {
        Release(&model->reference);
        failure.Assign("FBX transformed skin fixture did not sample: ");
        failure.Append(error.CStr());
        return false;
    }
    uint32_t animatedBoneIndex = UINT32_MAX;
    for (uint32_t boneIndex = 0; boneIndex < model->animation->source->Skeleton().parents.Count(); ++boneIndex)
    {
        const char* boneName = source.BoneName(boneIndex);
        if (boneName && strcmp(boneName, "SkinBone") == 0)
        {
            animatedBoneIndex = boneIndex;
            break;
        }
    }
    if (animatedBoneIndex == UINT32_MAX)
    {
        Release(&model->reference);
        failure.Assign("FBX transformed skin fixture did not retain its animated bone");
        return false;
    }
    if (requireFallbackInfluence)
    {
        // syntheticなfallback clusterを参照する頂点がfixtureにあることを確認する。
        const auto* geometry = source.GpuSkinningGeometry();
        // fallback influenceを実際に使う頂点が見つかったかを保持する。
        bool foundFallbackInfluence = false;
        if (geometry && geometry->segments.Count() > 0)
        {
            // 対象meshの最後のclusterは頂点ウェイト欠落用。
            const auto& segment = geometry->segments.At(0);
            const uint32_t fallbackCluster = segment.firstCluster + segment.clusterCount - 1;
            for (uint32_t positionIndex = segment.firstPosition; positionIndex < segment.firstPosition + segment.positionCount; ++positionIndex)
            {
                // 一件だけのweight=1がfallback行列を指すことを調べる。
                const auto& range = geometry->influenceRanges.At(positionIndex);
                if (range.influenceCount == 1 && range.firstInfluence < geometry->influences.Count())
                {
                    // fallbackに置換されたweight情報を読む。
                    const auto& influence = geometry->influences.At(range.firstInfluence);
                    foundFallbackInfluence = foundFallbackInfluence || (influence.clusterIndex == fallbackCluster && Near(influence.weight, 1.0f));
                }
            }
        }
        if (!foundFallbackInfluence)
        {
            Release(&model->reference);
            failure.Assign("FBX zero-weight vertex did not use the mesh-node fallback influence");
            return false;
        }
    }
    // 親変換とskin行列が変わる入力を作り、両評価経路で動きを比較する。
    pose.localTransforms.At(animatedBoneIndex).position[1] += 0.75f;
    detail::ModelResource* fast = CloneGeometry(*model);
    detail::ModelResource* reference = CloneGeometry(*model);
    if (!fast || !reference)
    {
        if (fast)
        {
            Release(&fast->reference);
        }
        if (reference)
        {
            Release(&reference->reference);
        }
        Release(&model->reference);
        failure.Assign("could not clone geometry for the FBX skin reference comparison");
        return false;
    }
    if (!source.Deform(pose, *fast, error) || !gk::model::DeformFbxAnimationWithUfbxForTesting(*model->animation->source, pose, *reference, error))
    {
        Release(&reference->reference);
        Release(&fast->reference);
        Release(&model->reference);
        failure.Assign("FBX skin fast or reference deformation failed: ");
        failure.Append(error.CStr());
        return false;
    }
    // sparse結果を固定対応表で展開し、従来経路の全頂点と比較する。
    gk::model::animation::FModelSparsePoseGeometry sparse;
    const gk::Array<gk::model::animation::FModelSparseVertexMap>* sparseMap = source.SparseVertexMap();
    if (!source.SparseDeformationIsValidated() || !sparseMap || sparseMap->Count() != model->vertices.Count() || !source.DeformSparse(pose, sparse, error) || sparse.positions.Count() == 0 || sparse.normals.Count() == 0 || static_cast<uint64_t>(sparse.positions.Count()) + sparse.normals.Count() >= static_cast<uint64_t>(model->vertices.Count()) * 2u)
    {
        Release(&reference->reference);
        Release(&fast->reference);
        Release(&model->reference);
        failure.Assign("FBX sparse deformation failed or omitted its immutable vertex map: ");
        failure.Append(error.CStr());
        return false;
    }
    String gpuReferenceFailure;
    float gpuMaxPositionError = 0.0f;
    float gpuMaxNormalError = 0.0f;
    const bool gpuReferenceMatches = CheckGpuSkinningCpuReference(source, pose, sparse, gpuMaxPositionError, gpuMaxNormalError, gpuReferenceFailure);
    bool changedFromRest = false;
    bool matchesReference = fast->vertices.Count() == reference->vertices.Count();
    uint32_t mismatchVertex = 0;
    uint32_t mismatchAxis = 0;
    float mismatchPosition = 0.0f;
    float mismatchNormal = 0.0f;
    for (uint32_t vertex = 0; vertex < fast->vertices.Count() && matchesReference; ++vertex)
    {
        const detail::ModelVertex& sourceVertex = model->vertices.At(vertex);
        const detail::ModelVertex& fastVertex = fast->vertices.At(vertex);
        const detail::ModelVertex& referenceVertex = reference->vertices.At(vertex);
        const gk::model::animation::FModelSparseVertexMap& sparseVertex = sparseMap->At(vertex);
        if (sparseVertex.positionIndex >= sparse.positions.Count() || sparseVertex.normalIndex >= sparse.normals.Count())
        {
            matchesReference = false;
            mismatchVertex = vertex;
            break;
        }
        const gk::model::animation::FModelSparsePoseGeometry::FModelVector4& sparsePosition = sparse.positions.At(sparseVertex.positionIndex);
        const gk::model::animation::FModelSparsePoseGeometry::FModelVector4& sparseNormal = sparse.normals.At(sparseVertex.normalIndex);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            changedFromRest = changedFromRest || fabsf(fastVertex.position[axis] - sourceVertex.position[axis]) > 0.01f;
            if (!Near(fastVertex.position[axis], referenceVertex.position[axis]) || !Near(fastVertex.normal[axis], referenceVertex.normal[axis]) || !Near(sparsePosition.value[axis], referenceVertex.position[axis]) || !Near(sparseNormal.value[axis], referenceVertex.normal[axis]))
            {
                matchesReference = false;
                mismatchVertex = vertex;
                mismatchAxis = axis;
                mismatchPosition = fastVertex.position[axis] - referenceVertex.position[axis];
                mismatchNormal = fastVertex.normal[axis] - referenceVertex.normal[axis];
                break;
            }
        }
    }
    gk::model::animation::FModelPose invalidPose;
    const float preservedPosition = sparse.positions.At(0).value[0];
    const float preservedNormal = sparse.normals.At(0).value[0];
    const bool sparseFailurePreservedOutput = !source.DeformSparse(invalidPose, sparse, error) && sparse.positions.Count() != 0 && sparse.normals.Count() != 0 && sparse.positions.At(0).value[0] == preservedPosition && sparse.normals.At(0).value[0] == preservedNormal;
    Release(&reference->reference);
    Release(&fast->reference);
    Release(&model->reference);
    if (!matchesReference || (sampleTime > 0.0 && !changedFromRest) || !sparseFailurePreservedOutput || !gpuReferenceMatches)
    {
        char diagnostic[256];
        snprintf(diagnostic, sizeof(diagnostic), "FBX skin reference mismatch in %s (vertex=%u axis=%u positionDelta=%.6f normalDelta=%.6f changed=%d)", fixtureName, mismatchVertex, mismatchAxis, mismatchPosition, mismatchNormal, changedFromRest ? 1 : 0);
        failure.Assign(diagnostic);
        if (!gpuReferenceMatches)
        {
            failure.Append("; ");
            failure.Append(gpuReferenceFailure.CStr());
        }
        return false;
    }
    fprintf(stdout, "gkcore_fbx_skin_pose_reference fixture=%s sampleTime=%.3f maxPositionError=%.8f maxNormalError=%.8f\n", fixtureName, sampleTime, gpuMaxPositionError, gpuMaxNormalError);
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
    if (gk::tests::CheckTranslationAnimation(argv[1], "fbx-animation-node-translation.fbx", "ParentMove", "Parent", 0.02f, 0.02f, 6.0f, failure) && gk::tests::CheckTranslationAnimation(argv[1], "fbx-animation-skin-translation.fbx", "SkinBoneMove", "SkinBone", 2.0f, 0.02f, 0.06f, failure) && gk::tests::CheckFastSkinReference(argv[1], "fbx-animation-skin-transformed.fbx", failure) && gk::tests::CheckFastSkinReference(argv[1], "fbx-animation-skin-empty-cluster.fbx", failure) && gk::tests::CheckFastSkinReference(argv[1], "fbx-animation-skin-zero-weight.fbx", failure, 0.0, true) && gk::tests::CheckFastSkinReference(argv[1], "fbx-animation-skin-zero-weight.fbx", failure, 0.5, true) && gk::tests::CheckFastSkinReference(argv[1], "fbx-animation-skin-zero-weight.fbx", failure, 1.0, true) && gk::tests::CheckMorphAnimation(argv[1], failure) && gk::tests::CheckDefaultMorphWeight(argv[1], failure) && gk::tests::CheckEmptySkinClusters(argv[1], failure))
    {
        return 0;
    }
    fprintf(stderr, "%s\n", failure.CStr());
    return 1;
}
