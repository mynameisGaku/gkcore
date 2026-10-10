// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionColliders.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include <float.h>
#include <math.h>

namespace gk::model
{
namespace
{
/**
 * boneのローカル端点を、有限なmodel空間位置へ変換する。
 */
bool TransformEndpoint(const float* matrix, Vec3 local, Vec3& output)
{
    // 行列の平行移動と3軸から求めるXYZ位置。
    const double values[3] = { static_cast<double>(matrix[12]) + matrix[0] * static_cast<double>(local.x) + matrix[4] * static_cast<double>(local.y) + matrix[8] * static_cast<double>(local.z), static_cast<double>(matrix[13]) + matrix[1] * static_cast<double>(local.x) + matrix[5] * static_cast<double>(local.y) + matrix[9] * static_cast<double>(local.z), static_cast<double>(matrix[14]) + matrix[2] * static_cast<double>(local.x) + matrix[6] * static_cast<double>(local.y) + matrix[10] * static_cast<double>(local.z) };
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(values[axis]) || fabs(values[axis]) > FLT_MAX)
        {
            return false;
        }
    }
    output = { static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2]) };
    return true;
}
}

bool EvaluateSecondaryMotionColliders(const animation::FModelSkeleton& skeleton, const animation::FModelPose& pose, const Array<float>& matrices, const FModelSecondaryMotionCollider* colliders, uint32_t count, float margin, Array<animation::FModelSecondaryMotionCollisionShape>& output, String& error)
{
    if (count > 64 || (count > 0 && !colliders) || !isfinite(margin) || margin < 0.0f || pose.localTransforms.Count() != skeleton.parents.Count() || skeleton.parents.Count() > UINT32_MAX / 16u || matrices.Count() != skeleton.parents.Count() * 16u)
    {
        error.Assign("secondary motion collider input is invalid");
        return false;
    }
    // 全形状の計算が済むまで、既存の配置を変更しない。
    Array<animation::FModelSecondaryMotionCollisionShape> candidate;
    for (uint32_t index = 0; index < count; ++index)
    {
        // 今回配置する身体のboneとローカル形状。
        const auto& collider = colliders[index];
        if (collider.bone >= skeleton.parents.Count() || !isfinite(collider.radius) || !(collider.radius > 0.0f))
        {
            error.Assign("secondary motion collider bone or radius is invalid");
            return false;
        }
        // 球を楕円体へ変えるscaleは扱わず、小さな入力軸差だけを許す。
        uint32_t visited = 0;
        for (int32_t ancestor = static_cast<int32_t>(collider.bone); ancestor >= 0; ancestor = skeleton.parents.At(static_cast<uint32_t>(ancestor)))
        {
            if (static_cast<uint32_t>(ancestor) >= skeleton.parents.Count() || ++visited > skeleton.parents.Count())
            {
                error.Assign("secondary motion collider hierarchy is invalid");
                return false;
            }
            const auto& transform = pose.localTransforms.At(static_cast<uint32_t>(ancestor));
            const float smallest = fminf(transform.scale[0], fminf(transform.scale[1], transform.scale[2]));
            const float largest = fmaxf(transform.scale[0], fmaxf(transform.scale[1], transform.scale[2]));
            if (!isfinite(smallest) || !isfinite(largest) || !(smallest > 0.0f) || largest - smallest > largest * (128.0f * FLT_EPSILON))
            {
                error.Assign("secondary motion collider requires positive near-uniform ancestor scale");
                return false;
            }
        }
        // 配置に使う身体のworld行列と、微小な軸差を含めた最大の拡大率。
        const float* matrix = matrices.Data() + collider.bone * 16u;
        double largestScale = 0.0;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const uint32_t offset = axis * 4u;
            const double scale = sqrt(static_cast<double>(matrix[offset]) * matrix[offset] + static_cast<double>(matrix[offset + 1u]) * matrix[offset + 1u] + static_cast<double>(matrix[offset + 2u]) * matrix[offset + 2u]);
            if (!isfinite(scale) || !(scale > 0.0))
            {
                error.Assign("secondary motion collider matrix scale is invalid");
                return false;
            }
            largestScale = fmax(largestScale, scale);
        }
        // 半径はboneに追従し、節の余白はmodel空間の長さで加える。
        const double radius = collider.radius * largestScale + margin;
        animation::FModelSecondaryMotionCollisionShape shape{};
        if (!isfinite(radius) || radius > FLT_MAX || !TransformEndpoint(matrix, collider.start, shape.start) || !TransformEndpoint(matrix, collider.end, shape.end))
        {
            error.Assign("secondary motion collider cannot be represented as finite coordinates");
            return false;
        }
        shape.radius = static_cast<float>(radius);
        if (!(shape.radius > 0.0f) || !candidate.Append(shape))
        {
            error.Assign("secondary motion collider allocation or radius conversion failed");
            return false;
        }
    }
    if (!animation::ValidateSecondaryMotionCollisionShapes(candidate.Data(), candidate.Count(), error))
    {
        return false;
    }
    output.MoveFrom(candidate);
    error.Clear();
    return true;
}
}
