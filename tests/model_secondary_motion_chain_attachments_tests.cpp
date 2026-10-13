// SPDX-License-Identifier: NOASSERTION
#include "foundation/Array.h"
#include "foundation/Memory.h"
#include "foundation/String.h"
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "model/animation/ModelSecondaryMotionChainContacts.h"
#include "model/animation/ModelSecondaryMotionCollision.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

using namespace gk::model::animation;

/**
 * 失敗した契約の名前を表示する。
 */
bool Fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return false;
}

/**
 * 三成分の距離を独立に計算する。
 */
double Distance(const gk::FVector3d& first, const gk::FVector3d& second)
{
    const double x = first.value[0] - second.value[0];
    const double y = first.value[1] - second.value[1];
    const double z = first.value[2] - second.value[2];
    return sqrt(x * x + y * y + z * z);
}

/**
 * 独立した幾何計算で接触球の外側にあることを調べる。
 */
bool IsOutsideSphere(const gk::FVector3d& point, const gk::Vec3& center, double radius, double margin)
{
    const gk::FVector3d sphereCenter = { { center.x, center.y, center.z } };
    return Distance(point, sphereCenter) >= radius + margin;
}

/**
 * 点からカプセル軸までの最短距離を独立に求める。
 */
double PointSegmentDistance(const gk::FVector3d& point, const gk::FVector3d& first, const gk::FVector3d& second)
{
    const gk::FVector3d axis = { { second.value[0] - first.value[0], second.value[1] - first.value[1], second.value[2] - first.value[2] } };
    const gk::FVector3d relative = { { point.value[0] - first.value[0], point.value[1] - first.value[1], point.value[2] - first.value[2] } };
    const double axisSquared = axis.value[0] * axis.value[0] + axis.value[1] * axis.value[1] + axis.value[2] * axis.value[2];
    double amount = 0.0;
    if (axisSquared > 0.0)
    {
        amount = fmax(0.0, fmin(1.0, (relative.value[0] * axis.value[0] + relative.value[1] * axis.value[1] + relative.value[2] * axis.value[2]) / axisSquared));
    }
    const gk::FVector3d closest = { { first.value[0] + amount * axis.value[0], first.value[1] + amount * axis.value[1], first.value[2] + amount * axis.value[2] } };
    return Distance(point, closest);
}

/**
 * 2本の有限線分間の最短距離を独立に求める。
 */
double SegmentSegmentDistance(const gk::FVector3d& firstStart, const gk::FVector3d& firstEnd, const gk::FVector3d& secondStart, const gk::FVector3d& secondEnd)
{
    const gk::FVector3d firstAxis = { { firstEnd.value[0] - firstStart.value[0], firstEnd.value[1] - firstStart.value[1], firstEnd.value[2] - firstStart.value[2] } };
    const gk::FVector3d secondAxis = { { secondEnd.value[0] - secondStart.value[0], secondEnd.value[1] - secondStart.value[1], secondEnd.value[2] - secondStart.value[2] } };
    const gk::FVector3d offset = { { firstStart.value[0] - secondStart.value[0], firstStart.value[1] - secondStart.value[1], firstStart.value[2] - secondStart.value[2] } };
    const double firstSquared = firstAxis.value[0] * firstAxis.value[0] + firstAxis.value[1] * firstAxis.value[1] + firstAxis.value[2] * firstAxis.value[2];
    const double secondSquared = secondAxis.value[0] * secondAxis.value[0] + secondAxis.value[1] * secondAxis.value[1] + secondAxis.value[2] * secondAxis.value[2];
    if (!(firstSquared > 0.0) && !(secondSquared > 0.0))
    {
        return Distance(firstStart, secondStart);
    }
    if (!(firstSquared > 0.0))
    {
        return PointSegmentDistance(firstStart, secondStart, secondEnd);
    }
    if (!(secondSquared > 0.0))
    {
        return PointSegmentDistance(secondStart, firstStart, firstEnd);
    }
    // 外積の長さを使い、ほぼ平行な軸で大きな二乗値の差を取らない。
    const gk::FVector3d normal = { { firstAxis.value[1] * secondAxis.value[2] - firstAxis.value[2] * secondAxis.value[1], firstAxis.value[2] * secondAxis.value[0] - firstAxis.value[0] * secondAxis.value[2], firstAxis.value[0] * secondAxis.value[1] - firstAxis.value[1] * secondAxis.value[0] } };
    const double denominator = normal.value[0] * normal.value[0] + normal.value[1] * normal.value[1] + normal.value[2] * normal.value[2];
    double minimumDistance = fmin(PointSegmentDistance(firstStart, secondStart, secondEnd), PointSegmentDistance(firstEnd, secondStart, secondEnd));
    minimumDistance = fmin(minimumDistance, PointSegmentDistance(secondStart, firstStart, firstEnd));
    minimumDistance = fmin(minimumDistance, PointSegmentDistance(secondEnd, firstStart, firstEnd));
    if (denominator > 0.0)
    {
        const gk::FVector3d secondCrossOffset = { { secondAxis.value[1] * offset.value[2] - secondAxis.value[2] * offset.value[1], secondAxis.value[2] * offset.value[0] - secondAxis.value[0] * offset.value[2], secondAxis.value[0] * offset.value[1] - secondAxis.value[1] * offset.value[0] } };
        const gk::FVector3d firstCrossOffset = { { firstAxis.value[1] * offset.value[2] - firstAxis.value[2] * offset.value[1], firstAxis.value[2] * offset.value[0] - firstAxis.value[0] * offset.value[2], firstAxis.value[0] * offset.value[1] - firstAxis.value[1] * offset.value[0] } };
        const double firstAmount = (secondCrossOffset.value[0] * normal.value[0] + secondCrossOffset.value[1] * normal.value[1] + secondCrossOffset.value[2] * normal.value[2]) / denominator;
        const double secondAmount = (firstCrossOffset.value[0] * normal.value[0] + firstCrossOffset.value[1] * normal.value[1] + firstCrossOffset.value[2] * normal.value[2]) / denominator;
        if (firstAmount >= 0.0 && firstAmount <= 1.0 && secondAmount >= 0.0 && secondAmount <= 1.0)
        {
            const gk::FVector3d firstPoint = { { firstStart.value[0] + firstAmount * firstAxis.value[0], firstStart.value[1] + firstAmount * firstAxis.value[1], firstStart.value[2] + firstAmount * firstAxis.value[2] } };
            const gk::FVector3d secondPoint = { { secondStart.value[0] + secondAmount * secondAxis.value[0], secondStart.value[1] + secondAmount * secondAxis.value[1], secondStart.value[2] + secondAmount * secondAxis.value[2] } };
            minimumDistance = fmin(minimumDistance, Distance(firstPoint, secondPoint));
        }
    }
    return minimumDistance;
}

