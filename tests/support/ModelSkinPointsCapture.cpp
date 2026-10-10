// SPDX-License-Identifier: NOASSERTION
#include "tests/support/ModelSkinPointsCapture.h"

#include "core/Context.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/EModelAnimationFormat.h"
#include "model/animation/FModelPlayback.h"
#include "model/animation/FModelSecondaryMotionState.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"
#include "model/animation/ModelSkinPoints.h"
#include "model/animation/ModelSnapshot.h"
#include "resources/Resources.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * native captureでinstance共通SRT適用前のmodel space skin位置と接触を調べる検証処理。
 */
namespace
{

using gk::FVector3d;
using gk::model::animation::FModelGpuSkinningGeometry;

/**
 * 診断に使う数値をJSON行として出力する。
 */
void EmitReport(uint32_t frame, uint32_t pointCount, uint32_t eligiblePointCount, uint32_t shapeCount, double maximumError, uint32_t pointPenetrationCount, uint32_t pointShapeContactCount, double maximumPenetrationDepth, bool contactTested, bool contactClear, bool valid)
{
    fprintf(stderr, "{\"type\":\"gkcore_model_skin_points_capture\",\"frame\":%u,\"points\":%u,\"eligiblePointCount\":%u,\"sampleCap\":8192,\"shapeCount\":%u,\"maxError\":%.17g,\"pointPenCount\":%u,\"pointShapeContactCount\":%u,\"maxDepth\":%.17g,\"contactTested\":%s,\"contactClear\":%s,\"valid\":%s}\n", frame, pointCount, eligiblePointCount, shapeCount, maximumError, pointPenetrationCount, pointShapeContactCount, maximumPenetrationDepth, contactTested ? "true" : "false", contactTested ? (contactClear ? "true" : "false") : "null", valid ? "true" : "false");
}

int FailProbe(uint32_t frame, int result, const char* message, uint32_t eligiblePointCount = 0);
double VectorLength(const double value[3]);
bool HasPrefixInfluence(const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, uint32_t positionId, const char* bonePrefix, size_t prefixLength, bool& selected, gk::String& error);
bool DistanceToShape(const FVector3d& point, const gk::model::animation::FModelSecondaryMotionCollisionShape& shape, double& distance);

}

