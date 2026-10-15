// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionContactReach.h"
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "model/animation/ModelSecondaryMotionCollision.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{

// 保存位置の128 float epsilonの許容幅と、積和・距離計算の丸めを広めに包む。
constexpr double kReachCoordinateGuard = 512.0 * FLT_EPSILON;
// 最小回転の分母が特異判定の境界へ近づく場合も、offsetの伸びを過小評価しない。
constexpr double kReachRotationGuard = 1024.0 * DBL_EPSILON / kSecondaryMotionAttachmentAntiparallelThreshold;

/**
 * 二乗前に縮小し、offsetの長さの桁あふれを避ける。
 */
double Length(const gk::FVector3d& value)
{
    const double largest = fmax(fabs(value.value[0]), fmax(fabs(value.value[1]), fabs(value.value[2])));
    if (!(largest > 0.0) || !isfinite(largest))
    {
        return largest;
    }
    const double x = value.value[0] / largest;
    const double y = value.value[1] / largest;
    const double z = value.value[2] / largest;
    return largest * sqrt(x * x + y * y + z * z);
}

/**
 * 角度制約を無視し、全anchorの可能位置を含む球と絶対座標の加算尺度を作る。
 * 計算が不確かか、既にどの身体半径よりも広い場合は診断を保留する。
 */
bool BuildReachSphere(const FSecondaryMotionContactChain* chains, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, const FSecondaryMotionWeightedContactPoint& contact, const gk::Array<gk::FVector3d>& positions, double maximumRadius, gk::FVector3d& center, double& radius, double& coordinateScale)
{
    center = {};
    radius = 0.0;
    coordinateScale = 0.0;
    for (uint32_t localAnchor = 0; localAnchor < contact.anchorCount; ++localAnchor)
    {
        const FSecondaryMotionWeightedContactAnchor& anchor = anchors[contact.firstAnchor + localAnchor];
        gk::FVector3d anchorCenter = anchor.constantPosition;
        if (anchor.chainIndex != 0xFFFFFFFFu)
        {
            const FSecondaryMotionContactChain& chain = chains[anchor.chainIndex];
            anchorCenter = positions.At(chain.firstPoint);
            // offsetはskin局所値でなく、実際のattachment回転へ渡すscale変換済み値。
            const double offsetLength = Length(anchor.definition.offset);
            radius += anchor.weight * offsetLength * (1.0 + kReachRotationGuard);
            for (uint32_t segment = 0; segment < anchor.segmentIndex; ++segment)
            {
                radius += anchor.weight * references[chain.firstSegment + segment].length;
                if (!isfinite(radius) || radius >= maximumRadius)
                {
                    return false;
                }
            }
        }
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            const double contribution = anchor.weight * anchorCenter.value[axis];
            center.value[axis] += contribution;
            // 相殺でcenterだけが小さくなる場合も、失われた桁を余白へ含める。
            coordinateScale += fabs(contribution);
        }
        if (!isfinite(radius) || !isfinite(coordinateScale) || radius >= maximumRadius)
        {
            return false;
        }
    }
    for (uint32_t axis = 0; axis < 3u; ++axis)
    {
        if (!isfinite(center.value[axis]) || fabs(center.value[axis]) > FLT_MAX)
        {
            return false;
        }
    }
    coordinateScale += radius;
    return isfinite(coordinateScale);
}

}

bool FindUnreachableSecondaryMotionWeightedContact(const FSecondaryMotionContactChain* chains, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& positions, uint32_t& contactIndex, uint32_t& shapeIndex)
{
    for (uint32_t point = 0; point < contactCount; ++point)
    {
        const FSecondaryMotionWeightedContactPoint& contact = contacts[point];
        double maximumRadius = 0.0;
        for (uint32_t shape = 0; shape < contact.shapeCount; ++shape)
        {
            maximumRadius = fmax(maximumRadius, contact.shapes[shape].radius);
        }
        gk::FVector3d center{};
        double radius = 0.0;
        double coordinateScale = 0.0;
        if (!(maximumRadius > 0.0) || !BuildReachSphere(chains, references, anchors, contact, positions, maximumRadius, center, radius, coordinateScale))
        {
            continue;
        }
        for (uint32_t shape = 0; shape < contact.shapeCount; ++shape)
        {
            const FPreparedSecondaryMotionCollisionShape& body = contact.shapes[shape];
            double scale = coordinateScale + body.radius + body.tolerance;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                scale += fabs(body.start.value[axis]) + fabs(body.end.value[axis]) + fabs(body.axis.value[axis]);
            }
            // 極小値、float保存、上流骨長の許容幅、中心と軸の積和を過大側へ含める。
            const double guard = kReachCoordinateGuard * scale + 4096.0 * DBL_MIN;
            const double expandedReach = radius + guard;
            if (!isfinite(guard) || !isfinite(expandedReach) || !(expandedReach < body.radius - body.tolerance))
            {
                continue;
            }
            double penetration = 0.0;
            double spanAmount = 0.0;
            gk::FVector3d normal{};
            gk::String queryError;
            // 通常weightedの半分許容より緩い、全許容幅でも退避できない場合だけ証明する。
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(body, center, center, penetration, spanAmount, normal, queryError, 1.0))
            {
                continue;
            }
            // 点からcapsuleまでの距離変化は、その点の移動距離を超えない。
            if (penetration > expandedReach)
            {
                contactIndex = point;
                shapeIndex = shape;
                return true;
            }
        }
    }
    return false;
}

}