/**
 * 独立線分距離式が交差、内部最短点、平行線分を正しく扱うか調べる。
 */
bool TestSegmentSegmentDistanceExactCases()
{
    const gk::FVector3d crossingFirstStart = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d crossingFirstEnd = { { 2.0, 0.0, 0.0 } };
    const gk::FVector3d crossingSecondStart = { { 1.0, -1.0, 0.0 } };
    const gk::FVector3d crossingSecondEnd = { { 1.0, 1.0, 0.0 } };
    if (SegmentSegmentDistance(crossingFirstStart, crossingFirstEnd, crossingSecondStart, crossingSecondEnd) > 1.0e-12)
    {
        return Fail("independent segment distance missed an interior crossing");
    }
    const gk::FVector3d skewSecondStart = { { 1.0, -1.0, 0.5 } };
    const gk::FVector3d skewSecondEnd = { { 1.0, 1.0, 0.5 } };
    if (fabs(SegmentSegmentDistance(crossingFirstStart, crossingFirstEnd, skewSecondStart, skewSecondEnd) - 0.5) > 1.0e-12)
    {
        return Fail("independent segment distance missed an interior skew minimum");
    }
    const gk::FVector3d parallelFirstEnd = { { 3.0, 0.0, 0.0 } };
    const gk::FVector3d parallelSecondStart = { { 1.0, 2.0, 1.0 } };
    const gk::FVector3d parallelSecondEnd = { { 4.0, 2.0, 1.0 } };
    if (fabs(SegmentSegmentDistance(crossingFirstStart, parallelFirstEnd, parallelSecondStart, parallelSecondEnd) - sqrt(5.0)) > 1.0e-12)
    {
        return Fail("independent segment distance mishandled parallel segments");
    }
    const gk::FVector3d longFirstStart = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d longFirstEnd = { { 100.0, 0.0, 0.0 } };
    const gk::FVector3d shallowSecondStart = { { -100000000.0, -11.0, 0.5 } };
    const gk::FVector3d shallowSecondEnd = { { 100000000.0, 9.0, 0.5 } };
    const gk::FVector3d shallowInteriorStart = { { -100000000.0, -10.000005, 0.5 } };
    const gk::FVector3d shallowInteriorEnd = { { 100000000.0, 9.999995, 0.5 } };
    if (fabs(SegmentSegmentDistance(longFirstStart, longFirstEnd, shallowInteriorStart, shallowInteriorEnd) - 0.5) > 1.0e-10)
    {
        return Fail("independent segment distance missed a shallow interior minimum");
    }
    const double endpointMinimum = PointSegmentDistance(longFirstEnd, shallowSecondStart, shallowSecondEnd);
    if (fabs(SegmentSegmentDistance(longFirstStart, longFirstEnd, shallowSecondStart, shallowSecondEnd) - endpointMinimum) > 1.0e-10)
    {
        return Fail("independent segment distance missed a shallow endpoint minimum");
    }
    return true;
}

/**
 * fixture内の一つの形状行から準備済み値に必要な入力を読む。
 */
bool ReadFixtureShape(FILE* file, FModelSecondaryMotionCollisionShape& shape, double& tolerance, bool& hasRootOverride, double& rootThreshold)
{
    double values[10]{};
    for (uint32_t index = 0; index < 10; ++index)
    {
        if (fscanf(file, "%lf", &values[index]) != 1 || !isfinite(values[index]))
        {
            return false;
        }
    }
    shape.start = gk::Vec3{ static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2]) };
    shape.end = gk::Vec3{ static_cast<float>(values[3]), static_cast<float>(values[4]), static_cast<float>(values[5]) };
    shape.radius = static_cast<float>(values[6]);
    tolerance = values[7];
    hasRootOverride = values[8] != 0.0;
    rootThreshold = values[9];
    return isfinite(shape.start.x) && isfinite(shape.start.y) && isfinite(shape.start.z) && isfinite(shape.end.x) && isfinite(shape.end.y) && isfinite(shape.end.z) && shape.radius >= 0.0f && tolerance >= 0.0 && rootThreshold >= 0.0;
}

/**
 * fixtureから骨ローカル接触定義を読む。
 */
bool ReadFixtureDefinition(FILE* file, FSecondaryMotionAttachmentDefinition& definition)
{
    double values[7]{};
    for (uint32_t index = 0; index < 7; ++index)
    {
        if (fscanf(file, "%lf", &values[index]) != 1 || !isfinite(values[index]))
        {
            return false;
        }
    }
    definition.direction = { { values[0], values[1], values[2] } };
    definition.offset = { { values[3], values[4], values[5] } };
    definition.cosineLimit = values[6];
    return definition.cosineLimit >= -1.0 && definition.cosineLimit <= 1.0;
}

/**
 * 基準方向から現在方向への最小回転をRodrigues式で独立に計算する。
 */