extern "C" GKCORE_API int VerifyModelSkinPointsForTesting(gk::ModelHandle model, uint32_t frame, const char* bonePrefix)
{
    if (!model.IsValid() || !bonePrefix || !bonePrefix[0])
    {
        return FailProbe(frame, 0, "model handle or bone prefix is invalid");
    }
    gk::detail::ModelResource* resource = gk::detail::FindModel(model);
    gk::detail::ModelTransform* transform = resource ? gk::detail::FindModelTransform(model) : nullptr;
    if (!resource || !transform || !resource->animation || !resource->animation->source)
    {
        return FailProbe(frame, 0, "model animation source or transform is unavailable");
    }
    const gk::model::FModelPlayback* playback = transform->playback;
    const gk::model::AModelAnimationSource* source = resource->animation->source;
    if (source->Format() != gk::model::EModelAnimationFormat::Fbx)
    {
        return FailProbe(frame, -1, "only the FBX skin cache is supported by this probe");
    }
    const FModelGpuSkinningGeometry* geometry = source->GpuSkinningGeometry();
    if (!geometry)
    {
        return FailProbe(frame, -1, "the FBX source has no supported GPU skinning geometry");
    }

    gk::String error;
    gk::model::animation::FModelPose pose;
    const bool evaluated = playback ? gk::model::EvaluateModelPlaybackPose(*resource, *playback, pose, error) : gk::model::animation::InitializeModelPose(source->Skeleton(), pose, error);
    if (!evaluated)
    {
        return FailProbe(frame, 0, error.Empty() ? "the current playback pose could not be evaluated" : error.CStr());
    }
    for (uint32_t morph = 0; morph < pose.morphWeights.Count(); ++morph)
    {
        if (pose.morphWeights.At(morph) != 0.0f)
        {
            return FailProbe(frame, -1, "nonzero morph weights are not supported by this skin point probe");
        }
    }
    if (!source->SupportsGpuSkinningPose(pose))
    {
        return FailProbe(frame, -1, "the current pose is outside the supported FBX GPU skinning path");
    }
    if (geometry->influenceRanges.Count() != geometry->positions.Count())
    {
        return FailProbe(frame, 0, "FBX skin point ranges do not match position count");
    }

    gk::Array<uint32_t> selectedIds;
    const size_t prefixLength = strlen(bonePrefix);
    uint32_t eligibleCount = 0;
    for (uint32_t positionId = 0; positionId < geometry->positions.Count(); ++positionId)
    {
        bool selected = false;
        if (!HasPrefixInfluence(*source, *geometry, positionId, bonePrefix, prefixLength, selected, error))
        {
            return FailProbe(frame, 0, error.CStr(), eligibleCount);
        }
        if (selected)
        {
            ++eligibleCount;
        }
    }
    if (eligibleCount == 0)
    {
        return FailProbe(frame, -1, "no positive skin influences matched the requested bone prefix");
    }
    const uint32_t sampleCount = eligibleCount > 8192u ? 8192u : eligibleCount;
    if (!selectedIds.Reserve(sampleCount))
    {
        return FailProbe(frame, 0, "skin point selection allocation failed", eligibleCount);
    }
    uint32_t eligibleOrdinal = 0;
    uint32_t sampleOrdinal = 0;
    uint32_t nextSampleOrdinal = 0;
    for (uint32_t positionId = 0; positionId < geometry->positions.Count() && sampleOrdinal < sampleCount; ++positionId)
    {
        bool selected = false;
        if (!HasPrefixInfluence(*source, *geometry, positionId, bonePrefix, prefixLength, selected, error))
        {
            return FailProbe(frame, 0, error.CStr(), eligibleCount);
        }
        if (!selected)
        {
            continue;
        }
        if (eligibleOrdinal == nextSampleOrdinal)
        {
            if (!selectedIds.Append(positionId))
            {
                return FailProbe(frame, 0, "skin point id allocation failed", eligibleCount);
            }
            ++sampleOrdinal;
            nextSampleOrdinal = static_cast<uint32_t>((static_cast<uint64_t>(sampleOrdinal) * eligibleCount) / sampleCount);
        }
        ++eligibleOrdinal;
    }
    if (selectedIds.Count() != sampleCount)
    {
        return FailProbe(frame, 0, "skin point sampling did not select the requested number of ids", eligibleCount);
    }

    gk::Array<FModelGpuSkinningGeometry::FMatrix> matrices;
    if (!source->EvaluateGpuSkinningMatrices(pose, matrices, error))
    {
        return FailProbe(frame, 0, error.Empty() ? "FBX skin matrices could not be evaluated" : error.CStr(), eligibleCount);
    }
    gk::Array<FVector3d> evaluatedPoints;
    if (!gk::model::animation::EvaluateModelSkinPoints(*geometry, matrices, selectedIds.Data(), selectedIds.Count(), evaluatedPoints, error))
    {
        return FailProbe(frame, 0, error.Empty() ? "selected skin points could not be evaluated" : error.CStr(), eligibleCount);
    }
    gk::model::animation::FModelSparsePoseGeometry reference;
    if (!source->SparseDeformationIsValidated() || !source->DeformSparse(pose, reference, error))
    {
        return FailProbe(frame, 0, error.Empty() ? "the FBX sparse deformation reference is unavailable" : error.CStr(), eligibleCount);
    }

    double maximumError = 0.0;
    bool matchesReference = true;
    for (uint32_t sample = 0; sample < selectedIds.Count(); ++sample)
    {
        const uint32_t positionId = selectedIds.At(sample);
        if (positionId >= reference.positions.Count())
        {
            return FailProbe(frame, 0, "skin point id is outside the sparse reference positions", eligibleCount);
        }
        const auto& expected = reference.positions.At(positionId);
        double difference[3]{};
        double scale = 1.0;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(expected.value[axis]))
            {
                return FailProbe(frame, 0, "the sparse reference contains a non-finite position", eligibleCount);
            }
            difference[axis] = evaluatedPoints.At(sample).value[axis] - expected.value[axis];
            scale = fmax(scale, fmax(fabs(evaluatedPoints.At(sample).value[axis]), fabs(expected.value[axis])));
        }
        const double errorLength = VectorLength(difference);
        if (!isfinite(errorLength))
        {
            return FailProbe(frame, 0, "skin point reference error is non-finite", eligibleCount);
        }
        maximumError = fmax(maximumError, errorLength);
        if (errorLength > scale * 1.0e-5)
        {
            matchesReference = false;
        }
    }

    uint32_t pointPenetrationCount = 0;
    uint32_t pointShapeContactCount = 0;
    double maximumPenetrationDepth = 0.0;
    const gk::model::FModelSecondaryMotionState* secondary = playback ? playback->secondaryMotion : nullptr;
    const uint32_t shapeCount = secondary ? secondary->previousCollisionShapes.Count() : 0;
    const bool contactTested = shapeCount > 0;
    if (contactTested)
    {
        for (uint32_t sample = 0; sample < evaluatedPoints.Count(); ++sample)
        {
            bool pointPenetrates = false;
            for (uint32_t shapeIndex = 0; shapeIndex < secondary->previousCollisionShapes.Count(); ++shapeIndex)
            {
                const auto& shape = secondary->previousCollisionShapes.At(shapeIndex);
                double distance = 0.0;
                if (!(shape.radius > 0.0f) || !isfinite(shape.radius) || !DistanceToShape(evaluatedPoints.At(sample), shape, distance))
                {
                    return FailProbe(frame, 0, "the current body collision shape or skin point is invalid", eligibleCount);
                }
                const double penetration = static_cast<double>(shape.radius) - distance;
                if (penetration > 0.0)
                {
                    pointPenetrates = true;
                    ++pointShapeContactCount;
                    maximumPenetrationDepth = fmax(maximumPenetrationDepth, penetration);
                }
            }
            if (pointPenetrates)
            {
                ++pointPenetrationCount;
            }
        }
    }
    const bool contactClear = contactTested && pointPenetrationCount == 0;
    EmitReport(frame, evaluatedPoints.Count(), eligibleCount, shapeCount, maximumError, pointPenetrationCount, pointShapeContactCount, maximumPenetrationDepth, contactTested, contactClear, matchesReference);
    if (!matchesReference)
    {
        fprintf(stderr, "model skin point probe: evaluated positions differ from the sparse reference\n");
        return -1;
    }
    return 1;
}

