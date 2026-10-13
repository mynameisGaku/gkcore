// SPDX-License-Identifier: NOASSERTION
#include <gkcore/FModelSecondaryMotionContactPoint.h>
#include <gkcore/FModelSecondaryMotionContactAnchor.h>
#include "model/animation/ModelSecondaryMotionWeightedContacts.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionDirection.h"
#include "foundation/Memory.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
using namespace gk::model::animation;

bool Fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return false;
}

double Distance(const gk::FVector3d& first, const gk::FVector3d& second)
{
    const double x = second.value[0] - first.value[0];
    const double y = second.value[1] - first.value[1];
    const double z = second.value[2] - first.value[2];
    return sqrt(x * x + y * y + z * z);
}

gk::FVector3d WeightedPosition(const gk::FVector3d& first, const gk::FVector3d& second, double firstWeight, double secondWeight)
{
    return { { first.value[0] * firstWeight + second.value[0] * secondWeight, first.value[1] * firstWeight + second.value[1] * secondWeight, first.value[2] * firstWeight + second.value[2] * secondWeight } };
}

gk::FVector3d EvaluateAnchorPoint(const FSecondaryMotionWeightedContactAnchor& anchor, const gk::Array<gk::FVector3d>& positions)
{
    if (anchor.chainIndex == 0xFFFFFFFFu)
    {
        return anchor.constantPosition;
    }
    const uint32_t rootIndex = anchor.chainIndex * 2u;
    const auto& parent = positions.At(rootIndex + anchor.segmentIndex);
    const auto& child = positions.At(rootIndex + anchor.segmentIndex + 1u);
    const double dx = child.value[0] - parent.value[0];
    const double dy = child.value[1] - parent.value[1];
    const double dz = child.value[2] - parent.value[2];
    const double length = sqrt(dx * dx + dy * dy + dz * dz);
    const double direction[3] = { dx / length, dy / length, dz / length };
    const double* base = anchor.definition.direction.value;
    const double baseLength = sqrt(base[0] * base[0] + base[1] * base[1] + base[2] * base[2]);
    const double u[3] = { base[0] / baseLength, base[1] / baseLength, base[2] / baseLength };
    const double axis[3] = { u[1] * direction[2] - u[2] * direction[1], u[2] * direction[0] - u[0] * direction[2], u[0] * direction[1] - u[1] * direction[0] };
    const double cosine = u[0] * direction[0] + u[1] * direction[1] + u[2] * direction[2];
    const double firstCross[3] = { axis[1] * anchor.definition.offset.value[2] - axis[2] * anchor.definition.offset.value[1], axis[2] * anchor.definition.offset.value[0] - axis[0] * anchor.definition.offset.value[2], axis[0] * anchor.definition.offset.value[1] - axis[1] * anchor.definition.offset.value[0] };
    const double secondCross[3] = { axis[1] * firstCross[2] - axis[2] * firstCross[1], axis[2] * firstCross[0] - axis[0] * firstCross[2], axis[0] * firstCross[1] - axis[1] * firstCross[0] };
    const double denominator = 1.0 + cosine;
    return { { parent.value[0] + anchor.definition.offset.value[0] + firstCross[0] + secondCross[0] / denominator, parent.value[1] + anchor.definition.offset.value[1] + firstCross[1] + secondCross[1] / denominator, parent.value[2] + anchor.definition.offset.value[2] + firstCross[2] + secondCross[2] / denominator } };
}

bool IsSolved(const gk::Array<gk::FVector3d>& positions, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FPreparedSecondaryMotionCollisionShape& body, const gk::FVector3d& fixedFirst, const gk::FVector3d& fixedSecond)
{
    if (positions.Count() != 4u || Distance(positions.At(0), fixedFirst) > 1.0e-12 || Distance(positions.At(2), fixedSecond) > 1.0e-12)
    {
        return false;
    }
    const gk::FVector3d referenceDirections[2] = { { { 1.0, 0.0, 0.0 } }, { { -1.0, 0.0, 0.0 } } };
    for (uint32_t chain = 0; chain < 2u; ++chain)
    {
        const auto& root = positions.At(chain * 2u);
        const auto& child = positions.At(chain * 2u + 1u);
        const double length = Distance(root, child);
        const double directionDot = ((child.value[0] - root.value[0]) * referenceDirections[chain].value[0] + (child.value[1] - root.value[1]) * referenceDirections[chain].value[1] + (child.value[2] - root.value[2]) * referenceDirections[chain].value[2]) / length;
        if (fabs(length - 1.0) > 0.0011 || directionDot < -0.001)
        {
            return false;
        }
    }
    gk::FVector3d mixed{};
    double weightSum = 0.0;
    for (uint32_t index = 0; index < anchorCount; ++index)
    {
        const auto& anchor = anchors[index];
        if (anchor.chainIndex == 0xFFFFFFFFu)
        {
            mixed.value[0] += anchor.constantPosition.value[0] * anchor.weight;
            mixed.value[1] += anchor.constantPosition.value[1] * anchor.weight;
            mixed.value[2] += anchor.constantPosition.value[2] * anchor.weight;
        }
        else
        {
            const auto position = EvaluateAnchorPoint(anchor, positions);
            mixed.value[0] += position.value[0] * anchor.weight;
            mixed.value[1] += position.value[1] * anchor.weight;
            mixed.value[2] += position.value[2] * anchor.weight;
        }
        weightSum += anchor.weight;
    }
    (void)body;
    const gk::FVector3d bodyCenter = { { 0.0, 0.0, 0.0 } };
    return fabs(weightSum - 1.0) <= 1.0e-12 && Distance(mixed, bodyCenter) >= body.radius * 0.998;
}