bool EvaluateFixtureAttachmentPoint(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, const gk::FVector3d& child, gk::FVector3d& point)
{
    gk::FVector3d base = definition.direction;
    gk::FVector3d current = { { child.value[0] - parent.value[0], child.value[1] - parent.value[1], child.value[2] - parent.value[2] } };
    const double baseLength = Distance(base, { { 0.0, 0.0, 0.0 } });
    const double currentLength = Distance(current, { { 0.0, 0.0, 0.0 } });
    if (!(baseLength > 0.0) || !(currentLength > 0.0))
    {
        return false;
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        base.value[axis] /= baseLength;
        current.value[axis] /= currentLength;
    }
    const double cosine = base.value[0] * current.value[0] + base.value[1] * current.value[1] + base.value[2] * current.value[2];
    if (!(1.0 + cosine > 1.0e-12))
    {
        return false;
    }
    const gk::FVector3d rotationAxis = { { base.value[1] * current.value[2] - base.value[2] * current.value[1], base.value[2] * current.value[0] - base.value[0] * current.value[2], base.value[0] * current.value[1] - base.value[1] * current.value[0] } };
    const gk::FVector3d firstCross = { { rotationAxis.value[1] * definition.offset.value[2] - rotationAxis.value[2] * definition.offset.value[1], rotationAxis.value[2] * definition.offset.value[0] - rotationAxis.value[0] * definition.offset.value[2], rotationAxis.value[0] * definition.offset.value[1] - rotationAxis.value[1] * definition.offset.value[0] } };
    const gk::FVector3d secondCross = { { rotationAxis.value[1] * firstCross.value[2] - rotationAxis.value[2] * firstCross.value[1], rotationAxis.value[2] * firstCross.value[0] - rotationAxis.value[0] * firstCross.value[2], rotationAxis.value[0] * firstCross.value[1] - rotationAxis.value[1] * firstCross.value[0] } };
    point = { { parent.value[0] + definition.offset.value[0] + firstCross.value[0] + secondCross.value[0] / (1.0 + cosine), parent.value[1] + definition.offset.value[1] + firstCross.value[1] + secondCross.value[1] / (1.0 + cosine), parent.value[2] + definition.offset.value[2] + firstCross.value[2] + secondCross.value[2] / (1.0 + cosine) } };
    return isfinite(point.value[0]) && isfinite(point.value[1]) && isfinite(point.value[2]);
}

/**
 * 骨方向がcontact定義自身の角度範囲内か独立に調べる。
 */
bool IsWithinDefinitionCone(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, const gk::FVector3d& child)
{
    const gk::FVector3d segment = { { child.value[0] - parent.value[0], child.value[1] - parent.value[1], child.value[2] - parent.value[2] } };
    const double segmentLength = Distance(segment, { { 0.0, 0.0, 0.0 } });
    const double referenceLength = Distance(definition.direction, { { 0.0, 0.0, 0.0 } });
    if (!(segmentLength > 0.0) || !(referenceLength > 0.0))
    {
        return false;
    }
    const double cosine = (segment.value[0] * definition.direction.value[0] + segment.value[1] * definition.direction.value[1] + segment.value[2] * definition.direction.value[2]) / (segmentLength * referenceLength);
    return cosine >= definition.cosineLimit - 1.0e-10;
}

/**
 * prepared shape群とchain poseを独立した線分距離式で照合する。
 */
