// SPDX-License-Identifier: NOASSERTION
#include "tests/support/ModelSkinPointsCapture.h"
#include "tests/support/ModelSkinSurfaceCapture.h"

#include "core/Context.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/EModelAnimationFormat.h"
#include "model/animation/FModelPlayback.h"
#include "model/animation/FModelSecondaryMotionState.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"
#include "model/animation/ModelSkinPoints.h"
#include "model/animation/ModelSkinContactSnapshot.h"
#include "model/animation/ModelSnapshot.h"
#include "resources/Resources.h"
#include <gkcore/ModelSecondaryMotion.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
/**
 * 詳細を出すframe番号を、0以上のuint32範囲で読み取る。
 */
bool ParseDetailFrame(const char* value, uint32_t& frame);
/**
 * JSON文字列の引用符と制御文字をescapeして書き込む。
 */
void EmitJsonString(const char* value);
/**
 * 詳細診断の失敗を通常のskin検証と分けて記録する。
 */
void EmitDetailError(uint32_t frame, const char* message);
/**
 * 選択したframeの侵入点と骨の寄与を記録する。
 */
void EmitPointDetails(gk::ModelHandle model, uint32_t frame, const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, const gk::Array<uint32_t>& selectedIds, const gk::Array<FModelGpuSkinningGeometry::FMatrix>& matrices, const gk::model::animation::FModelSparsePoseGeometry& reference, const gk::model::FModelSecondaryMotionState* secondary);
bool EmitSkinBindingDump(uint32_t frame, const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, const gk::Array<uint32_t>& selectedIds, const gk::model::FModelSecondaryMotionState* secondary, gk::String& error);
/**
 * 元の点選択を保ち、描画位置の身体接触をmeshごとに記録する。
 */
bool EmitMeshContactReports(uint32_t frame, const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, const gk::Array<uint32_t>& selectedIds, const gk::Array<FVector3d>& positions, const gk::model::FModelSecondaryMotionState& secondary);
/**
 * 指定frameだけ、実描画indexで構成した身体面と衣服点を独立した診断へ保存する。
 */
void EmitSelectedSkinSurface(uint32_t frame, const gk::detail::ModelResource& resource, const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, const gk::Array<uint32_t>& selectedIds, const gk::model::animation::FModelSparsePoseGeometry& reference, const gk::Array<gk::model::animation::FModelSkinPointCorrection>& corrections, const gk::model::FModelSecondaryMotionState* secondary);

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

    // 元skinの参照比較を保ち、接触だけは予約描画と同じ補正後の位置で調べる。
    gk::Array<gk::model::animation::FModelSkinPointCorrection> corrections;
    if (playback && !gk::model::BuildModelSkinContactSnapshot(*source, playback->secondaryMotion, pose, matrices, corrections, error))
    {
        return FailProbe(frame, 0, error.CStr(), eligibleCount);
    }
    uint32_t correctionIndex = 0;
    for (uint32_t sample = 0; sample < selectedIds.Count(); ++sample)
    {
        while (correctionIndex < corrections.Count() && corrections.At(correctionIndex).positionId < selectedIds.At(sample))
        {
            ++correctionIndex;
        }
        if (correctionIndex < corrections.Count() && corrections.At(correctionIndex).positionId == selectedIds.At(sample))
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                evaluatedPoints.At(sample).value[axis] = corrections.At(correctionIndex).position[axis];
            }
        }
    }
    uint32_t pointPenetrationCount = 0;
    uint32_t pointShapeContactCount = 0;
    double maximumPenetrationDepth = 0.0;
    const gk::model::FModelSecondaryMotionState* secondary = playback ? playback->secondaryMotion : nullptr;
    const char* bindingDumpFlag = getenv("GKCORE_DUMP_SKIN_BINDINGS");
    static bool bindingDumpAttempted = false;
    if (frame == 0 && bindingDumpFlag && strcmp(bindingDumpFlag, "1") == 0 && !bindingDumpAttempted)
    {
        bindingDumpAttempted = true;
        if (!EmitSkinBindingDump(frame, *source, *geometry, selectedIds, secondary, error))
        {
            fprintf(stderr, "model skin binding dump: %s\n", error.Empty() ? "binding dump failed" : error.CStr());
        }
    }
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
    EmitSelectedSkinSurface(frame, *resource, *source, *geometry, selectedIds, reference, corrections, secondary);
    EmitReport(frame, evaluatedPoints.Count(), eligibleCount, shapeCount, maximumError, pointPenetrationCount, pointShapeContactCount, maximumPenetrationDepth, contactTested, contactClear, matchesReference);
    if (contactTested && (getenv("GKCORE_SKIN_POINT_MESH_SUMMARY") || getenv("GKCORE_SKIN_POINT_DETAIL_PENETRATIONS")) && !EmitMeshContactReports(frame, *source, *geometry, selectedIds, evaluatedPoints, *secondary))
    {
        fprintf(stderr, "{\"type\":\"gkcore_model_skin_mesh_frame_error\",\"frame\":%u,\"valid\":false,\"message\":\"mesh contact classification failed\"}\n", frame);
        return 0;
    }
    const char* detailFrameValue = getenv("GKCORE_SKIN_POINT_DETAIL_FRAME");
    if (detailFrameValue && detailFrameValue[0])
    {
        uint32_t detailFrame = 0;
        if (!ParseDetailFrame(detailFrameValue, detailFrame))
        {
            EmitDetailError(frame, "GKCORE_SKIN_POINT_DETAIL_FRAME must be a nonnegative uint32 frame number");
        }
        else if (detailFrame == frame)
        {
            EmitPointDetails(model, frame, *source, *geometry, selectedIds, matrices, reference, secondary);
        }
    }
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

/**
 * 詳細出力の対象frame番号を、32 bit範囲で読み取る。
 */
