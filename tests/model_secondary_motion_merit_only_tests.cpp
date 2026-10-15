// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "model/animation/ModelSecondaryMotionChainContacts.h"
#include "model/animation/ModelSecondaryMotionWeightedContacts.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "foundation/Memory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace gk::model::animation
{
/**
 * この専用targetだけで従来trialとの数値比較を有効にする。
 */
void SetSecondaryMotionWeightedFullTrialForTesting(bool enabled);
/**
 * solver内部の同一候補について、微分の有無だけを変えた侵入量を比較する。
 */
bool EvaluateSecondaryMotionWeightedMeritForTesting(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& positions, bool calculateGradients, bool useClearanceMargin, double& merit, gk::String& error);
}

namespace
{
using namespace gk;
using namespace gk::model::animation;

/**
 * 同じ最小回転で位置が一致し、位置専用評価が微分を計算しないことを確かめる。
 */
bool TestPositionEvaluationSkipsJacobian()
{
    // 軸に沿わない回転とoffsetで、単純な固定点評価へ退化させない。
    FSecondaryMotionAttachmentDefinition definition{};
    definition.direction = { { 1.0, 2.0, 3.0 } };
    definition.offset = { { 0.37, -0.29, 0.81 } };
    const FVector3d parent{ { 0.23, -0.41, 0.13 } };
    const FVector3d child{ { 0.63, 0.19, -0.07 } };
    FSecondaryMotionAttachmentEvaluation full{};
    FVector3d position{};
    String error;
    ResetSecondaryMotionAttachmentJacobianCountForTesting();
    if (!EvaluateSecondaryMotionAttachment(definition, parent, child, full, error) || GetSecondaryMotionAttachmentJacobianCountForTesting() != 1u)
    {
        return false;
    }
    ResetSecondaryMotionAttachmentJacobianCountForTesting();
    if (!EvaluateSecondaryMotionAttachmentPosition(definition, parent, child, position, error) || memcmp(&position, &full.position, sizeof(position)) != 0 || GetSecondaryMotionAttachmentJacobianCountForTesting() != 0u)
    {
        fprintf(stderr, "position-only attachment changed position or evaluated its Jacobian: count=%llu\n", static_cast<unsigned long long>(GetSecondaryMotionAttachmentJacobianCountForTesting()));
        return false;
    }
    return true;
}

/**
 * 位置専用評価も入力不正・反対向き・数値範囲外で出力を保つことを確かめる。
 */
bool TestPositionFailureAtomicity()
{
    FSecondaryMotionAttachmentDefinition definition{};
    definition.direction = { { 1.0, 0.0, 0.0 } };
    definition.offset = { { 0.7, 0.2, 0.1 } };
    const FVector3d sentinel{ { 11.0, 22.0, 33.0 } };
    const FVector3d invalidChildren[3] = { {}, { { -1.0, 0.0, 0.0 } }, { { NAN, 0.0, 0.0 } } };
    String error;
    for (const auto& child : invalidChildren)
    {
        FVector3d position = sentinel;
        if (EvaluateSecondaryMotionAttachmentPosition(definition, {}, child, position, error) || error.Empty() || memcmp(&position, &sentinel, sizeof(position)) != 0)
        {
            return false;
        }
    }
    // 位置だけ有限でも、極小線分で元の微分計算が成立しない場合は同じく拒否する。
    definition.offset = {};
    const FVector3d tinyChild{ { 1.0e-320, 0.0, 0.0 } };
    FSecondaryMotionAttachmentEvaluation full{};
    FVector3d position = sentinel;
    if (EvaluateSecondaryMotionAttachment(definition, {}, tinyChild, full, error) || EvaluateSecondaryMotionAttachmentPosition(definition, {}, tinyChild, position, error) || error.Empty() || memcmp(&position, &sentinel, sizeof(position)) != 0)
    {
        return false;
    }
    // 正規化は成立しても微分だけが桁あふれする値を使い、位置専用の数値検査も残す。
    definition.offset = { { 1.0e38, 1.0e38, 0.0 } };
    const FVector3d overflowChild{ { 1.0e-300, 0.0, 0.0 } };
    if (EvaluateSecondaryMotionAttachment(definition, {}, overflowChild, full, error) || EvaluateSecondaryMotionAttachmentPosition(definition, {}, overflowChild, position, error) || error.Empty() || memcmp(&position, &sentinel, sizeof(position)) != 0)
    {
        return false;
    }
    return true;
}

/**
 * 身体線分とbone-local線分の侵入量を、同じ候補で微分なしでも厳密に再現する。
 */
bool TestChainMeritMatchesWithoutJacobian()
{
    const FModelSecondaryMotionCollisionShape sphere{ { 0.1f, 0.1f, 0.0f }, { 0.1f, 0.1f, 0.0f }, 0.25f };
    Array<FPreparedSecondaryMotionCollisionShape> shapes;
    String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sphere, 1u, shapes, error))
    {
        return false;
    }
    const FSecondaryMotionReferenceSegment reference{ { { 1.0, 0.0, 0.0 } }, 1.0 };
    const FVector3d positions[2] = { {}, { { 0.8, 0.6, 0.0 } } };
    FSecondaryMotionChainAttachmentContact attachment{};
    attachment.firstDefinition.direction = reference.direction;
    attachment.secondDefinition.direction = reference.direction;
    attachment.firstDefinition.offset = { { 0.05, 0.03, 0.0 } };
    attachment.secondDefinition.offset = { { 0.3, 0.07, 0.0 } };
    attachment.shapes = shapes.Data();
    attachment.shapeCount = shapes.Count();
    for (uint32_t margin = 0u; margin < 2u; ++margin)
    {
        double fullMerit = 0.0;
        double onlyMerit = 0.0;
        FVector3d gradient{};
        ResetSecondaryMotionAttachmentJacobianCountForTesting();
        if (!EvaluatePreparedSecondaryMotionChainContactsMerit(shapes.Data(), shapes.Count(), &reference, 1u, -1.0, 1.0, &attachment, 1u, positions, 2u, fullMerit, &gradient, 1u, error, nullptr, 0u, margin != 0u) || GetSecondaryMotionAttachmentJacobianCountForTesting() != 2u)
        {
            return false;
        }
        ResetSecondaryMotionAttachmentJacobianCountForTesting();
        if (!EvaluatePreparedSecondaryMotionChainContactsMerit(shapes.Data(), shapes.Count(), &reference, 1u, -1.0, 1.0, &attachment, 1u, positions, 2u, onlyMerit, nullptr, 0u, error, nullptr, 0u, margin != 0u) || !(fullMerit > 0.0) || memcmp(&fullMerit, &onlyMerit, sizeof(fullMerit)) != 0 || GetSecondaryMotionAttachmentJacobianCountForTesting() != 0u)
        {
            fprintf(stderr, "chain merit-only mismatch: full=%.17g only=%.17g count=%llu\n", fullMerit, onlyMerit, static_cast<unsigned long long>(GetSecondaryMotionAttachmentJacobianCountForTesting()));
            return false;
        }
    }
    return true;
}