bool CheckFixtureBody(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d* positions, uint32_t pointCount, double toleranceFraction)
{
    const gk::FVector3d root = positions[0];
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        const auto& shape = shapes[shapeIndex];
        const bool rootInside = shape.hasRootContactOverride && PointSegmentDistance(root, shape.start, shape.end) < shape.rootContactThreshold;
        for (uint32_t segment = 0; segment + 1 < pointCount; ++segment)
        {
            if (segment == 0 && rootInside)
            {
                continue;
            }
            const double distance = SegmentSegmentDistance(positions[segment], positions[segment + 1], shape.start, shape.end);
            if (distance < shape.radius - shape.tolerance * toleranceFraction - 1.0e-10)
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * prepared attachment shapesと骨ローカルoffset線分の距離を独立に照合する。
 */
bool CheckFixtureAttachments(const FSecondaryMotionChainAttachmentContact* contacts, uint32_t contactCount, const gk::FVector3d* positions, double toleranceFraction)
{
    gk::String error;
    for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
    {
        const auto& contact = contacts[contactIndex];
        gk::FVector3d first{};
        gk::FVector3d second{};
        if (!IsWithinDefinitionCone(contact.firstDefinition, positions[contact.segmentIndex], positions[contact.segmentIndex + 1]) || !IsWithinDefinitionCone(contact.secondDefinition, positions[contact.segmentIndex], positions[contact.segmentIndex + 1]) || !EvaluateFixtureAttachmentPoint(contact.firstDefinition, positions[contact.segmentIndex], positions[contact.segmentIndex + 1], first) || !EvaluateFixtureAttachmentPoint(contact.secondDefinition, positions[contact.segmentIndex], positions[contact.segmentIndex + 1], second))
        {
            return false;
        }
        for (uint32_t shapeIndex = 0; shapeIndex < contact.shapeCount; ++shapeIndex)
        {
            const auto& shape = contact.shapes[shapeIndex];
            const double distance = SegmentSegmentDistance(first, second, shape.start, shape.end);
            if (distance < shape.radius - shape.tolerance * toleranceFraction - 1.0e-10)
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * 2つの骨ローカル接触が同じ鎖の子骨を動かすとき、両方を同時に解く。
 */
bool TestJointSameChainOffsetSpheres()
{
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d initialChild = { { 0.0, 1.0, 0.0 } };
    const double coneCosine = 0.8;
    const FModelSecondaryMotionCollisionShape bodies[2] = { { { 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.08f }, { { -0.5f, 0.0f, 0.0f }, { -0.5f, 0.0f, 0.0f }, 0.08f } };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared[2];
    gk::String error;
    for (uint32_t index = 0; index < 2; ++index)
    {
        if (!PrepareSecondaryMotionCollisionShapes(&bodies[index], 1, prepared[index], error) || prepared[index].Count() != 1)
        {
            return Fail(error.CStr());
        }
    }
    FSecondaryMotionAttachmentDefinition definitions[2]{};
    for (uint32_t index = 0; index < 2; ++index)
    {
        definitions[index].direction = { { 0.0, 1.0, 0.0 } };
        definitions[index].offset = { { index == 0 ? 0.5 : -0.5, 0.0, 0.0 } };
        definitions[index].cosineLimit = coneCosine;
    }
    FSecondaryMotionChainAttachmentContact contacts[2]{};
    for (uint32_t index = 0; index < 2; ++index)
    {
        contacts[index].segmentIndex = 0;
        contacts[index].firstDefinition = definitions[index];
        contacts[index].secondDefinition = definitions[index];
        contacts[index].shapes = prepared[index].Data();
        contacts[index].shapeCount = prepared[index].Count();
    }

    // 初期姿勢では2つの接触点がそれぞれ身体球の中心にある。
    for (uint32_t index = 0; index < 2; ++index)
    {
        FSecondaryMotionAttachmentEvaluation evaluation{};
        if (!EvaluateSecondaryMotionAttachment(definitions[index], root, initialChild, evaluation, error))
        {
            return Fail(error.CStr());
        }
        if (Distance(evaluation.position, { { bodies[index].start.x, bodies[index].start.y, bodies[index].start.z } }) >= bodies[index].radius)
        {
            return Fail("joint attachment fixture did not start inside both independent body spheres");
        }
    }

    // 既知の可解姿勢は鎖長と円錐を保ち、両接触点を球の外へ動かす。
    const double witnessAngle = 0.3;
    const gk::FVector3d witnessChild = { { sin(witnessAngle), cos(witnessAngle), 0.0 } };
    if (fabs(Distance(root, witnessChild) - 1.0) > 1.0e-12 || witnessChild.value[1] < coneCosine)
    {
        return Fail("joint attachment witness violates the independent bone length or cone constraint");
    }
    for (uint32_t index = 0; index < 2; ++index)
    {
        FSecondaryMotionAttachmentEvaluation evaluation{};
        if (!EvaluateSecondaryMotionAttachment(definitions[index], root, witnessChild, evaluation, error))
        {
            return Fail(error.CStr());
        }
        if (!IsOutsideSphere(evaluation.position, bodies[index].start, bodies[index].radius, 0.04))
        {
            return Fail("joint attachment witness does not clear both independent body spheres");
        }
    }

    FSecondaryMotionReferenceSegment reference{};
    reference.direction = { { 0.0, 1.0, 0.0 } };
    reference.length = 1.0;
    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(root) || !positions.Append(initialChild))
    {
        return Fail("joint attachment fixture allocation failed");
    }
    if (!ResolvePreparedSecondaryMotionChainContacts(nullptr, 0, &reference, 1, coneCosine, positions, error, 1.0, contacts, 2))
    {
        return Fail(error.CStr());
    }
    if (!error.Empty() || Distance(positions.At(0), root) != 0.0 || fabs(Distance(positions.At(0), positions.At(1)) - 1.0) > 2.0e-6)
    {
        return Fail("joint attachment solve changed the pinned root, bone length, or returned an error");
    }
    const double solvedCosine = positions.At(1).value[1] / Distance(positions.At(0), positions.At(1));
    if (solvedCosine < coneCosine - 1.0e-6)
    {
        return Fail("joint attachment solve exceeded the bone cone");
    }
    for (uint32_t index = 0; index < 2; ++index)
    {
        FSecondaryMotionAttachmentEvaluation evaluation{};
        if (!EvaluateSecondaryMotionAttachment(definitions[index], positions.At(0), positions.At(1), evaluation, error))
        {
            return Fail(error.CStr());
        }
        if (!IsOutsideSphere(evaluation.position, bodies[index].start, bodies[index].radius, 1.0e-5))
        {
            return Fail("joint attachment solve cleared one body sphere but left the other offset inside");
        }
    }
    return true;
}

/**
 * 不可能な複数接触では、鎖の候補位置を途中まで書き換えない。
 */
bool TestJointAttachmentFailureIsAtomic()
{
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const FModelSecondaryMotionCollisionShape impossibleBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 4.0f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&impossibleBody, 1, prepared, error))
    {
        return Fail(error.CStr());
    }
    FSecondaryMotionAttachmentDefinition definition{};
    definition.direction = { { 0.0, 1.0, 0.0 } };
    definition.offset = { { 0.5, 0.0, 0.0 } };
    definition.cosineLimit = 0.0;
    FSecondaryMotionChainAttachmentContact contact{};
    contact.segmentIndex = 0;
    contact.firstDefinition = definition;
    contact.secondDefinition = definition;
    contact.shapes = prepared.Data();
    contact.shapeCount = prepared.Count();
    FSecondaryMotionReferenceSegment reference{};
    reference.direction = { { 0.0, 1.0, 0.0 } };
    reference.length = 1.0;
    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(root) || !positions.Append({ { 0.0, 1.0, 0.0 } }))
    {
        return Fail("atomicity fixture allocation failed");
    }
    gk::FVector3d snapshot[2] = { positions.At(0), positions.At(1) };
    char diagnosticReservation[256]{};
    memset(diagnosticReservation, 'x', sizeof(diagnosticReservation) - 1);
    if (!error.Assign(diagnosticReservation))
    {
        return Fail("could not reserve diagnostic storage before allocation failure injection");
    }
    error.Clear();
    gk::SetAllocationFailureAfterForTesting(0);
    const bool allocationFailureAccepted = ResolvePreparedSecondaryMotionChainContacts(nullptr, 0, &reference, 1, 0.0, positions, error, 1.0, &contact, 1);
    gk::ResetAllocationFailureForTesting();
    if (allocationFailureAccepted || !strstr(error.CStr(), "allocation") || memcmp(positions.Data(), snapshot, sizeof(snapshot)) != 0)
    {
        return Fail("joint attachment allocation failure was accepted or changed chain positions");
    }
    error.Clear();
    if (ResolvePreparedSecondaryMotionChainContacts(nullptr, 0, &reference, 1, 0.0, positions, error, 1.0, &contact, 1) || !error.Empty())
    {
        return Fail("unreachable joint attachment contact was accepted");
    }
    if (memcmp(positions.Data(), snapshot, sizeof(snapshot)) != 0)
    {
        return Fail("failed joint attachment solve changed chain positions");
    }
    return true;
}

/**
 * 不正な鎖内segment番号を拒否し、位置配列を保つ。
 */
bool TestJointAttachmentInvalidSegmentIsAtomic()
{
    const FModelSecondaryMotionCollisionShape body = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.1f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&body, 1, prepared, error))
    {
        return Fail(error.CStr());
    }
    FSecondaryMotionAttachmentDefinition definition{};
    definition.direction = { { 0.0, 1.0, 0.0 } };
    definition.offset = { { 0.0, 0.0, 0.0 } };
    definition.cosineLimit = 0.0;
    FSecondaryMotionChainAttachmentContact contact{};
    contact.segmentIndex = 1;
    contact.firstDefinition = definition;
    contact.secondDefinition = definition;
    contact.shapes = prepared.Data();
    contact.shapeCount = prepared.Count();
    FSecondaryMotionReferenceSegment reference{};
    reference.direction = { { 0.0, 1.0, 0.0 } };
    reference.length = 1.0;
    gk::Array<gk::FVector3d> positions;
    if (!positions.Append({ { 0.0, 0.0, 0.0 } }) || !positions.Append({ { 0.0, 1.0, 0.0 } }))
    {
        return Fail("invalid-segment fixture allocation failed");
    }
    gk::FVector3d snapshot[2] = { positions.At(0), positions.At(1) };
    if (ResolvePreparedSecondaryMotionChainContacts(nullptr, 0, &reference, 1, 0.0, positions, error, 1.0, &contact, 1) || error.Empty())
    {
        return Fail("joint attachment solver accepted an out-of-range segment index");
    }
    if (memcmp(positions.Data(), snapshot, sizeof(snapshot)) != 0)
    {
        return Fail("invalid joint attachment input changed chain positions");
    }
    FSecondaryMotionAttachmentDefinition mismatchedDefinition = definition;
    mismatchedDefinition.cosineLimit = -1.0;
    contact.segmentIndex = 0;
    contact.firstDefinition = mismatchedDefinition;
    error.Clear();
    if (ResolvePreparedSecondaryMotionChainContacts(nullptr, 0, &reference, 1, 0.0, positions, error, 1.0, &contact, 1) || error.Empty() || memcmp(positions.Data(), snapshot, sizeof(snapshot)) != 0)
    {
        return Fail("joint attachment solver accepted a mismatched definition cone or changed positions");
    }
    FPreparedSecondaryMotionCollisionShape invalidShape = prepared.At(0);
    invalidShape.start.value[0] = NAN;
    contact.firstDefinition = definition;
    contact.shapes = &invalidShape;
    error.Clear();
    if (ResolvePreparedSecondaryMotionChainContacts(nullptr, 0, &reference, 1, 0.0, positions, error, 1.0, &contact, 1) || error.Empty() || memcmp(positions.Data(), snapshot, sizeof(snapshot)) != 0)
    {
        return Fail("joint attachment solver accepted a NaN shape or changed positions");
    }
    return true;
}