bool TestOpposedAnchorsUseTheirWeightedSkinPosition(float bodyRadius)
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, bodyRadius };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error) || prepared.Count() != 1u)
    {
        return Fail("could not prepare the independent body sphere fixture");
    }

    const gk::FVector3d roots[2] = { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } } };
    const gk::FVector3d initialChildren[2] = { { { 1.0, 0.0, 0.0 } }, { { -1.0, 0.0, 0.0 } } };
    const gk::FVector3d initialBlend = WeightedPosition(initialChildren[0], initialChildren[1], 0.5, 0.5);
    if (Distance(initialBlend, roots[0]) >= sourceBody.radius || Distance(initialChildren[0], roots[0]) <= sourceBody.radius || Distance(initialChildren[1], roots[1]) <= sourceBody.radius)
    {
        return Fail("the independent fixture must start with only the blended point inside the body sphere");
    }

    FSecondaryMotionContactChain chains[2]{};
    chains[0].firstPoint = 0;
    chains[0].firstSegment = 0;
    chains[0].segmentCount = 1;
    chains[0].cosineLimit = 0.0;
    chains[0].shapes = prepared.Data();
    chains[0].shapeCount = prepared.Count();
    chains[1].firstPoint = 2;
    chains[1].firstSegment = 1;
    chains[1].segmentCount = 1;
    chains[1].cosineLimit = 0.0;
    chains[1].shapes = prepared.Data();
    chains[1].shapeCount = prepared.Count();

    const FSecondaryMotionReferenceSegment references[2] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { -1.0, 0.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    anchors[0].chainIndex = 0;
    anchors[0].segmentIndex = 0;
    anchors[0].definition.direction = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.offset = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.5;
    anchors[1].chainIndex = 1;
    anchors[1].segmentIndex = 0;
    anchors[1].definition.direction = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.offset = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.5;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.firstAnchor = 0;
    contact.anchorCount = 2;
    contact.shapes = prepared.Data();
    contact.shapeCount = prepared.Count();

    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(roots[0]) || !positions.Append(initialChildren[0]) || !positions.Append(roots[1]) || !positions.Append(initialChildren[1]))
    {
        return Fail("could not allocate the weighted contact positions");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 2, &contact, 1, positions, error))
    {
        fprintf(stderr, "weighted blend radius %.6f failed: %s\n", bodyRadius, error.CStr());
        return Fail(error.Empty() ? "the solver rejected a feasible weighted blend fixture" : error.CStr());
    }
    if (!IsSolved(positions, anchors, 2, prepared.At(0), roots[0], roots[1]))
    {
        return Fail("the solver did not clear the blended point while preserving roots, lengths, and allowed angles");
    }
    const FSecondaryMotionWeightedContactAnchor reversedAnchors[2] = { anchors[1], anchors[0] };
    gk::Array<gk::FVector3d> reversedPositions;
    if (!reversedPositions.Append(roots[0]) || !reversedPositions.Append(initialChildren[0]) || !reversedPositions.Append(roots[1]) || !reversedPositions.Append(initialChildren[1]) || !ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, reversedAnchors, 2, &contact, 1, reversedPositions, error) || !IsSolved(reversedPositions, reversedAnchors, 2, prepared.At(0), roots[0], roots[1]))
    {
        return Fail(error.Empty() ? "reversing anchor order changed a feasible blend into an invalid result" : error.CStr());
    }
    return true;
}