bool ParseDetailFrame(const char* value, uint32_t& frame)
{
    if (!value || !value[0] || value[0] == '-')
    {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = strtoull(value, &end, 10);
    if (errno == ERANGE || !end || *end != '\0' || parsed > UINT32_MAX)
    {
        return false;
    }
    frame = static_cast<uint32_t>(parsed);
    return true;
}

/**
 * 詳細診断の準備失敗を、skin検証結果と分けて記録する。
 */
void EmitDetailError(uint32_t frame, const char* message)
{
    fprintf(stderr, "{\"type\":\"gkcore_model_skin_point_detail_error\",\"frame\":%u,\"message\":", frame);
    EmitJsonString(message ? message : "unknown diagnostic failure");
    fprintf(stderr, "}\n");
}

/**
 * JSON文字列を引用符と制御文字をescapeして出力する。
 */
void EmitJsonString(const char* value)
{
    fputc('"', stderr);
    if (value)
    {
        for (const unsigned char* character = reinterpret_cast<const unsigned char*>(value); *character; ++character)
        {
            if (*character == '"' || *character == '\\')
            {
                fputc('\\', stderr);
                fputc(*character, stderr);
            }
            else if (*character < 0x20)
            {
                fprintf(stderr, "\\u%04x", static_cast<unsigned int>(*character));
            }
            else
            {
                fputc(*character, stderr);
            }
        }
    }
    fputc('"', stderr);
}

/**
 * 同じboneへの寄与を合計した診断用weight。
 */
struct FDiagnosticBoneWeight
{
    // 共通骨格内のbone番号。
    uint32_t bone;
    // この点に対する正のweightの合計。
    double weight;
};

/**
 * bind位置を同じboneへまとめるskin binding出力用の集計値。
 */
struct FBindingBoneWeight
{
    // 共通骨格内のbone番号。
    uint32_t bone;
    // 全influenceを正規化する前の、このboneのweight合計。
    double weight;
    // geometryToBoneで変換したbind位置のweight合計。
    double localPositionSum[3];
};

/**
 * 身体形状へ入った点と、最も深い接触を保持する。
 */
struct FDiagnosticPoint
{
    // skin geometry内の位置番号。
    uint32_t positionId;
    // 最深の侵入がある身体形状番号。
    uint32_t shapeIndex;
    // 頂点接触を補正する前のmodel空間位置。
    FVector3d position;
    // 点から身体形状の軸までの距離。
    double distance;
    // 身体形状の半径から距離を引いた侵入深さ。
    double depth;
};

/**
 * mesh segmentごとのskin点数と身体形状への侵入を数える。
 */
struct FMeshContactSummary
{
    // 対象mesh segmentに分類できた選択点数。
    uint32_t pointCount;
    // 身体形状へ侵入した選択点数。
    uint32_t pointPenCount;
    // このmesh segment内で最も深い侵入。
    double maxDepth;
};

/**
 * 位置IDが一意に属するmesh segmentを範囲検査して返す。
 */
bool FindPointSegment(const FModelGpuSkinningGeometry& geometry, uint32_t positionId, uint32_t& segmentIndex)
{
    bool found = false;
    for (uint32_t index = 0; index < geometry.segments.Count(); ++index)
    {
        const auto& segment = geometry.segments.At(index);
        if (segment.firstPosition > geometry.positions.Count() || segment.positionCount > geometry.positions.Count() - segment.firstPosition)
        {
            continue;
        }
        if (positionId < segment.firstPosition || positionId - segment.firstPosition >= segment.positionCount)
        {
            continue;
        }
        if (found)
        {
            return false;
        }
        segmentIndex = index;
        found = true;
    }
    return found;
}

/**
 * 面の保存失敗を、通常のskin一致判定と分けて記録する。
 */
void EmitSkinSurfaceError(uint32_t frame, const char* message)
{
    fprintf(stderr, "{\"type\":\"gkcore_model_skin_surface_error\",\"frame\":%u,\"valid\":false,\"message\":", frame);
    EmitJsonString(message);
    fprintf(stderr, "}\n");
}

void EmitSelectedSkinSurface(uint32_t frame, const gk::detail::ModelResource& resource, const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, const gk::Array<uint32_t>& selectedIds, const gk::model::animation::FModelSparsePoseGeometry& reference, const gk::Array<gk::model::animation::FModelSkinPointCorrection>& corrections, const gk::model::FModelSecondaryMotionState* secondary)
{
    // どちらも未指定なら、面の診断用処理は行わない。
    const char* meshName = getenv("GKCORE_SKIN_SURFACE_MESH");
    const char* frameValue = getenv("GKCORE_SKIN_SURFACE_FRAME");
    if (!meshName && !frameValue)
    {
        return;
    }
    // 指定した一つのframeだけ、重い位置と三角形の保存を行う。
    uint32_t surfaceFrame = 0;
    if (!meshName || !meshName[0] || !frameValue || !ParseDetailFrame(frameValue, surfaceFrame))
    {
        if (frame == 0)
        {
            EmitSkinSurfaceError(frame, "GKCORE_SKIN_SURFACE_MESH and a nonnegative uint32 GKCORE_SKIN_SURFACE_FRAME are required");
        }
        return;
    }
    if (frame != surfaceFrame)
    {
        return;
    }
    // node名だけで身体を推測せず、実際に描画されるmesh範囲も保存する。
    for (uint32_t segmentIndex = 0; segmentIndex < geometry.segments.Count(); ++segmentIndex)
    {
        const auto& segment = geometry.segments.At(segmentIndex);
        const char* segmentName = source.BoneName(segment.nodeIndex);
        if (!segmentName)
        {
            EmitSkinSurfaceError(frame, "a drawing mesh segment has no source node name");
            return;
        }
        fprintf(stderr, "{\"type\":\"gkcore_model_skin_surface_mesh\",\"frame\":%u,\"segmentIndex\":%u,\"nodeIndex\":%u,\"firstPosition\":%u,\"positionCount\":%u,\"meshName\":", frame, segmentIndex, segment.nodeIndex, segment.firstPosition, segment.positionCount);
        EmitJsonString(segmentName);
        fprintf(stderr, "}\n");
    }
    // 同名nodeが複数ある場合は、別のmeshを推測して選ばない。
    uint32_t bodyNode = UINT32_MAX;
    for (uint32_t bone = 0; bone < source.Skeleton().parents.Count(); ++bone)
    {
        const char* boneName = source.BoneName(bone);
        if (boneName && strcmp(boneName, meshName) == 0)
        {
            if (bodyNode != UINT32_MAX)
            {
                EmitSkinSurfaceError(frame, "the body mesh node name is ambiguous");
                return;
            }
            bodyNode = bone;
        }
    }
    // 実描画cornerからunique位置への対応を借用し、faceを三角形へ作り直さない。
    const auto* vertexMap = source.SparseVertexMap();
    if (bodyNode == UINT32_MAX || !vertexMap)
    {
        EmitSkinSurfaceError(frame, "the body mesh node or sparse drawing vertex map is unavailable");
        return;
    }
    // 揺れものがない構成も、身体形状なしの面診断として扱う。
    const gk::Array<gk::model::animation::FModelSecondaryMotionCollisionShape> emptyShapes;
    const auto& shapes = secondary ? secondary->previousCollisionShapes : emptyShapes;
    // このframeの身体面と衣服点を、書込み完了まで所有する。
    gk::testing::FModelSkinSurfaceCapture snapshot;
    // 正常に組み立てられた一行だけを出力するための作業領域。
    gk::String output;
    // 通常のskin検証とは別に扱う面診断の失敗理由。
    gk::String error;
    if (!gk::testing::BuildModelSkinSurfaceCapture(geometry, *vertexMap, resource.indices, bodyNode, meshName, reference, corrections, selectedIds, shapes, snapshot, error) || !gk::testing::EncodeModelSkinSurfaceCapture(frame, snapshot, output, error))
    {
        EmitSkinSurfaceError(frame, error.Empty() ? "body surface capture failed" : error.CStr());
        return;
    }
    // 全行を組み立てられた場合だけ書き込み、途中までのJSONを残さない。
    fputs(output.CStr(), stderr);
    if (output.Empty() || output.CStr()[output.Length() - 1u] != '\n')
    {
        fputc('\n', stderr);
    }
    // 面の所属部位を調べる場合だけ、元body位置の全bone寄与も既存形式で保存する。
    const char* bindingsFlag = getenv("GKCORE_SKIN_SURFACE_BINDINGS");
    if (bindingsFlag && strcmp(bindingsFlag, "1") == 0)
    {
        // body位置を重複なく要求し、三角形cornerの重複出力を避ける。
        gk::Array<uint32_t> bodyIds;
        bool prepared = bodyIds.Reserve(snapshot.bodyPoints.Count());
        for (uint32_t index = 0; prepared && index < snapshot.bodyPoints.Count(); ++index)
        {
            prepared = bodyIds.Append(snapshot.bodyPoints.At(index).positionId);
        }
        if (!prepared || !EmitSkinBindingDump(frame, source, geometry, bodyIds, secondary, error))
        {
            EmitSkinSurfaceError(frame, !prepared ? "body binding ID allocation failed" : error.CStr());
        }
    }
}

bool EmitMeshContactReports(uint32_t frame, const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, const gk::Array<uint32_t>& selectedIds, const gk::Array<FVector3d>& positions, const gk::model::FModelSecondaryMotionState& secondary)
{
    // 描画と同じ位置の侵入点だけを、frameを限定せず記録する。
    const char* penetrationFlag = getenv("GKCORE_SKIN_POINT_DETAIL_PENETRATIONS");
    const bool emitPenetrations = penetrationFlag && strcmp(penetrationFlag, "1") == 0;
    gk::Array<FMeshContactSummary> summaries;
    if (positions.Count() != selectedIds.Count() || !summaries.Reserve(geometry.segments.Count()))
    {
        return false;
    }
    for (uint32_t segmentIndex = 0; segmentIndex < geometry.segments.Count(); ++segmentIndex)
    {
        if (!summaries.Append({}))
        {
            return false;
        }
    }
    for (uint32_t sample = 0; sample < selectedIds.Count(); ++sample)
    {
        uint32_t segmentIndex = 0;
        if (!FindPointSegment(geometry, selectedIds.At(sample), segmentIndex))
        {
            return false;
        }
        auto& summary = summaries.At(segmentIndex);
        ++summary.pointCount;
        double maximumDepth = 0.0;
        uint32_t deepestShapeIndex = 0;
        for (uint32_t shapeIndex = 0; shapeIndex < secondary.previousCollisionShapes.Count(); ++shapeIndex)
        {
            double distance = 0.0;
            const auto& shape = secondary.previousCollisionShapes.At(shapeIndex);
            if (!DistanceToShape(positions.At(sample), shape, distance))
            {
                return false;
            }
            const double depth = static_cast<double>(shape.radius) - distance;
            if (depth > maximumDepth)
            {
                maximumDepth = depth;
                deepestShapeIndex = shapeIndex;
            }
        }
        if (maximumDepth > 0.0)
        {
            ++summary.pointPenCount;
            summary.maxDepth = fmax(summary.maxDepth, maximumDepth);
            if (emitPenetrations)
            {
                const auto& segment = geometry.segments.At(segmentIndex);
                if (segment.nodeIndex >= source.Skeleton().parents.Count())
                {
                    return false;
                }
                const char* meshName = source.BoneName(segment.nodeIndex);
                if (!meshName)
                {
                    return false;
                }
                fprintf(stderr, "{\"type\":\"gkcore_model_skin_penetration_frame\",\"frame\":%u,\"positionId\":%u,\"segmentIndex\":%u,\"shapeIndex\":%u,\"positionSource\":\"rendered skin\",\"meshName\":", frame, selectedIds.At(sample), segmentIndex, deepestShapeIndex);
                EmitJsonString(meshName);
                const auto& position = positions.At(sample);
                fprintf(stderr, ",\"position\":[%.17g,%.17g,%.17g],\"maxDepth\":%.17g,\"valid\":true}\n", position.value[0], position.value[1], position.value[2], maximumDepth);
            }
        }
    }
    for (uint32_t segmentIndex = 0; segmentIndex < summaries.Count(); ++segmentIndex)
    {
        const auto& summary = summaries.At(segmentIndex);
        const auto& segment = geometry.segments.At(segmentIndex);
        if (segment.nodeIndex >= source.Skeleton().parents.Count() || !source.BoneName(segment.nodeIndex))
        {
            return false;
        }
        if (summary.pointCount == 0)
        {
            continue;
        }
        fprintf(stderr, "{\"type\":\"gkcore_model_skin_mesh_frame\",\"frame\":%u,\"segmentIndex\":%u,\"meshNode\":%u,\"positionSource\":\"rendered skin\",\"meshName\":", frame, segmentIndex, segment.nodeIndex);
        EmitJsonString(source.BoneName(segment.nodeIndex));
        fprintf(stderr, ",\"pointCount\":%u,\"pointPenCount\":%u,\"maxDepth\":%.17g,\"valid\":true}\n", summary.pointCount, summary.pointPenCount, summary.maxDepth);
    }
    return true;
}

/**
 * 指定boneが登録Skirt鎖のrootなら1、childなら2、対象外なら0を返す。
 */
uint32_t GetSkirtChainBoneRole(const gk::model::AModelAnimationSource& source, const gk::model::FModelSecondaryMotionState& secondary, uint32_t bone)
{
    for (uint32_t chainIndex = 0; chainIndex < secondary.chains.Count(); ++chainIndex)
    {
        const auto* chain = secondary.chains.At(chainIndex);
        if (!chain || chain->bones.Count() == 0)
        {
            continue;
        }
        const char* rootName = source.BoneName(chain->bones.At(0));
        if (!rootName || strncmp(rootName, "Skirt", 5) != 0)
        {
            continue;
        }
        for (uint32_t boneIndex = 0; boneIndex < chain->bones.Count(); ++boneIndex)
        {
            if (chain->bones.At(boneIndex) == bone)
            {
                return boneIndex == 0 ? 1u : 2u;
            }
        }
    }
    return 0;
}

/**
 * 身体shape取付boneまたはそのskeleton祖先に属するskin weightか調べる。
 */
bool IsBodyShapeBone(const gk::model::AModelAnimationSource& source, const gk::model::FModelSecondaryMotionState& secondary, uint32_t bone)
{
    const auto& parents = source.Skeleton().parents;
    for (uint32_t colliderIndex = 0; colliderIndex < secondary.colliders.Count(); ++colliderIndex)
    {
        if (secondary.colliders.At(colliderIndex).bone > static_cast<uint32_t>(INT_MAX))
        {
            continue;
        }
        int32_t ancestor = static_cast<int32_t>(secondary.colliders.At(colliderIndex).bone);
        uint32_t visited = 0;
        while (ancestor >= 0 && static_cast<uint32_t>(ancestor) < parents.Count() && visited++ < parents.Count())
        {
            if (static_cast<uint32_t>(ancestor) == bone)
            {
                return true;
            }
            ancestor = parents.At(static_cast<uint32_t>(ancestor));
        }
    }
    return false;
}

/**
 * 侵入点のbone weight内訳と上位4本の寄与を集計する。
 */
bool CollectPointWeights(const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, const gk::model::FModelSecondaryMotionState& secondary, uint32_t positionId, double& rootWeight, double& childWeight, double& bodyWeight, double& otherWeight, double& totalWeight, gk::Array<FDiagnosticBoneWeight>& boneWeights, gk::String& error)
{
    if (positionId >= geometry.influenceRanges.Count())
    {
        error.Assign("detail point has no influence range");
        return false;
    }
    const auto& range = geometry.influenceRanges.At(positionId);
    if (range.firstInfluence > geometry.influences.Count() || range.influenceCount > geometry.influences.Count() - range.firstInfluence || !boneWeights.Reserve(range.influenceCount))
    {
        error.Assign("detail point influence range is invalid or allocation failed");
        return false;
    }
    rootWeight = 0.0;
    childWeight = 0.0;
    bodyWeight = 0.0;
    otherWeight = 0.0;
    totalWeight = 0.0;
    boneWeights.Clear();
    for (uint32_t offset = 0; offset < range.influenceCount; ++offset)
    {
        const auto& influence = geometry.influences.At(range.firstInfluence + offset);
        if (!isfinite(influence.weight) || influence.weight < 0.0 || influence.clusterIndex >= geometry.clusters.Count())
        {
            error.Assign("detail point has an invalid influence");
            return false;
        }
        if (!(influence.weight > 0.0))
        {
            continue;
        }
        const uint32_t bone = geometry.clusters.At(influence.clusterIndex).nodeIndex;
        if (bone >= source.Skeleton().parents.Count())
        {
            error.Assign("detail point influence references an invalid bone");
            return false;
        }
        const uint32_t role = GetSkirtChainBoneRole(source, secondary, bone);
        if (role == 1)
        {
            rootWeight += influence.weight;
        }
        else if (role == 2)
        {
            childWeight += influence.weight;
        }
        else if (IsBodyShapeBone(source, secondary, bone))
        {
            bodyWeight += influence.weight;
        }
        else
        {
            otherWeight += influence.weight;
        }
        totalWeight += influence.weight;

        uint32_t found = boneWeights.Count();
        for (uint32_t index = 0; index < boneWeights.Count(); ++index)
        {
            if (boneWeights.At(index).bone == bone)
            {
                found = index;
                break;
            }
        }
        if (found < boneWeights.Count())
        {
            boneWeights.At(found).weight += influence.weight;
        }
        else if (!boneWeights.Append({ bone, influence.weight }))
        {
            error.Assign("detail point bone weight allocation failed");
            return false;
        }
    }
    return true;
}

/**
 * 選択頂点の正weightをbone単位でまとめ、bind位置とbone-local bind座標をJSONへ出す。
 */
bool EmitSkinBindingDump(uint32_t frame, const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, const gk::Array<uint32_t>& selectedIds, const gk::model::FModelSecondaryMotionState* secondary, gk::String& error)
{
    fprintf(stderr, "{\"type\":\"gkcore_skin_binding_dump\",\"frame\":%u,\"pointCount\":%u}\n", frame, selectedIds.Count());
    for (uint32_t sample = 0; sample < selectedIds.Count(); ++sample)
    {
        const uint32_t positionId = selectedIds.At(sample);
        if (positionId >= geometry.positions.Count() || positionId >= geometry.influenceRanges.Count())
        {
            error.Assign("binding dump position id is outside geometry arrays");
            return false;
        }
        const auto& range = geometry.influenceRanges.At(positionId);
        if (range.firstInfluence > geometry.influences.Count() || range.influenceCount > geometry.influences.Count() - range.firstInfluence)
        {
            error.Assign("binding dump influence range is invalid");
            return false;
        }
        gk::Array<FBindingBoneWeight> bindings;
        if (!bindings.Reserve(range.influenceCount))
        {
            error.Assign("binding dump bone allocation failed");
            return false;
        }
        double totalWeight = 0.0;
        for (uint32_t influenceOffset = 0; influenceOffset < range.influenceCount; ++influenceOffset)
        {
            const auto& influence = geometry.influences.At(range.firstInfluence + influenceOffset);
            if (!isfinite(influence.weight) || influence.weight < 0.0 || influence.clusterIndex >= geometry.clusters.Count())
            {
                error.Assign("binding dump contains an invalid influence");
                return false;
            }
            if (!(influence.weight > 0.0))
            {
                continue;
            }
            const auto& cluster = geometry.clusters.At(influence.clusterIndex);
            if (cluster.nodeIndex >= source.Skeleton().parents.Count())
            {
                error.Assign("binding dump cluster references an invalid bone");
                return false;
            }
            const auto& position = geometry.positions.At(positionId);
            const double localPosition[3] = { cluster.geometryToBone[0] * position.value[0] + cluster.geometryToBone[3] * position.value[1] + cluster.geometryToBone[6] * position.value[2] + cluster.geometryToBone[9], cluster.geometryToBone[1] * position.value[0] + cluster.geometryToBone[4] * position.value[1] + cluster.geometryToBone[7] * position.value[2] + cluster.geometryToBone[10], cluster.geometryToBone[2] * position.value[0] + cluster.geometryToBone[5] * position.value[1] + cluster.geometryToBone[8] * position.value[2] + cluster.geometryToBone[11] };
            if (!isfinite(localPosition[0]) || !isfinite(localPosition[1]) || !isfinite(localPosition[2]))
            {
                error.Assign("binding dump bind-local position is non-finite");
                return false;
            }
            uint32_t bindingIndex = bindings.Count();
            for (uint32_t index = 0; index < bindings.Count(); ++index)
            {
                if (bindings.At(index).bone == cluster.nodeIndex)
                {
                    bindingIndex = index;
                    break;
                }
            }
            if (bindingIndex == bindings.Count())
            {
                const FBindingBoneWeight emptyBinding{};
                if (!bindings.Append(emptyBinding))
                {
                    error.Assign("binding dump bone append failed");
                    return false;
                }
                bindings.At(bindingIndex).bone = cluster.nodeIndex;
            }
            FBindingBoneWeight& binding = bindings.At(bindingIndex);
            binding.weight += influence.weight;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                binding.localPositionSum[axis] += localPosition[axis] * influence.weight;
            }
            totalWeight += influence.weight;
            if (!isfinite(binding.weight) || !isfinite(totalWeight) || !isfinite(binding.localPositionSum[0]) || !isfinite(binding.localPositionSum[1]) || !isfinite(binding.localPositionSum[2]))
            {
                error.Assign("binding dump weight accumulation overflowed");
                return false;
            }
        }
        if (!(totalWeight > 0.0))
        {
            error.Assign("binding dump point has no positive influence weight");
            return false;
        }
        const auto& bindPosition = geometry.positions.At(positionId);
        // 衣服ごとに対象を選べるよう、位置IDが属する描画meshも記録する。
        uint32_t segmentIndex = 0;
        if (!FindPointSegment(geometry, positionId, segmentIndex))
        {
            error.Assign("binding dump point does not belong to one mesh segment");
            return false;
        }
        const auto& segment = geometry.segments.At(segmentIndex);
        if (segment.nodeIndex >= source.Skeleton().parents.Count() || !source.BoneName(segment.nodeIndex))
        {
            error.Assign("binding dump mesh node is invalid");
            return false;
        }
        fprintf(stderr, "{\"type\":\"gkcore_skin_binding_point\",\"frame\":%u,\"positionId\":%u,\"segmentIndex\":%u,\"meshNode\":%u,\"meshName\":", frame, positionId, segmentIndex, segment.nodeIndex);
        EmitJsonString(source.BoneName(segment.nodeIndex));
        fprintf(stderr, ",\"meshPositionId\":%u,\"bindPosition\":[%.17g,%.17g,%.17g],\"originalWeightSum\":%.17g,\"bindings\":[", positionId - segment.firstPosition, bindPosition.value[0], bindPosition.value[1], bindPosition.value[2], totalWeight);
        for (uint32_t index = 0; index < bindings.Count(); ++index)
        {
            if (index > 0)
            {
                fputc(',', stderr);
            }
            const FBindingBoneWeight& binding = bindings.At(index);
            const char* group = "other";
            if (secondary)
            {
                const uint32_t role = GetSkirtChainBoneRole(source, *secondary, binding.bone);
                group = role == 1 ? "skirtRoot" : (role == 2 ? "skirtChild" : (IsBodyShapeBone(source, *secondary, binding.bone) ? "bodyShapeAncestor" : "other"));
            }
            fprintf(stderr, "{\"bone\":%u,\"name\":", binding.bone);
            EmitJsonString(source.BoneName(binding.bone));
            fprintf(stderr, ",\"group\":\"%s\",\"weight\":%.17g,\"localPosition\":[%.17g,%.17g,%.17g]}", group, binding.weight / totalWeight, binding.localPositionSum[0] / binding.weight, binding.localPositionSum[1] / binding.weight, binding.localPositionSum[2] / binding.weight);
        }
        fprintf(stderr, "]}\n");
    }
    return true;
}