/**
 * 同じ骨の異なるoffsetを結ぶcapsule接触を独立の幾何式で確認する。
 */
bool TestSameBoneCapsuleWithDifferentOffsets()
{
    const FModelSecondaryMotionCollisionShape rawBody = { { 0.12f, 0.0f, -0.1f }, { 0.12f, 0.0f, 0.1f }, 0.04f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&rawBody, 1, prepared, error))
    {
        return Fail(error.CStr());
    }
    FSecondaryMotionAttachmentDefinition first{};
    first.direction = { { 0.0, 1.0, 0.0 } };
    first.offset = { { 0.2, 0.0, 0.0 } };
    first.cosineLimit = 0.8;
    FSecondaryMotionAttachmentDefinition second = first;
    second.offset = { { -0.2, 0.0, 0.0 } };
    FSecondaryMotionChainAttachmentContact contact{};
    contact.segmentIndex = 0;
    contact.firstDefinition = first;
    contact.secondDefinition = second;
    contact.shapes = prepared.Data();
    contact.shapeCount = prepared.Count();
    FSecondaryMotionReferenceSegment reference{};
    reference.direction = { { 0.0, 1.0, 0.0 } };
    reference.length = 1.0;
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(root) || !positions.Append({ { 0.0, 1.0, 0.0 } }))
    {
        return Fail("same-bone capsule fixture allocation failed");
    }
    gk::FVector3d witness[2] = { root, { { sin(0.6), cos(0.6), 0.0 } } };
    gk::FVector3d witnessFirst{};
    gk::FVector3d witnessSecond{};
    if (!EvaluateFixtureAttachmentPoint(first, witness[0], witness[1], witnessFirst) || !EvaluateFixtureAttachmentPoint(second, witness[0], witness[1], witnessSecond) || SegmentSegmentDistance(witnessFirst, witnessSecond, { { rawBody.start.x, rawBody.start.y, rawBody.start.z } }, { { rawBody.end.x, rawBody.end.y, rawBody.end.z } }) < rawBody.radius + 0.02)
    {
        return Fail("same-bone capsule witness did not clear the offset span");
    }
    FSecondaryMotionAttachmentEvaluation initialFirst{};
    FSecondaryMotionAttachmentEvaluation initialSecond{};
    if (!EvaluateSecondaryMotionAttachment(first, root, positions.At(1), initialFirst, error) || !EvaluateSecondaryMotionAttachment(second, root, positions.At(1), initialSecond, error) || SegmentSegmentDistance(initialFirst.position, initialSecond.position, { { rawBody.start.x, rawBody.start.y, rawBody.start.z } }, { { rawBody.end.x, rawBody.end.y, rawBody.end.z } }) >= rawBody.radius)
    {
        return Fail("same-bone capsule fixture did not start with an intersecting offset span");
    }
    if (!ResolvePreparedSecondaryMotionChainContacts(nullptr, 0, &reference, 1, 0.8, positions, error, 1.0, &contact, 1) || !error.Empty())
    {
        return Fail(error.CStr());
    }
    if (Distance(positions.At(0), root) != 0.0 || fabs(Distance(positions.At(0), positions.At(1)) - 1.0) > 2.0e-6 || positions.At(1).value[1] < 0.8 - 1.0e-5)
    {
        return Fail("same-bone capsule correction changed the root, length, or cone");
    }
    FSecondaryMotionAttachmentEvaluation resultFirst{};
    FSecondaryMotionAttachmentEvaluation resultSecond{};
    if (!EvaluateSecondaryMotionAttachment(first, positions.At(0), positions.At(1), resultFirst, error) || !EvaluateSecondaryMotionAttachment(second, positions.At(0), positions.At(1), resultSecond, error) || SegmentSegmentDistance(resultFirst.position, resultSecond.position, { { rawBody.start.x, rawBody.start.y, rawBody.start.z } }, { { rawBody.end.x, rawBody.end.y, rawBody.end.z } }) < rawBody.radius - prepared.At(0).tolerance * 0.5 - 1.0e-8)
    {
        return Fail("same-bone capsule solve left its two different local offsets penetrated");
    }
    return true;
}

/**
 * 祖先節を動かす必要がある複数の接触を、順序に依存せず解く。
 */