bool TestUnrelatedChainKeepsItsCurrentDirectionDuringWeightedSeeding()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.99f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error))
    {
        return Fail("could not prepare the independent weighted body sphere");
    }
    const gk::FVector3d roots[3] = { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, { { 10.0, 0.0, 0.0 } } };
    const gk::FVector3d initialChildren[3] = { { { 1.0, 0.0, 0.0 } }, { { -1.0, 0.0, 0.0 } }, { { 10.6, 0.8, 0.0 } } };
    FSecondaryMotionContactChain chains[3]{};
    for (uint32_t index = 0; index < 3u; ++index)
    {
        chains[index].firstPoint = index * 2u;
        chains[index].firstSegment = index;
        chains[index].segmentCount = 1;
        chains[index].cosineLimit = 0.0;
    }
    chains[0].shapes = prepared.Data();
    chains[0].shapeCount = prepared.Count();
    chains[1].shapes = prepared.Data();
    chains[1].shapeCount = prepared.Count();
    const FSecondaryMotionReferenceSegment references[3] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { -1.0, 0.0, 0.0 } }, 1.0 }, { { { 0.0, 1.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    anchors[0].chainIndex = 0;
    anchors[0].segmentIndex = 0;
    anchors[0].definition.direction = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.offset = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.5;
    anchors[1].chainIndex = 1;
    anchors[1].segmentIndex = 0;
    anchors[1].definition.direction = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.offset = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.5;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.anchorCount = 2;
    contact.shapes = prepared.Data();
    contact.shapeCount = prepared.Count();
    gk::Array<gk::FVector3d> positions;
    for (uint32_t index = 0; index < 3u; ++index)
    {
        if (!positions.Append(roots[index]) || !positions.Append(initialChildren[index]))
        {
            return Fail("could not allocate the three-chain weighted fixture");
        }
    }
    if (fabs(Distance(roots[2], initialChildren[2]) - 1.0) > 1.0e-12 || (initialChildren[2].value[1] - roots[2].value[1]) < 0.0)
    {
        return Fail("the unrelated chain must begin at a valid non-reference direction");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 3, references, 3, anchors, 2, &contact, 1, positions, error))
    {
        fprintf(stderr, "unrelated-chain weighted solve failed: %s\n", error.CStr());
        return Fail(error.Empty() ? "the solver rejected a feasible weighted fixture with an unrelated chain" : error.CStr());
    }
    if (positions.Count() != 6u || Distance(positions.At(4), roots[2]) > 1.0e-12 || Distance(positions.At(5), initialChildren[2]) > 1.0e-12 || fabs(Distance(positions.At(4), positions.At(5)) - 1.0) > 1.0e-12)
    {
        return Fail("weighted-contact seed generation changed a chain unrelated to all contacts");
    }
    const gk::FVector3d solvedFirstDirection = { { positions.At(1).value[0] - positions.At(0).value[0], positions.At(1).value[1] - positions.At(0).value[1], positions.At(1).value[2] - positions.At(0).value[2] } };
    const gk::FVector3d solvedSecondDirection = { { positions.At(3).value[0] - positions.At(2).value[0], positions.At(3).value[1] - positions.At(2).value[1], positions.At(3).value[2] - positions.At(2).value[2] } };
    const gk::FVector3d solvedWeighted = WeightedPosition(EvaluateAnchorPoint(anchors[0], positions), EvaluateAnchorPoint(anchors[1], positions), 0.5, 0.5);
    const gk::FVector3d bodyCenter = { { 0.0, 0.0, 0.0 } };
    if (Distance(positions.At(0), roots[0]) > 1.0e-12 || Distance(positions.At(2), roots[1]) > 1.0e-12 || fabs(Distance(positions.At(0), positions.At(1)) - 1.0) > 0.0011 || fabs(Distance(positions.At(2), positions.At(3)) - 1.0) > 0.0011 || solvedFirstDirection.value[0] < -0.001 || solvedSecondDirection.value[0] > 0.001 || Distance(solvedWeighted, bodyCenter) < prepared.At(0).radius * 0.998)
    {
        return Fail("the contact chains lost root, length, cone, or weighted-body clearance");
    }
    return true;
}

bool TestTinyBodyContactDoesNotOvercorrect()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 1.0f, 0.0009989f, 0.0f }, { 1.0f, 0.0009989f, 0.0f }, 0.001f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error) || prepared.Count() != 1u)
    {
        return Fail("could not prepare the tiny body-contact sphere");
    }
    FSecondaryMotionContactChain chain{};
    chain.firstPoint = 0;
    chain.firstSegment = 0;
    chain.segmentCount = 1;
    chain.cosineLimit = 0.0;
    chain.shapes = prepared.Data();
    chain.shapeCount = prepared.Count();
    const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d initialChild = { { 1.0, 0.0, 0.0 } };
    const gk::FVector3d sphereCenter = { { sourceBody.start.x, sourceBody.start.y, sourceBody.start.z } };
    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(root) || !positions.Append(initialChild))
    {
        return Fail("could not allocate the tiny body-contact points");
    }
    if (Distance(initialChild, sphereCenter) >= sourceBody.radius * 0.999)
    {
        return Fail("the tiny contact fixture must begin just inside the solver's clearance threshold");
    }
    const double witnessY = -0.0000002;
    const double witnessX = sqrt(1.0 - witnessY * witnessY);
    const gk::FVector3d clearWitness = { { witnessX, witnessY, 0.0 } };
    if (fabs(Distance(root, clearWitness) - 1.0) > 1.0e-12 || Distance(clearWitness, sphereCenter) < sourceBody.radius * 0.999 || witnessY > 0.0)
    {
        return Fail("the independent tiny-angle witness does not preserve length and clear the body sphere");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, nullptr, 0, nullptr, 0, positions, error))
    {
        fprintf(stderr, "tiny body-contact solve failed: %s\n", error.CStr());
        return Fail(error.Empty() ? "the solver rejected a feasible tiny body-contact witness" : error.CStr());
    }
    const gk::FVector3d& solvedChild = positions.At(1);
    const double solvedDisplacement = Distance(initialChild, solvedChild);
    if (positions.Count() != 2u || Distance(positions.At(0), root) > 1.0e-12 || fabs(Distance(root, solvedChild) - 1.0) > 0.0011 || (solvedChild.value[0] - root.value[0]) < 0.0 || Distance(solvedChild, sphereCenter) < sourceBody.radius * 0.999 || solvedDisplacement >= 0.001)
    {
        return Fail("the tiny body contact either remained penetrating or moved the chain far beyond the nearby clear solution");
    }
    return true;
}