/**
 * weighted点だけが球内にある二本鎖で、merit一致とsolver全体の数値一致・微分削減を確かめる。
 */
bool TestWeightedMeritAndFinalSolveMatch()
{
    const FModelSecondaryMotionCollisionShape sphere{ {}, {}, 0.8f };
    Array<FPreparedSecondaryMotionCollisionShape> shapes;
    String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sphere, 1u, shapes, error))
    {
        return false;
    }
    FSecondaryMotionContactChain chains[2]{};
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    const FSecondaryMotionReferenceSegment references[2] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { -1.0, 0.0, 0.0 } }, 1.0 } };
    const FVector3d initial[4] = { {}, { { 1.0, 0.0, 0.0 } }, {}, { { -1.0, 0.0, 0.0 } } };
    Array<FVector3d> fullPositions;
    Array<FVector3d> fastPositions;
    if (!fullPositions.AppendRange(initial, 4u) || !fastPositions.AppendRange(initial, 4u))
    {
        return false;
    }
    for (uint32_t index = 0u; index < 2u; ++index)
    {
        chains[index].firstPoint = index * 2u;
        chains[index].firstSegment = index;
        chains[index].segmentCount = 1u;
        chains[index].cosineLimit = 0.0;
        chains[index].shapes = shapes.Data();
        chains[index].shapeCount = shapes.Count();
        anchors[index].chainIndex = index;
        anchors[index].definition.direction = references[index].direction;
        anchors[index].definition.offset = references[index].direction;
        anchors[index].definition.cosineLimit = 0.0;
        anchors[index].weight = 0.5;
    }
    FSecondaryMotionWeightedContactPoint contact{};
    contact.anchorCount = 2u;
    contact.shapes = shapes.Data();
    contact.shapeCount = shapes.Count();
    for (uint32_t margin = 0u; margin < 2u; ++margin)
    {
        double fullMerit = 0.0;
        double onlyMerit = 0.0;
        ResetSecondaryMotionAttachmentJacobianCountForTesting();
        if (!EvaluateSecondaryMotionWeightedMeritForTesting(chains, 2u, references, anchors, 2u, &contact, 1u, fullPositions, true, margin != 0u, fullMerit, error) || GetSecondaryMotionAttachmentJacobianCountForTesting() != 2u)
        {
            return false;
        }
        ResetSecondaryMotionAttachmentJacobianCountForTesting();
        // 中心距離0なので、独立式の侵入量は半径から既存許容幅を引いた値になる。
        const double penetration = shapes.At(0u).radius - shapes.At(0u).tolerance * 0.5 + (margin != 0u ? shapes.At(0u).tolerance * 0.125 : 0.0);
        if (!EvaluateSecondaryMotionWeightedMeritForTesting(chains, 2u, references, anchors, 2u, &contact, 1u, fullPositions, false, margin != 0u, onlyMerit, error) || memcmp(&fullMerit, &onlyMerit, sizeof(fullMerit)) != 0 || fabs(onlyMerit - penetration * penetration) > 1.0e-15 || GetSecondaryMotionAttachmentJacobianCountForTesting() != 0u)
        {
            fprintf(stderr, "weighted merit-only mismatch: full=%.17g only=%.17g expected=%.17g\n", fullMerit, onlyMerit, penetration * penetration);
            return false;
        }
    }
    ResetSecondaryMotionAttachmentJacobianCountForTesting();
    SetSecondaryMotionWeightedFullTrialForTesting(true);
    const bool fullSolved = ResolvePreparedSecondaryMotionWeightedContacts(chains, 2u, references, 2u, anchors, 2u, &contact, 1u, fullPositions, error);
    const uint64_t fullWork = GetSecondaryMotionAttachmentJacobianCountForTesting();
    SetSecondaryMotionWeightedFullTrialForTesting(false);
    ResetSecondaryMotionAttachmentJacobianCountForTesting();
    const bool fastSolved = ResolvePreparedSecondaryMotionWeightedContacts(chains, 2u, references, 2u, anchors, 2u, &contact, 1u, fastPositions, error);
    const uint64_t fastWork = GetSecondaryMotionAttachmentJacobianCountForTesting();
    if (!fullSolved || !fastSolved || memcmp(fullPositions.Data(), fastPositions.Data(), sizeof(initial)) != 0 || !(fastWork < fullWork))
    {
        fprintf(stderr, "weighted full/merit-only solve mismatch: fullSolved=%u fastSolved=%u fullWork=%llu fastWork=%llu error=%s\n", fullSolved ? 1u : 0u, fastSolved ? 1u : 0u, static_cast<unsigned long long>(fullWork), static_cast<unsigned long long>(fastWork), error.CStr());
        return false;
    }
    // 許容幅は既存と同じ値を使い、解のweighted中心が身体から離れたことも独立に検査する。
    const double x = 0.5 * (fastPositions.At(1u).value[0] + fastPositions.At(3u).value[0]);
    const double y = 0.5 * (fastPositions.At(1u).value[1] + fastPositions.At(3u).value[1]);
    const double z = 0.5 * (fastPositions.At(1u).value[2] + fastPositions.At(3u).value[2]);
    if (sqrt(x * x + y * y + z * z) < sphere.radius * 0.999)
    {
        return false;
    }
    // 保存候補の所有配列は最後まで保ち、途中の確保失敗でも部分結果を公開しない。
    FVector3d saved[4]{};
    memcpy(saved, fastPositions.Data(), sizeof(saved));
    SetAllocationFailureAfterForTesting(0u);
    const bool allocationAccepted = ResolvePreparedSecondaryMotionWeightedContacts(chains, 2u, references, 2u, anchors, 2u, &contact, 1u, fastPositions, error);
    ResetAllocationFailureForTesting();
    if (allocationAccepted || memcmp(saved, fastPositions.Data(), sizeof(saved)) != 0)
    {
        return false;
    }
    fprintf(stdout, "weighted merit-only identical positions: fullJacobian=%llu meritOnlyJacobian=%llu\n", static_cast<unsigned long long>(fullWork), static_cast<unsigned long long>(fastWork));
    return true;
}
}

int main()
{
    return TestPositionEvaluationSkipsJacobian() && TestPositionFailureAtomicity() && TestChainMeritMatchesWithoutJacobian() && TestWeightedMeritAndFinalSolveMatch() ? 0 : 1;
}