bool TestJointContactsUseAncestorDegreesOfFreedom()
{
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d initialPivot = { { 0.0, 1.0, 0.0 } };
    const gk::FVector3d initialTip = { { 0.0, 2.0, 0.0 } };
    const double coneCosine = 0.8;
    const FModelSecondaryMotionCollisionShape rootBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.1f };
    const FModelSecondaryMotionCollisionShape offsetBodies[2] = { { { 0.05f, 1.0f, 0.0f }, { 0.05f, 1.0f, 0.0f }, 0.2f }, { { -0.05f, 1.0f, 0.0f }, { -0.05f, 1.0f, 0.0f }, 0.2f } };
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedRootBody;
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedOffsetBodies;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&rootBody, 1, preparedRootBody, error) || !PrepareSecondaryMotionCollisionShapes(offsetBodies, 2, preparedOffsetBodies, error))
    {
        return Fail(error.CStr());
    }

    FSecondaryMotionAttachmentDefinition definitions[2]{};
    for (uint32_t index = 0; index < 2; ++index)
    {
        definitions[index].direction = { { 0.0, 1.0, 0.0 } };
        definitions[index].offset = { { index == 0 ? 0.05 : -0.05, 0.0, 0.0 } };
        definitions[index].cosineLimit = coneCosine;
        FSecondaryMotionAttachmentEvaluation initial{};
        if (!EvaluateSecondaryMotionAttachment(definitions[index], initialPivot, initialTip, initial, error) || Distance(initial.position, { { offsetBodies[index].start.x, offsetBodies[index].start.y, offsetBodies[index].start.z } }) >= offsetBodies[index].radius)
        {
            return Fail("ancestor fixture did not start with both offset contacts inside their spheres");
        }
    }

    // 親節を固定した旧4点solverでは、offset長が小さいためsphereから出られない。
    gk::FVector3d fixedPivot = initialPivot;
    gk::FVector3d fixedTip = initialTip;
    gk::FVector3d* oldPoints[4] = { &fixedPivot, &fixedTip, &fixedPivot, &fixedTip };
    if (ProjectSecondaryMotionAttachmentContacts(preparedOffsetBodies.Data(), preparedOffsetBodies.Count(), definitions[0], definitions[1], oldPoints, error))
    {
        return Fail("fixed-parent attachment projection unexpectedly escaped both enclosing spheres");
    }
    if (Distance(fixedPivot, initialPivot) != 0.0 || Distance(fixedTip, initialTip) != 0.0)
    {
        return Fail("fixed-parent attachment failure changed the old four-point input");
    }

    // 既知の可解poseは第1節を円錐境界まで傾け、第2節の親を接触領域から運び出す。
    const gk::FVector3d witnessPivot = { { 0.5, sqrt(0.75), 0.0 } };
    const gk::FVector3d witnessTip = { { witnessPivot.value[0], witnessPivot.value[1] + 1.0, 0.0 } };
    if (fabs(Distance(root, witnessPivot) - 1.0) > 1.0e-12 || fabs(Distance(witnessPivot, witnessTip) - 1.0) > 1.0e-12 || witnessPivot.value[1] < coneCosine)
    {
        return Fail("ancestor witness violates an independent length or cone constraint");
    }
    if (Distance(witnessPivot, { { rootBody.start.x, rootBody.start.y, rootBody.start.z } }) <= rootBody.radius)
    {
        return Fail("ancestor witness did not leave the pinned-root body sphere along the first span");
    }
    for (uint32_t index = 0; index < 2; ++index)
    {
        FSecondaryMotionAttachmentEvaluation evaluation{};
        if (!EvaluateSecondaryMotionAttachment(definitions[index], witnessPivot, witnessTip, evaluation, error) || !IsOutsideSphere(evaluation.position, offsetBodies[index].start, offsetBodies[index].radius, 0.1))
        {
            return Fail("ancestor witness does not independently clear both second-segment offset spheres");
        }
    }

    FSecondaryMotionReferenceSegment references[2]{};
    references[0].direction = { { 0.0, 1.0, 0.0 } };
    references[0].length = 1.0;
    references[1] = references[0];
    FSecondaryMotionChainAttachmentContact contacts[2]{};
    for (uint32_t index = 0; index < 2; ++index)
    {
        contacts[index].segmentIndex = 1;
        contacts[index].firstDefinition = definitions[index];
        contacts[index].secondDefinition = definitions[index];
        contacts[index].shapes = preparedOffsetBodies.Data() + index;
        contacts[index].shapeCount = 1;
    }
    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(root) || !positions.Append(initialPivot) || !positions.Append(initialTip))
    {
        return Fail("ancestor fixture position allocation failed");
    }
    if (!ResolvePreparedSecondaryMotionChainContacts(preparedRootBody.Data(), preparedRootBody.Count(), references, 2, coneCosine, positions, error, 1.0, contacts, 2))
    {
        return Fail(error.CStr());
    }
    if (!error.Empty() || Distance(positions.At(0), root) != 0.0 || fabs(Distance(positions.At(0), positions.At(1)) - 1.0) > 2.0e-6 || fabs(Distance(positions.At(1), positions.At(2)) - 1.0) > 2.0e-6)
    {
        return Fail("joint attachment solve changed the pinned root, a bone length, or returned an error");
    }
    if (positions.At(1).value[1] < coneCosine - 1.0e-5 || positions.At(2).value[1] - positions.At(1).value[1] < coneCosine - 1.0e-5)
    {
        return Fail("joint attachment solve exceeded a segment cone");
    }
    if (!CheckPreparedSecondaryMotionContacts(preparedRootBody.Data(), preparedRootBody.Count(), positions.Data(), positions.Count(), error, 1.0))
    {
        return Fail("joint attachment solve left the ancestor body sphere intersecting a chain span");
    }
    for (uint32_t index = 0; index < 2; ++index)
    {
        FSecondaryMotionAttachmentEvaluation evaluation{};
        if (!EvaluateSecondaryMotionAttachment(definitions[index], positions.At(1), positions.At(2), evaluation, error) || !IsOutsideSphere(evaluation.position, offsetBodies[index].start, offsetBodies[index].radius, 1.0e-5))
        {
            return Fail("joint attachment solve cleared one offset sphere but left another penetrated");
        }
    }

    // 接触配列の順序を反転しても、両方の接触と鎖制約を満たす。
    gk::Array<gk::FVector3d> reversedPositions;
    if (!reversedPositions.Append(root) || !reversedPositions.Append(initialPivot) || !reversedPositions.Append(initialTip))
    {
        return Fail("reversed ancestor fixture allocation failed");
    }
    FSecondaryMotionChainAttachmentContact reversedContacts[2] = { contacts[1], contacts[0] };
    if (!ResolvePreparedSecondaryMotionChainContacts(preparedRootBody.Data(), preparedRootBody.Count(), references, 2, coneCosine, reversedPositions, error, 1.0, reversedContacts, 2) || !error.Empty())
    {
        return Fail("reversing joint attachment order prevented a feasible chain solve");
    }
    if (Distance(reversedPositions.At(0), root) != 0.0 || fabs(Distance(reversedPositions.At(0), reversedPositions.At(1)) - 1.0) > 2.0e-6 || fabs(Distance(reversedPositions.At(1), reversedPositions.At(2)) - 1.0) > 2.0e-6 || reversedPositions.At(1).value[1] < coneCosine - 1.0e-5)
    {
        return Fail("reversed joint attachment order violated the root, length, or cone constraint");
    }
    if (!CheckPreparedSecondaryMotionContacts(preparedRootBody.Data(), preparedRootBody.Count(), reversedPositions.Data(), reversedPositions.Count(), error, 1.0))
    {
        return Fail("reversed joint attachment order left a body span penetrated");
    }
    for (uint32_t index = 0; index < 2; ++index)
    {
        FSecondaryMotionAttachmentEvaluation evaluation{};
        if (!EvaluateSecondaryMotionAttachment(definitions[index], reversedPositions.At(1), reversedPositions.At(2), evaluation, error) || !IsOutsideSphere(evaluation.position, offsetBodies[index].start, offsetBodies[index].radius, 1.0e-5))
        {
            return Fail("reversed joint attachment order left an offset sphere penetrated");
        }
    }
    return true;
}