bool TestRotatedCurrentPoseKeepsNearbyClearCorrection()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.9999f, 0.0f }, { 0.0f, 0.9999f, 0.0f }, 0.001f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error))
    {
        return Fail("could not prepare the rotated-pose contact sphere");
    }
    FSecondaryMotionContactChain chain{};
    chain.firstPoint = 0;
    chain.firstSegment = 0;
    chain.segmentCount = 1;
    chain.cosineLimit = 0.0;
    chain.shapes = prepared.Data();
    chain.shapeCount = prepared.Count();
    const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d initialChild = { { 0.0, 1.0, 0.0 } };
    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(root) || !positions.Append(initialChild))
    {
        return Fail("could not allocate the rotated-pose contact points");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, nullptr, 0, nullptr, 0, positions, error))
    {
        return Fail(error.Empty() ? "the solver rejected a nearby clear rotated-pose correction" : error.CStr());
    }
    const gk::FVector3d& solvedChild = positions.At(1);
    if (Distance(positions.At(0), root) > 1.0e-12 || fabs(Distance(root, solvedChild) - 1.0) > 0.0011 || Distance(solvedChild, initialChild) >= 0.003 || Distance(solvedChild, { { sourceBody.start.x, sourceBody.start.y, sourceBody.start.z } }) < sourceBody.radius * 0.999)
    {
        return Fail("the nearby rotated-pose contact was cleared by a large pose jump");
    }
    return true;
}

bool TestBodyDirectionPrefersNearbyClearPoseToClearReference()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.9999f, 0.0f }, { 0.0f, 0.9999f, 0.0f }, 0.001f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error))
    {
        return Fail("could not prepare the body-direction local-clearance sphere");
    }
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d reference = { { 1.0, 0.0, 0.0 } };
    const gk::FVector3d initial = { { 0.0, 1.0, 0.0 } };
    gk::FVector3d direction = initial;
    if (!FindPreparedSecondaryMotionCollisionDirection(prepared.Data(), prepared.Count(), root, 1.0, false, reference, 0.0, direction, error, 0.5))
    {
        return Fail(error.Empty() ? "the body-direction solver rejected a nearby clear witness" : error.CStr());
    }
    const gk::FVector3d endpoint = { { direction.value[0], direction.value[1], direction.value[2] } };
    const gk::FVector3d bodyCenter = { { sourceBody.start.x, sourceBody.start.y, sourceBody.start.z } };
    if (Distance(direction, initial) >= 0.003 || fabs(Distance(root, endpoint) - 1.0) > 1.0e-12 || Distance(endpoint, bodyCenter) < sourceBody.radius * 0.999)
    {
        return Fail("the body-direction solver snapped from a nearby clear pose to the distant reference");
    }
    return true;
}

bool TestThreeAnchorsAndInvalidInputsAreAtomic()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.4f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error))
    {
        return Fail("could not prepare the three-anchor body sphere");
    }
    FSecondaryMotionContactChain chains[2]{};
    chains[0].firstPoint = 0;
    chains[0].firstSegment = 0;
    chains[0].segmentCount = 1;
    chains[0].cosineLimit = 0.0;
    chains[0].shapes = prepared.Data();
    chains[0].shapeCount = 1;
    chains[1].firstPoint = 2;
    chains[1].firstSegment = 1;
    chains[1].segmentCount = 1;
    chains[1].cosineLimit = 0.0;
    chains[1].shapes = prepared.Data();
    chains[1].shapeCount = 1;
    const FSecondaryMotionReferenceSegment references[2] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { -1.0, 0.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchors[3]{};
    anchors[0].chainIndex = 0;
    anchors[0].definition.direction = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.offset = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.4;
    anchors[1].chainIndex = 1;
    anchors[1].definition.direction = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.offset = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.4;
    anchors[2].chainIndex = 0xFFFFFFFFu;
    anchors[2].constantPosition = { { 0.0, 0.0, 0.0 } };
    anchors[2].weight = 0.2;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.anchorCount = 3;
    contact.shapes = prepared.Data();
    contact.shapeCount = 1;
    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d initial[4] = { { { 0.0, 0.0, 0.0 } }, { { 1.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, { { -1.0, 0.0, 0.0 } } };
    if (!positions.AppendRange(initial, 4))
    {
        return Fail("could not allocate the three-anchor positions");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error))
    {
        return Fail(error.Empty() ? "the solver rejected a feasible fixed-plus-moving three-anchor point" : error.CStr());
    }
    const gk::FVector3d firstRoot = initial[0];
    const gk::FVector3d secondRoot = initial[2];
    if (!IsSolved(positions, anchors, 3, prepared.At(0), firstRoot, secondRoot))
    {
        return Fail("the solver did not clear a three-anchor blend with a fixed contribution");
    }

    gk::FVector3d snapshot[4];
    memcpy(snapshot, positions.Data(), sizeof(snapshot));
    if (ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error, 0) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("zero weighted-contact iterations were accepted or changed the pose");
    }
    if (ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error, 33) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("more than 32 weighted-contact iterations were accepted or changed the pose");
    }
    anchors[2].weight = 0.0;
    if (ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("a zero influence weight was accepted or changed the pose on rejection");
    }
    anchors[2].weight = 0.2;
    anchors[1].definition.direction.value[0] = NAN;
    if (ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("a non-finite moving anchor was accepted or changed the pose on rejection");
    }
    anchors[1].definition.direction.value[0] = -1.0;
    gk::SetAllocationFailureAfterForTesting(0);
    const bool allocationResult = ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error);
    gk::ResetAllocationFailureForTesting();
    if (allocationResult || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("allocation failure did not preserve the weighted contact pose");
    }
    return true;
}