/**
 * 指定boneのcluster変形位置をweight平均し、有限値なら返す。
 */
bool GetBoneTransformedCentroid(const FModelGpuSkinningGeometry& geometry, const gk::Array<FModelGpuSkinningGeometry::FMatrix>& matrices, uint32_t positionId, uint32_t bone, double output[3])
{
    if (positionId >= geometry.positions.Count() || positionId >= geometry.influenceRanges.Count())
    {
        return false;
    }
    const auto& position = geometry.positions.At(positionId);
    const auto& range = geometry.influenceRanges.At(positionId);
    if (range.firstInfluence > geometry.influences.Count() || range.influenceCount > geometry.influences.Count() - range.firstInfluence)
    {
        return false;
    }
    output[0] = 0.0;
    output[1] = 0.0;
    output[2] = 0.0;
    double totalWeight = 0.0;
    for (uint32_t offset = 0; offset < range.influenceCount; ++offset)
    {
        const auto& influence = geometry.influences.At(range.firstInfluence + offset);
        if (!isfinite(influence.weight) || influence.weight < 0.0 || influence.clusterIndex >= geometry.clusters.Count() || influence.clusterIndex >= matrices.Count())
        {
            return false;
        }
        if (!(influence.weight > 0.0) || geometry.clusters.At(influence.clusterIndex).nodeIndex != bone)
        {
            continue;
        }
        const auto& matrix = matrices.At(influence.clusterIndex);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const uint32_t row = axis * 4u;
            const double transformed = matrix.value[row] * position.value[0] + matrix.value[row + 1u] * position.value[1] + matrix.value[row + 2u] * position.value[2] + matrix.value[row + 3u];
            output[axis] += transformed * influence.weight;
            if (!isfinite(transformed) || !isfinite(output[axis]))
            {
                return false;
            }
        }
        totalWeight += influence.weight;
        if (!isfinite(totalWeight))
        {
            return false;
        }
    }
    if (!(totalWeight > 0.0))
    {
        return false;
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        output[axis] /= totalWeight;
        if (!isfinite(output[axis]))
        {
            return false;
        }
    }
    return true;
}