/**
 * 実機停止時のchain入力と、独立に確認した可解poseを同じkernelで検証する。
 */
bool TestRecordedNativeJointAttachmentFixture(const char* fixturePath)
{
    const uint32_t expectedPointCount = 5;
    const uint32_t expectedSegmentCount = 4;
    const uint32_t expectedBodyCount = 4;
    const uint32_t expectedContactCount = 17;
    const uint32_t expectedShapesPerContact = 8;
    FILE* file = fopen(fixturePath, "rb");
    if (!file)
    {
        return Fail("could not open the native joint attachment fixture");
    }
    unsigned char prefix[3]{};
    const size_t prefixLength = fread(prefix, 1, sizeof(prefix), file);
    if (prefixLength == 3 && !(prefix[0] == 0xefu && prefix[1] == 0xbbu && prefix[2] == 0xbfu))
    {
        rewind(file);
    }
    char magic[64]{};
    unsigned int pointCount = 0;
    unsigned int segmentCount = 0;
    unsigned int bodyCount = 0;
    unsigned int contactCount = 0;
    double cosineLimit = 0.0;
    if (fscanf(file, "%63s", magic) != 1 || strcmp(magic, "GKCORE_JOINT_ATTACHMENT_FIXTURE_V1") != 0 || fscanf(file, "%u %u %u %u", &pointCount, &segmentCount, &bodyCount, &contactCount) != 4 || fscanf(file, "%lf", &cosineLimit) != 1 || pointCount != expectedPointCount || segmentCount != expectedSegmentCount || bodyCount != expectedBodyCount || contactCount != expectedContactCount || !isfinite(cosineLimit) || cosineLimit < -1.0 || cosineLimit > 1.0)
    {
        fclose(file);
        return Fail("native joint attachment fixture header is invalid");
    }
    gk::FVector3d targets[5]{};
    gk::FVector3d capturedPositions[5]{};
    for (uint32_t index = 0; index < pointCount; ++index)
    {
        if (fscanf(file, "%lf %lf %lf", &targets[index].value[0], &targets[index].value[1], &targets[index].value[2]) != 3)
        {
            fclose(file);
            return Fail("native fixture target list is truncated");
        }
    }
    for (uint32_t index = 0; index < pointCount; ++index)
    {
        if (fscanf(file, "%lf %lf %lf", &capturedPositions[index].value[0], &capturedPositions[index].value[1], &capturedPositions[index].value[2]) != 3)
        {
            fclose(file);
            return Fail("native fixture position list is truncated");
        }
    }
    FModelSecondaryMotionCollisionShape bodyInputs[4]{};
    bool bodyRootOverrides[4]{};
    double bodyTolerances[4]{};
    double bodyRootThresholds[4]{};
    for (uint32_t index = 0; index < bodyCount; ++index)
    {
        if (!ReadFixtureShape(file, bodyInputs[index], bodyTolerances[index], bodyRootOverrides[index], bodyRootThresholds[index]))
        {
            fclose(file);
            return Fail("native fixture body shape is invalid");
        }
    }
    struct FContactFixtureRow
    {
        uint32_t segment;
        uint32_t shapeOffset;
        uint32_t shapeCount;
        FSecondaryMotionAttachmentDefinition definitions[2];
        double tolerances[8];
        bool rootOverrides[8];
        double rootThresholds[8];
    } contactRows[17]{};
    FModelSecondaryMotionCollisionShape contactInputs[17 * 8]{};
    for (uint32_t index = 0; index < contactCount; ++index)
    {
        unsigned int segment = 0;
        unsigned int shapeCount = 0;
        if (fscanf(file, "%u %u", &segment, &shapeCount) != 2 || segment >= segmentCount || shapeCount != expectedShapesPerContact || !ReadFixtureDefinition(file, contactRows[index].definitions[0]) || !ReadFixtureDefinition(file, contactRows[index].definitions[1]))
        {
            fclose(file);
            return Fail("native fixture contact header or definition is invalid");
        }
        contactRows[index].segment = segment;
        contactRows[index].shapeOffset = index * expectedShapesPerContact;
        contactRows[index].shapeCount = shapeCount;
        for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
        {
            const uint32_t flatIndex = contactRows[index].shapeOffset + shapeIndex;
            if (!ReadFixtureShape(file, contactInputs[flatIndex], contactRows[index].tolerances[shapeIndex], contactRows[index].rootOverrides[shapeIndex], contactRows[index].rootThresholds[shapeIndex]))
            {
                fclose(file);
                return Fail("native fixture attachment shape is invalid");
            }
        }
    }
    fclose(file);

    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedBody;
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedContactShapes;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(bodyInputs, bodyCount, preparedBody, error) || preparedBody.Count() != bodyCount)
    {
        return Fail(error.CStr());
    }
    for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
    {
        const auto& row = contactRows[contactIndex];
        gk::Array<FPreparedSecondaryMotionCollisionShape> preparedRow;
        if (!PrepareSecondaryMotionCollisionShapes(contactInputs + row.shapeOffset, row.shapeCount, preparedRow, error) || preparedRow.Count() != row.shapeCount || !preparedContactShapes.AppendRange(preparedRow.Data(), preparedRow.Count()))
        {
            return Fail(error.CStr());
        }
    }
    if (preparedContactShapes.Count() != contactCount * expectedShapesPerContact)
    {
        return Fail("native fixture prepared contact shape count is invalid");
    }
    for (uint32_t index = 0; index < bodyCount; ++index)
    {
        auto& shape = preparedBody.At(index);
        if (fabs(shape.tolerance - bodyTolerances[index]) > 1.0e-12)
        {
            return Fail("native fixture body tolerance does not match the prepared collision contract");
        }
        shape.hasRootContactOverride = bodyRootOverrides[index];
        shape.rootContactThreshold = bodyRootThresholds[index];
    }
    FSecondaryMotionChainAttachmentContact contacts[17]{};
    for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
    {
        auto& contact = contacts[contactIndex];
        const auto& row = contactRows[contactIndex];
        contact.segmentIndex = row.segment;
        contact.firstDefinition = row.definitions[0];
        contact.secondDefinition = row.definitions[1];
        contact.shapes = preparedContactShapes.Data() + row.shapeOffset;
        contact.shapeCount = row.shapeCount;
        for (uint32_t shapeIndex = 0; shapeIndex < row.shapeCount; ++shapeIndex)
        {
            auto& shape = preparedContactShapes.At(row.shapeOffset + shapeIndex);
            if (fabs(shape.tolerance - row.tolerances[shapeIndex]) > 1.0e-12)
            {
                return Fail("native fixture attachment tolerance does not match the prepared collision contract");
            }
            shape.hasRootContactOverride = row.rootOverrides[shapeIndex];
            shape.rootContactThreshold = row.rootThresholds[shapeIndex];
        }
    }

    FSecondaryMotionReferenceSegment references[4]{};
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        gk::FVector3d difference = { { targets[index + 1].value[0] - targets[index].value[0], targets[index + 1].value[1] - targets[index].value[1], targets[index + 1].value[2] - targets[index].value[2] } };
        references[index].length = Distance(targets[index], targets[index + 1]);
        if (!(references[index].length > 0.0))
        {
            return Fail("native fixture has a zero-length reference segment");
        }
        references[index].direction = { { difference.value[0] / references[index].length, difference.value[1] / references[index].length, difference.value[2] / references[index].length } };
    }

    // 独立探索で得た可解pose。bodyはfull tolerance、attachment contactは既存half toleranceで検査する。
    const gk::FVector3d witness[5] = { { { -0.00224020379, 1.18901098, 0.153678104 } }, { { -0.09207656792038126, 1.1662013367416355, 0.1667735506955021 } }, { { -0.11879030454032799, 1.1422866180420521, 0.11581766614270345 } }, { { -0.1031651037540036, 1.1366139600604384, 0.1481447584058207 } }, { { -0.08807685455894486, 1.1227304548279473, 0.11977939000594309 } } };
    for (uint32_t index = 0; index < pointCount; ++index)
    {
        if (!isfinite(witness[index].value[0]) || !isfinite(witness[index].value[1]) || !isfinite(witness[index].value[2]))
        {
            return Fail("native witness contains a non-finite position");
        }
        if (index == 0)
        {
            if (Distance(witness[index], capturedPositions[index]) != 0.0)
            {
                return Fail("native witness does not preserve the recorded pinned root");
            }
            continue;
        }
        const double length = Distance(witness[index - 1], witness[index]);
        const gk::FVector3d direction = { { (witness[index].value[0] - witness[index - 1].value[0]) / length, (witness[index].value[1] - witness[index - 1].value[1]) / length, (witness[index].value[2] - witness[index - 1].value[2]) / length } };
        const double cosine = direction.value[0] * references[index - 1].direction.value[0] + direction.value[1] * references[index - 1].direction.value[1] + direction.value[2] * references[index - 1].direction.value[2];
        if (fabs(length - references[index - 1].length) > 2.0e-8 || cosine < cosineLimit - 1.0e-8)
        {
            return Fail("independent native witness violates a chain length or cone constraint");
        }
    }
    if (!CheckFixtureBody(preparedBody.Data(), preparedBody.Count(), witness, pointCount, 1.0) || !CheckFixtureAttachments(contacts, contactCount, witness, 0.5))
    {
        return Fail("independent native witness does not clear every body and attachment shape");
    }
    if (CheckFixtureAttachments(contacts, contactCount, capturedPositions, 0.5))
    {
        return Fail("native failure fixture unexpectedly starts with all attachment contacts clear");
    }

    gk::Array<gk::FVector3d> positions;
    if (!positions.AppendRange(capturedPositions, pointCount))
    {
        return Fail("native fixture position allocation failed");
    }
    if (!ResolvePreparedSecondaryMotionChainContacts(preparedBody.Data(), preparedBody.Count(), references, segmentCount, cosineLimit, positions, error, 1.0, contacts, contactCount))
    {
        return Fail(error.CStr());
    }
    if (!error.Empty() || Distance(positions.At(0), capturedPositions[0]) != 0.0 || !CheckFixtureBody(preparedBody.Data(), preparedBody.Count(), positions.Data(), positions.Count(), 1.0) || !CheckFixtureAttachments(contacts, contactCount, positions.Data(), 0.5))
    {
        return Fail("joint chain solver did not return a finite pose clear of all recorded native contacts");
    }
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        const double length = Distance(positions.At(index), positions.At(index + 1));
        const double cosine = ((positions.At(index + 1).value[0] - positions.At(index).value[0]) * references[index].direction.value[0] + (positions.At(index + 1).value[1] - positions.At(index).value[1]) * references[index].direction.value[1] + (positions.At(index + 1).value[2] - positions.At(index).value[2]) * references[index].direction.value[2]) / length;
        if (!isfinite(length) || fabs(length - references[index].length) > 2.0e-6 || cosine < cosineLimit - 1.0e-5)
        {
            return Fail("joint chain solver changed a recorded segment length or cone");
        }
    }
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc != 2 || !TestSegmentSegmentDistanceExactCases() || !TestJointSameChainOffsetSpheres() || !TestJointAttachmentFailureIsAtomic() || !TestJointAttachmentInvalidSegmentIsAtomic() || !TestSameBoneCapsuleWithDifferentOffsets() || !TestJointContactsUseAncestorDegreesOfFreedom() || !TestRecordedNativeJointAttachmentFixture(argv[1]))
    {
        return 1;
    }
    return 0;
}