bool TestAntiparallelInitialDirectionFindsClearSeed()
{
    using namespace gk::model::animation;
    const FModelSecondaryMotionCollisionShape body = { { -2.0f, 0.0f, 0.0f }, { -2.0f, 0.0f, 0.0f }, 0.8f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&body, 1, prepared, error))
    {
        return Fail("could not prepare the antiparallel weighted-contact body");
    }

    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 1;
    chain.cosineLimit = -1.0;
    chain.shapes = prepared.Data();
    chain.shapeCount = 1;
    const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0;
    anchor.definition.direction = { { 1.0, 0.0, 0.0 } };
    anchor.definition.offset = { { 2.0, 0.0, 0.0 } };
    anchor.definition.cosineLimit = -1.0;
    anchor.weight = 1.0;
    const FSecondaryMotionWeightedContactPoint contact = { 0, 1, prepared.Data(), 1 };
    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d antiparallelChild = { { -1.0, 0.0, 0.0 } };
    const gk::FVector3d feasibleChild = { { 0.0, 1.0, 0.0 } };
    const gk::FVector3d initialAnchor = { { -2.0, 0.0, 0.0 } };
    const gk::FVector3d feasibleAnchor = { { 0.0, 2.0, 0.0 } };
    const gk::FVector3d bodyCenter = { { -2.0, 0.0, 0.0 } };
    if (!positions.Append(root) || !positions.Append(antiparallelChild) || Distance(initialAnchor, bodyCenter) >= prepared.At(0).radius || Distance(feasibleAnchor, bodyCenter) <= prepared.At(0).radius)
    {
        return Fail("the independent antiparallel input or alternate clear witness is invalid");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, &anchor, 1, &contact, 1, positions, error))
    {
        return Fail(error.Empty() ? "the solver did not recover from an antiparallel initial direction" : error.CStr());
    }
    const double length = Distance(positions.At(0), positions.At(1));
    const gk::FVector3d actualDirection = { { positions.At(1).value[0] / length, positions.At(1).value[1] / length, positions.At(1).value[2] / length } };
    const gk::FVector3d actualAnchor = { { actualDirection.value[0] * 2.0, actualDirection.value[1] * 2.0, actualDirection.value[2] * 2.0 } };
    if (Distance(positions.At(0), root) > 1.0e-12 || fabs(length - 1.0) > 0.0011 || Distance(actualAnchor, bodyCenter) < prepared.At(0).radius * 0.998)
    {
        return Fail("the antiparallel solve changed its root, bone length, or weighted contact clearance");
    }
    return true;
}