namespace
{

/**
 * 失敗理由を出し、指定したprobe状態を返す。
 */
int FailProbe(uint32_t frame, int result, const char* message, uint32_t eligiblePointCount)
{
    EmitReport(frame, 0, eligiblePointCount, 0, 0.0, 0, 0, 0.0, false, false, false);
    fprintf(stderr, "model skin point probe: %s\n", message ? message : "unknown failure");
    return result;
}

/**
 * 有限な倍精度3成分の長さをoverflowしにくく計算する。
 */
double VectorLength(const double value[3])
{
    const double largest = fmax(fabs(value[0]), fmax(fabs(value[1]), fabs(value[2])));
    if (!(largest > 0.0) || !isfinite(largest))
    {
        return largest;
    }
    const double x = value[0] / largest;
    const double y = value[1] / largest;
    const double z = value[2] / largest;
    return largest * sqrt(x * x + y * y + z * z);
}

/**
 * 指定位置のinfluence範囲を検査し、prefix boneの正weightがあるか返す。
 */
bool HasPrefixInfluence(const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, uint32_t positionId, const char* bonePrefix, size_t prefixLength, bool& selected, gk::String& error)
{
    selected = false;
    const FModelGpuSkinningGeometry::FInfluenceRange& range = geometry.influenceRanges.At(positionId);
    if (range.firstInfluence > geometry.influences.Count() || range.influenceCount > geometry.influences.Count() - range.firstInfluence)
    {
        error.Assign("skin point probe found an invalid influence range");
        return false;
    }
    for (uint32_t offset = 0; offset < range.influenceCount; ++offset)
    {
        const FModelGpuSkinningGeometry::FInfluence& influence = geometry.influences.At(range.firstInfluence + offset);
        if (!isfinite(influence.weight) || influence.weight < 0.0 || influence.clusterIndex >= geometry.clusters.Count())
        {
            error.Assign("skin point probe found an invalid influence");
            return false;
        }
        if (!(influence.weight > 0.0))
        {
            continue;
        }
        const uint32_t bone = geometry.clusters.At(influence.clusterIndex).nodeIndex;
        if (bone >= source.Skeleton().parents.Count())
        {
            error.Assign("skin point probe found an invalid cluster bone");
            return false;
        }
        const char* name = source.BoneName(bone);
        if (name && strncmp(name, bonePrefix, prefixLength) == 0)
        {
            selected = true;
        }
    }
    return true;
}

/**
 * 点と球またはcapsule軸の最短距離を返す。
 */
bool DistanceToShape(const FVector3d& point, const gk::model::animation::FModelSecondaryMotionCollisionShape& shape, double& distance)
{
    const double axis[3] = { static_cast<double>(shape.end.x) - shape.start.x, static_cast<double>(shape.end.y) - shape.start.y, static_cast<double>(shape.end.z) - shape.start.z };
    const double offset[3] = { point.value[0] - shape.start.x, point.value[1] - shape.start.y, point.value[2] - shape.start.z };
    for (uint32_t axisIndex = 0; axisIndex < 3; ++axisIndex)
    {
        if (!isfinite(axis[axisIndex]) || !isfinite(offset[axisIndex]))
        {
            return false;
        }
    }
    const double axisLength = VectorLength(axis);
    if (!isfinite(axisLength))
    {
        return false;
    }
    double amount = 0.0;
    if (axisLength > 0.0)
    {
        const double unit[3] = { axis[0] / axisLength, axis[1] / axisLength, axis[2] / axisLength };
        const double projected = offset[0] * unit[0] + offset[1] * unit[1] + offset[2] * unit[2];
        if (!isfinite(projected))
        {
            return false;
        }
        amount = fmax(0.0, fmin(1.0, projected / axisLength));
    }
    const double difference[3] = { offset[0] - axis[0] * amount, offset[1] - axis[1] * amount, offset[2] - axis[2] * amount };
    distance = VectorLength(difference);
    return isfinite(distance);
}

}