/**
 * 対象frameの侵入位置を最大32個に絞り、寄与weightと身体・骨球形状を出力する。
 */
void EmitPointDetails(gk::ModelHandle model, uint32_t frame, const gk::model::AModelAnimationSource& source, const FModelGpuSkinningGeometry& geometry, const gk::Array<uint32_t>& selectedIds, const gk::Array<FModelGpuSkinningGeometry::FMatrix>& matrices, const gk::model::animation::FModelSparsePoseGeometry& reference, const gk::model::FModelSecondaryMotionState* secondary)
{
    if (!secondary || secondary->previousCollisionShapes.Count() != secondary->colliders.Count())
    {
        EmitDetailError(frame, "secondary body shapes are unavailable or inconsistent");
        return;
    }
    gk::Array<gk::FModelSecondaryMotionDebugShape> debugShapes;
    const int32_t requiredDebugShapes = gk::GetModelSecondaryMotionDebugShapes(model, nullptr, 0);
    bool hasDebugShapes = requiredDebugShapes >= 0 && debugShapes.Reserve(static_cast<uint32_t>(requiredDebugShapes));
    if (hasDebugShapes)
    {
        const gk::FModelSecondaryMotionDebugShape emptyShape{};
        for (int32_t index = 0; index < requiredDebugShapes; ++index)
        {
            if (!debugShapes.Append(emptyShape))
            {
                hasDebugShapes = false;
                break;
            }
        }
    }
    if (hasDebugShapes && gk::GetModelSecondaryMotionDebugShapes(model, debugShapes.Data(), debugShapes.Count()) != static_cast<int32_t>(debugShapes.Count()))
    {
        hasDebugShapes = false;
    }
    if (!hasDebugShapes)
    {
        EmitDetailError(frame, "current drawing FK debug shapes could not be read");
    }

    gk::Array<FDiagnosticPoint> details;
    if (!details.Reserve(32))
    {
        EmitDetailError(frame, "detail point allocation failed");
        return;
    }
    uint32_t penetratingPointCount = 0;
    gk::Array<FMeshContactSummary> meshSummaries;
    bool meshSummaryValid = meshSummaries.Reserve(geometry.segments.Count());
    if (meshSummaryValid)
    {
        for (uint32_t segmentIndex = 0; segmentIndex < geometry.segments.Count(); ++segmentIndex)
        {
            const FMeshContactSummary emptySummary{};
            if (!meshSummaries.Append(emptySummary))
            {
                meshSummaryValid = false;
                break;
            }
        }
    }
    if (!meshSummaryValid)
    {
        EmitDetailError(frame, "mesh contact summary allocation failed");
    }
    for (uint32_t sample = 0; sample < selectedIds.Count(); ++sample)
    {
        const uint32_t positionId = selectedIds.At(sample);
        if (positionId >= reference.positions.Count())
        {
            EmitDetailError(frame, "detail position id exceeds sparse skin positions");
            return;
        }
        const auto& referencePosition = reference.positions.At(positionId);
        const FVector3d position = { { referencePosition.value[0], referencePosition.value[1], referencePosition.value[2] } };
        FDiagnosticPoint candidate{};
        candidate.positionId = positionId;
        candidate.shapeIndex = UINT32_MAX;
        candidate.position = position;
        for (uint32_t shapeIndex = 0; shapeIndex < secondary->previousCollisionShapes.Count(); ++shapeIndex)
        {
            double distance = 0.0;
            if (!(secondary->previousCollisionShapes.At(shapeIndex).radius > 0.0f) || !DistanceToShape(position, secondary->previousCollisionShapes.At(shapeIndex), distance))
            {
                EmitDetailError(frame, "detail point or collision shape is invalid");
                return;
            }
            const double depth = static_cast<double>(secondary->previousCollisionShapes.At(shapeIndex).radius) - distance;
            if (depth > candidate.depth)
            {
                candidate.shapeIndex = shapeIndex;
                candidate.distance = distance;
                candidate.depth = depth;
            }
        }
        if (meshSummaryValid)
        {
            uint32_t segmentIndex = 0;
            if (!FindPointSegment(geometry, positionId, segmentIndex) || segmentIndex >= meshSummaries.Count())
            {
                meshSummaryValid = false;
                EmitDetailError(frame, "a selected skin point could not be assigned to exactly one mesh segment");
            }
            else
            {
                const auto& segment = geometry.segments.At(segmentIndex);
                if (segment.nodeIndex >= source.Skeleton().parents.Count() || !source.BoneName(segment.nodeIndex))
                {
                    meshSummaryValid = false;
                    EmitDetailError(frame, "a mesh segment has no valid skeleton bone name");
                }
                else
                {
                    auto& summary = meshSummaries.At(segmentIndex);
                    ++summary.pointCount;
                    if (candidate.depth > 0.0)
                    {
                        ++summary.pointPenCount;
                        summary.maxDepth = fmax(summary.maxDepth, candidate.depth);
                    }
                }
            }
        }
        if (!(candidate.depth > 0.0))
        {
            continue;
        }
        ++penetratingPointCount;
        if (details.Count() < 32)
        {
            if (!details.Append(candidate))
            {
                EmitDetailError(frame, "detail point list allocation failed");
                return;
            }
            continue;
        }
        uint32_t smallestIndex = 0;
        for (uint32_t index = 1; index < details.Count(); ++index)
        {
            if (details.At(index).depth < details.At(smallestIndex).depth || (details.At(index).depth == details.At(smallestIndex).depth && details.At(index).positionId > details.At(smallestIndex).positionId))
            {
                smallestIndex = index;
            }
        }
        if (candidate.depth > details.At(smallestIndex).depth || (candidate.depth == details.At(smallestIndex).depth && candidate.positionId < details.At(smallestIndex).positionId))
        {
            details.At(smallestIndex) = candidate;
        }
    }
    for (uint32_t index = 0; index < details.Count(); ++index)
    {
        uint32_t largestIndex = index;
        for (uint32_t candidate = index + 1; candidate < details.Count(); ++candidate)
        {
            if (details.At(candidate).depth > details.At(largestIndex).depth || (details.At(candidate).depth == details.At(largestIndex).depth && details.At(candidate).positionId < details.At(largestIndex).positionId))
            {
                largestIndex = candidate;
            }
        }
        if (largestIndex != index)
        {
            const FDiagnosticPoint temporary = details.At(index);
            details.At(index) = details.At(largestIndex);
            details.At(largestIndex) = temporary;
        }
    }

    fprintf(stderr, "{\"type\":\"gkcore_model_skin_point_detail\",\"frame\":%u,\"vertexContactsEnabled\":%s,\"pointPositionSource\":\"pre-contact sparse skin\",\"spherePositionSource\":\"current drawing FK\",\"bodyWeightMeaning\":\"collider attachments and their skeleton ancestors\",\"scannedPoints\":%u,\"penetratingPointsBeforeContact\":%u,\"reportedPoints\":%u,\"maxReportedPoints\":32}\n", frame, secondary->skinPointContactsEnabled ? "true" : "false", selectedIds.Count(), penetratingPointCount, details.Count());
    fprintf(stderr, "{\"type\":\"gkcore_model_skin_mesh_contact_summary\",\"frame\":%u,\"valid\":%s,\"meshCount\":%u}\n", frame, meshSummaryValid ? "true" : "false", geometry.segments.Count());
    for (uint32_t segmentIndex = 0; segmentIndex < meshSummaries.Count(); ++segmentIndex)
    {
        const auto& segment = geometry.segments.At(segmentIndex);
        fprintf(stderr, "{\"type\":\"gkcore_model_skin_mesh_contact\",\"frame\":%u,\"meshNode\":%u,\"meshName\":", frame, segment.nodeIndex);
        EmitJsonString(segment.nodeIndex < source.Skeleton().parents.Count() ? source.BoneName(segment.nodeIndex) : nullptr);
        fprintf(stderr, ",\"pointCount\":%u,\"pointPenCount\":%u,\"maxDepth\":%.17g}\n", meshSummaries.At(segmentIndex).pointCount, meshSummaries.At(segmentIndex).pointPenCount, meshSummaries.At(segmentIndex).maxDepth);
    }
    gk::Array<FDiagnosticBoneWeight> boneWeights;
    for (uint32_t detailIndex = 0; detailIndex < details.Count(); ++detailIndex)
    {
        const FDiagnosticPoint& detail = details.At(detailIndex);
        double rootWeight = 0.0;
        double childWeight = 0.0;
        double bodyWeight = 0.0;
        double otherWeight = 0.0;
        double totalWeight = 0.0;
        gk::String error;
        if (!CollectPointWeights(source, geometry, *secondary, detail.positionId, rootWeight, childWeight, bodyWeight, otherWeight, totalWeight, boneWeights, error))
        {
            EmitDetailError(frame, error.CStr());
            continue;
        }
        const auto& shape = secondary->previousCollisionShapes.At(detail.shapeIndex);
        const auto& collider = secondary->colliders.At(detail.shapeIndex);
        const char* shapeBoneName = source.BoneName(collider.bone);
        uint32_t segmentIndex = 0;
        const bool hasSegment = FindPointSegment(geometry, detail.positionId, segmentIndex);
        fprintf(stderr, "{\"type\":\"gkcore_model_skin_point\",\"frame\":%u,\"positionId\":%u,\"position\":[%.17g,%.17g,%.17g],\"meshSegment\":", frame, detail.positionId, detail.position.value[0], detail.position.value[1], detail.position.value[2]);
        if (hasSegment)
        {
            const auto& segment = geometry.segments.At(segmentIndex);
            fprintf(stderr, "{\"segmentIndex\":%u,\"meshNode\":%u,\"meshNodeName\":", segmentIndex, segment.nodeIndex);
            EmitJsonString(segment.nodeIndex < source.Skeleton().parents.Count() ? source.BoneName(segment.nodeIndex) : nullptr);
            fprintf(stderr, ",\"localPositionId\":%u}", detail.positionId - segment.firstPosition);
        }
        else
        {
            fprintf(stderr, "null");
        }
        fprintf(stderr, ",\"deepestShape\":{\"shapeIndex\":%u,\"bone\":%u,\"boneName\":", detail.shapeIndex, collider.bone);
        EmitJsonString(shapeBoneName);
        fprintf(stderr, ",\"kind\":\"%s\",\"start\":[%.17g,%.17g,%.17g],\"end\":[%.17g,%.17g,%.17g],\"radius\":%.9g,\"distance\":%.17g,\"depth\":%.17g},\"weights\":{\"sum\":%.17g,\"skirtRoot\":%.17g,\"skirtChild\":%.17g,\"bodyShapeAncestor\":%.17g,\"other\":%.17g,\"rootShare\":%.9g,\"childShare\":%.9g,\"bodyShapeAncestorShare\":%.9g,\"otherShare\":%.9g},\"topInfluences\":[", shape.start.x == shape.end.x && shape.start.y == shape.end.y && shape.start.z == shape.end.z ? "sphere" : "capsule", shape.start.x, shape.start.y, shape.start.z, shape.end.x, shape.end.y, shape.end.z, shape.radius, detail.distance, detail.depth, totalWeight, rootWeight, childWeight, bodyWeight, otherWeight, totalWeight > 0.0 ? rootWeight / totalWeight : 0.0, totalWeight > 0.0 ? childWeight / totalWeight : 0.0, totalWeight > 0.0 ? bodyWeight / totalWeight : 0.0, totalWeight > 0.0 ? otherWeight / totalWeight : 0.0);

        FDiagnosticBoneWeight topInfluences[4]{};
        uint32_t topInfluenceCount = 0;
        for (uint32_t weightIndex = 0; weightIndex < boneWeights.Count(); ++weightIndex)
        {
            const FDiagnosticBoneWeight candidate = boneWeights.At(weightIndex);
            uint32_t insertIndex = topInfluenceCount;
            for (uint32_t index = 0; index < topInfluenceCount; ++index)
            {
                if (candidate.weight > topInfluences[index].weight || (candidate.weight == topInfluences[index].weight && candidate.bone < topInfluences[index].bone))
                {
                    insertIndex = index;
                    break;
                }
            }
            if (insertIndex >= 4)
            {
                continue;
            }
            const uint32_t newCount = topInfluenceCount < 4 ? topInfluenceCount + 1 : topInfluenceCount;
            for (uint32_t shift = newCount; shift > insertIndex + 1; --shift)
            {
                topInfluences[shift - 1] = topInfluences[shift - 2];
            }
            topInfluences[insertIndex] = candidate;
            topInfluenceCount = newCount;
        }
        for (uint32_t index = 0; index < topInfluenceCount; ++index)
        {
            if (index > 0)
            {
                fputc(',', stderr);
            }
            const uint32_t bone = topInfluences[index].bone;
            const uint32_t role = GetSkirtChainBoneRole(source, *secondary, bone);
            const char* roleName = role == 1 ? "skirtRoot" : (role == 2 ? "skirtChild" : (IsBodyShapeBone(source, *secondary, bone) ? "bodyShapeAncestor" : "other"));
            double transformedPosition[3]{};
            const bool hasTransformedPosition = GetBoneTransformedCentroid(geometry, matrices, detail.positionId, bone, transformedPosition);
            fprintf(stderr, "{\"bone\":%u,\"name\":", bone);
            EmitJsonString(source.BoneName(bone));
            fprintf(stderr, ",\"weight\":%.17g,\"group\":\"%s\",\"transformedPosition\":", topInfluences[index].weight, roleName);
            if (hasTransformedPosition)
            {
                fprintf(stderr, "[%.17g,%.17g,%.17g]}", transformedPosition[0], transformedPosition[1], transformedPosition[2]);
            }
            else
            {
                fprintf(stderr, "null}");
            }
        }
        fprintf(stderr, "],\"skirtBoneSpheres\":[");
        bool firstSphere = true;
        if (hasDebugShapes)
        {
            for (uint32_t weightIndex = 0; weightIndex < boneWeights.Count(); ++weightIndex)
            {
                const FDiagnosticBoneWeight& boneWeight = boneWeights.At(weightIndex);
                if (GetSkirtChainBoneRole(source, *secondary, boneWeight.bone) == 0)
                {
                    continue;
                }
                for (uint32_t debugIndex = 0; debugIndex < debugShapes.Count(); ++debugIndex)
                {
                    const auto& debugShape = debugShapes.At(debugIndex);
                    if (debugShape.body || debugShape.virtualTip || debugShape.bone != boneWeight.bone)
                    {
                        continue;
                    }
                    const FVector3d center = { { debugShape.start.x, debugShape.start.y, debugShape.start.z } };
                    const double difference[3] = { detail.position.value[0] - center.value[0], detail.position.value[1] - center.value[1], detail.position.value[2] - center.value[2] };
                    const double sphereDistance = VectorLength(difference);
                    if (!firstSphere)
                    {
                        fputc(',', stderr);
                    }
                    firstSphere = false;
                    const double centerX = debugShape.start.x;
                    const double centerY = debugShape.start.y;
                    const double centerZ = debugShape.start.z;
                    fprintf(stderr, "{\"bone\":%u,\"name\":", debugShape.bone);
                    EmitJsonString(source.BoneName(debugShape.bone));
                    fprintf(stderr, ",\"weight\":%.17g,\"center\":[%.17g,%.17g,%.17g],\"radius\":%.9g,\"distance\":%.17g,\"clearance\":%.17g}", boneWeight.weight, centerX, centerY, centerZ, debugShape.radius, sphereDistance, sphereDistance - debugShape.radius);
                    break;
                }
            }
        }
        fprintf(stderr, "]}\n");
    }
}

}