bool TestRootZeroOffsetAndFixedOnlyContacts()
{
    using namespace gk::model::animation;
    const FModelSecondaryMotionCollisionShape nearBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.4f };
    const FModelSecondaryMotionCollisionShape farBody = { { 2.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, 0.4f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> nearPrepared;
    gk::Array<FPreparedSecondaryMotionCollisionShape> farPrepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&nearBody, 1, nearPrepared, error) || !PrepareSecondaryMotionCollisionShapes(&farBody, 1, farPrepared, error))
    {
        return Fail("could not prepare fixed-root weighted-contact shapes");
    }

    const FSecondaryMotionReferenceSegment reference = { { { 0.0, 1.0, 0.0 } }, 1.0 };
    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 1;
    chain.cosineLimit = 0.0;
    chain.shapes = nearPrepared.Data();
    chain.shapeCount = 1;
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0;
    anchor.definition.direction = { { 0.0, 1.0, 0.0 } };
    anchor.definition.offset = {};
    anchor.definition.cosineLimit = 0.0;
    anchor.weight = 1.0;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.anchorCount = 1;
    contact.shapes = nearPrepared.Data();
    contact.shapeCount = 1;
    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d initial[2] = { { { 0.0, 0.0, 0.0 } }, { { 0.0, 1.0, 0.0 } } };
    if (!positions.AppendRange(initial, 2))
    {
        return Fail("could not allocate root zero-offset positions");
    }
    gk::FVector3d snapshot[2];
    memcpy(snapshot, positions.Data(), sizeof(snapshot));
    if (ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, &anchor, 1, &contact, 1, positions, error) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("a zero-offset root anchor was moved out of an impossible body contact");
    }

    chain.shapes = farPrepared.Data();
    contact.shapes = farPrepared.Data();
    contact.shapeCount = 1;
    memcpy(positions.Data(), snapshot, sizeof(snapshot));
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, &anchor, 1, &contact, 1, positions, error) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("a clear zero-offset root anchor did not remain a fixed point");
    }

    FSecondaryMotionWeightedContactAnchor fixedAnchor{};
    fixedAnchor.chainIndex = 0xFFFFFFFFu;
    fixedAnchor.constantPosition = { { 1.0, 0.0, 0.0 } };
    fixedAnchor.weight = 1.0;
    FSecondaryMotionWeightedContactPoint fixedContact = { 0, 1, nearPrepared.Data(), 1 };
    gk::Array<gk::FVector3d> noPositions;
    if (!ResolvePreparedSecondaryMotionWeightedContacts(nullptr, 0, nullptr, 0, &fixedAnchor, 1, &fixedContact, 1, noPositions, error) || noPositions.Count() != 0)
    {
        return Fail("a purely fixed contact outside the body was not accepted without chains");
    }
    fixedAnchor.constantPosition = {};
    if (ResolvePreparedSecondaryMotionWeightedContacts(nullptr, 0, nullptr, 0, &fixedAnchor, 1, &fixedContact, 1, noPositions, error) || noPositions.Count() != 0)
    {
        return Fail("an impossible purely fixed contact was accepted without chains");
    }
    return true;
}

bool TestMultiSegmentWeightedOffsetsUseAncestorRotation()
{
    using namespace gk::model::animation;
    const FModelSecondaryMotionCollisionShape body = { { 0.65f, 1.1f, 0.05f }, { 0.65f, 1.1f, 0.05f }, 0.2f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&body, 1, prepared, error))
    {
        return Fail("could not prepare the two-segment weighted body sphere");
    }

    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 2;
    chain.cosineLimit = 0.0;
    chain.shapes = prepared.Data();
    chain.shapeCount = 1;
    const FSecondaryMotionReferenceSegment references[2] = { { { { 0.0, 1.0, 0.0 } }, 1.0 }, { { { 1.0, 0.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    anchors[0].chainIndex = 0;
    anchors[0].segmentIndex = 0;
    anchors[0].definition.direction = references[0].direction;
    anchors[0].definition.offset = { { 0.3, 1.0, 0.0 } };
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.5;
    anchors[1].chainIndex = 0;
    anchors[1].segmentIndex = 1;
    anchors[1].definition.direction = references[1].direction;
    anchors[1].definition.offset = { { 1.0, 0.2, 0.1 } };
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.5;
    const FSecondaryMotionWeightedContactPoint contact = { 0, 2, prepared.Data(), 1 };
    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d initial[3] = { root, { { 0.0, 1.0, 0.0 } }, { { 1.0, 1.0, 0.0 } } };
    if (!positions.AppendRange(initial, 3))
    {
        return Fail("could not allocate the two-segment weighted positions");
    }
    const gk::FVector3d initialFirstAnchor = { { 0.3, 1.0, 0.0 } };
    const gk::FVector3d initialSecondAnchor = { { 1.0, 1.2, 0.1 } };
    const gk::FVector3d initialBlend = WeightedPosition(initialFirstAnchor, initialSecondAnchor, 0.5, 0.5);
    const gk::FVector3d witness[3] = { root, { { 0.0, 0.0, 1.0 } }, { { 1.0, 0.0, 1.0 } } };
    gk::Array<gk::FVector3d> witnessPositions;
    if (!witnessPositions.AppendRange(witness, 3))
    {
        return Fail("could not allocate the independent two-segment witness");
    }
    const gk::FVector3d witnessBlend = WeightedPosition(EvaluateAnchorPoint(anchors[0], witnessPositions), EvaluateAnchorPoint(anchors[1], witnessPositions), 0.5, 0.5);
    const gk::FVector3d bodyCenter = { { body.start.x, body.start.y, body.start.z } };
    if (Distance(initialBlend, bodyCenter) >= body.radius || Distance(witnessBlend, bodyCenter) <= body.radius)
    {
        return Fail("the independent multi-segment witness does not separate the weighted point");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, references, 2, anchors, 2, &contact, 1, positions, error))
    {
        return Fail(error.Empty() ? "the solver rejected a feasible multi-segment ancestor-offset fixture" : error.CStr());
    }
    if (Distance(positions.At(0), root) > 1.0e-12)
    {
        return Fail("the multi-segment solve moved its fixed root");
    }
    for (uint32_t segment = 0; segment < 2; ++segment)
    {
        const auto& parent = positions.At(segment);
        const auto& child = positions.At(segment + 1u);
        const double length = Distance(parent, child);
        const double directionDot = ((child.value[0] - parent.value[0]) * references[segment].direction.value[0] + (child.value[1] - parent.value[1]) * references[segment].direction.value[1] + (child.value[2] - parent.value[2]) * references[segment].direction.value[2]) / length;
        if (fabs(length - references[segment].length) > 0.0011 || directionDot < -0.001)
        {
            return Fail("the multi-segment solve changed a reference length or cone direction");
        }
    }
    const gk::FVector3d solvedBlend = WeightedPosition(EvaluateAnchorPoint(anchors[0], positions), EvaluateAnchorPoint(anchors[1], positions), 0.5, 0.5);
    if (Distance(solvedBlend, bodyCenter) < prepared.At(0).radius * 0.998)
    {
        return Fail("rotated ancestor offsets left the weighted point inside the body sphere");
    }
    return true;
}

double SegmentDistance(const gk::FVector3d& firstStart, const gk::FVector3d& firstEnd, const gk::FVector3d& secondStart, const gk::FVector3d& secondEnd)
{
    const double u[3] = { firstEnd.value[0] - firstStart.value[0], firstEnd.value[1] - firstStart.value[1], firstEnd.value[2] - firstStart.value[2] };
    const double v[3] = { secondEnd.value[0] - secondStart.value[0], secondEnd.value[1] - secondStart.value[1], secondEnd.value[2] - secondStart.value[2] };
    const double w[3] = { firstStart.value[0] - secondStart.value[0], firstStart.value[1] - secondStart.value[1], firstStart.value[2] - secondStart.value[2] };
    const double a = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
    const double b = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
    const double c = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    const double d = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
    const double e = v[0] * w[0] + v[1] * w[1] + v[2] * w[2];
    const double denominator = a * c - b * b;
    double firstAmount = denominator > 1.0e-20 ? (b * e - c * d) / denominator : 0.0;
    firstAmount = fmax(0.0, fmin(1.0, firstAmount));
    double secondAmount = c > 1.0e-20 ? (b * firstAmount + e) / c : 0.0;
    secondAmount = fmax(0.0, fmin(1.0, secondAmount));
    firstAmount = a > 1.0e-20 ? (b * secondAmount - d) / a : 0.0;
    firstAmount = fmax(0.0, fmin(1.0, firstAmount));
    const double delta[3] = { w[0] + u[0] * firstAmount - v[0] * secondAmount, w[1] + u[1] * firstAmount - v[1] * secondAmount, w[2] + u[2] * firstAmount - v[2] * secondAmount };
    return sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
}

bool TestWeightedSpheresCompeteWithChainBodyCapsules()
{
    using namespace gk::model::animation;
    const FModelSecondaryMotionCollisionShape bodyCapsules[2] = { { { -0.5f, -0.1f, 0.0f }, { -0.5f, 0.1f, 0.0f }, 0.18f }, { { 0.5f, -0.1f, 0.0f }, { 0.5f, 0.1f, 0.0f }, 0.18f } };
    const FModelSecondaryMotionCollisionShape weightedBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.25f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedChains[2];
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedWeightedBody;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&bodyCapsules[0], 1, preparedChains[0], error) || !PrepareSecondaryMotionCollisionShapes(&bodyCapsules[1], 1, preparedChains[1], error) || !PrepareSecondaryMotionCollisionShapes(&weightedBody, 1, preparedWeightedBody, error))
    {
        return Fail("could not prepare the competing chain capsules and weighted body sphere");
    }

    const gk::FVector3d roots[2] = { { { -1.0, 0.0, 0.0 } }, { { 1.0, 0.0, 0.0 } } };
    const gk::FVector3d initialChildren[2] = { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } } };
    const gk::FVector3d witnessChildren[2] = { { { -1.0, 1.0, 0.0 } }, { { 1.0, 1.0, 0.0 } } };
    const FSecondaryMotionReferenceSegment references[2] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { -1.0, 0.0, 0.0 } }, 1.0 } };
    FSecondaryMotionContactChain chains[2]{};
    chains[0].firstPoint = 0;
    chains[0].firstSegment = 0;
    chains[0].segmentCount = 1;
    chains[0].cosineLimit = 0.0;
    chains[0].shapes = preparedChains[0].Data();
    chains[0].shapeCount = 1;
    chains[1].firstPoint = 2;
    chains[1].firstSegment = 1;
    chains[1].segmentCount = 1;
    chains[1].cosineLimit = 0.0;
    chains[1].shapes = preparedChains[1].Data();
    chains[1].shapeCount = 1;
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    anchors[0].chainIndex = 0;
    anchors[0].definition.direction = references[0].direction;
    anchors[0].definition.offset = references[0].direction;
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.5;
    anchors[1].chainIndex = 1;
    anchors[1].definition.direction = references[1].direction;
    anchors[1].definition.offset = references[1].direction;
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.5;
    const FSecondaryMotionWeightedContactPoint contact = { 0, 2, preparedWeightedBody.Data(), 1 };

    const gk::FVector3d initialWeighted = WeightedPosition(initialChildren[0], initialChildren[1], 0.5, 0.5);
    const gk::FVector3d witnessWeighted = WeightedPosition(witnessChildren[0], witnessChildren[1], 0.5, 0.5);
    const gk::FVector3d weightedCenter = { { weightedBody.start.x, weightedBody.start.y, weightedBody.start.z } };
    if (Distance(initialWeighted, weightedCenter) >= weightedBody.radius || Distance(witnessWeighted, weightedCenter) <= weightedBody.radius)
    {
        return Fail("the independent weighted-sphere input and clear witness are inconsistent");
    }
    for (uint32_t chain = 0; chain < 2; ++chain)
    {
        if (SegmentDistance(roots[chain], initialChildren[chain], gk::FVector3d{ { bodyCapsules[chain].start.x, bodyCapsules[chain].start.y, bodyCapsules[chain].start.z } }, gk::FVector3d{ { bodyCapsules[chain].end.x, bodyCapsules[chain].end.y, bodyCapsules[chain].end.z } }) >= bodyCapsules[chain].radius || SegmentDistance(roots[chain], witnessChildren[chain], gk::FVector3d{ { bodyCapsules[chain].start.x, bodyCapsules[chain].start.y, bodyCapsules[chain].start.z } }, gk::FVector3d{ { bodyCapsules[chain].end.x, bodyCapsules[chain].end.y, bodyCapsules[chain].end.z } }) <= bodyCapsules[chain].radius)
        {
            return Fail("the independent chain-capsule input and clear witness are inconsistent");
        }
    }

    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d initial[4] = { roots[0], initialChildren[0], roots[1], initialChildren[1] };
    if (!positions.AppendRange(initial, 4))
    {
        return Fail("could not allocate the competing contact fixture positions");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 2, &contact, 1, positions, error, 32))
    {
        fprintf(stderr, "weighted capsule competition failed: %s\n", error.CStr());
        return Fail(error.Empty() ? "the solver rejected a known feasible capsule and weighted-sphere pose" : error.CStr());
    }

    if (positions.Count() != 4 || Distance(positions.At(0), roots[0]) > 1.0e-12 || Distance(positions.At(2), roots[1]) > 1.0e-12)
    {
        return Fail("the capsule competition solve moved a fixed chain root");
    }
    for (uint32_t chain = 0; chain < 2; ++chain)
    {
        const auto& child = positions.At(chain * 2u + 1u);
        const double length = Distance(roots[chain], child);
        const double dot = ((child.value[0] - roots[chain].value[0]) * references[chain].direction.value[0] + (child.value[1] - roots[chain].value[1]) * references[chain].direction.value[1] + (child.value[2] - roots[chain].value[2]) * references[chain].direction.value[2]) / length;
        const gk::FVector3d bodyStart = { { bodyCapsules[chain].start.x, bodyCapsules[chain].start.y, bodyCapsules[chain].start.z } };
        const gk::FVector3d bodyEnd = { { bodyCapsules[chain].end.x, bodyCapsules[chain].end.y, bodyCapsules[chain].end.z } };
        if (fabs(length - references[chain].length) > 0.0011 || dot < -0.001 || SegmentDistance(roots[chain], child, bodyStart, bodyEnd) < bodyCapsules[chain].radius * 0.997)
        {
            return Fail("the solve violated a chain length, cone, or independent capsule-clearance check");
        }
    }
    const gk::FVector3d solvedWeighted = WeightedPosition(EvaluateAnchorPoint(anchors[0], positions), EvaluateAnchorPoint(anchors[1], positions), 0.5, 0.5);
    if (Distance(solvedWeighted, weightedCenter) < weightedBody.radius * 0.997)
    {
        return Fail("the solve left the weighted offset-sphere contact inside the body sphere");
    }
    return true;
}
}

int main()
{
    return TestOpposedAnchorsUseTheirWeightedSkinPosition(0.8f) && TestOpposedAnchorsUseTheirWeightedSkinPosition(0.99f) && TestUnrelatedChainKeepsItsCurrentDirectionDuringWeightedSeeding() && TestTinyBodyContactDoesNotOvercorrect() && TestRotatedCurrentPoseKeepsNearbyClearCorrection() && TestBodyDirectionPrefersNearbyClearPoseToClearReference() && TestThreeAnchorsAndInvalidInputsAreAtomic() && TestAntiparallelInitialDirectionFindsClearSeed() && TestRootZeroOffsetAndFixedOnlyContacts() && TestMultiSegmentWeightedOffsetsUseAncestorRotation() && TestWeightedSpheresCompeteWithChainBodyCapsules() ? 0 : 1;
}
