// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionContactReach.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "foundation/Memory.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
using namespace gk;
using namespace gk::model::animation;

/**
 * 入力位置と結果番号を保ち、不可能の証明と可解性不明を区別して確認する。
 */
bool CheckReach(const char* name, const FSecondaryMotionContactChain* chains, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const Array<FVector3d>& positions, bool expectedUnreachable, uint32_t expectedPoint = 0u, uint32_t expectedShape = 0u)
{
    std::vector<FVector3d> before;
    for (uint32_t point = 0u; point < positions.Count(); ++point)
    {
        before.push_back(positions.At(point));
    }
    uint32_t pointIndex = 701u;
    uint32_t shapeIndex = 709u;
    const bool unreachable = FindUnreachableSecondaryMotionWeightedContact(chains, references, anchors, contacts, contactCount, positions, pointIndex, shapeIndex);
    if (unreachable != expectedUnreachable || pointIndex != (unreachable ? expectedPoint : 701u) || shapeIndex != (unreachable ? expectedShape : 709u) || positions.Count() != before.size() || (!before.empty() && memcmp(before.data(), positions.Data(), before.size() * sizeof(FVector3d)) != 0))
    {
        fprintf(stderr, "contact reach contract failed: %s unreachable=%d expected=%d point=%u shape=%u\n", name, unreachable ? 1 : 0, expectedUnreachable ? 1 : 0, pointIndex, shapeIndex);
        return false;
    }
    return true;
}

/**
 * 球またはカプセルを既存の準備関数で作る。
 */
bool Prepare(const Vec3& start, const Vec3& end, float radius, Array<FPreparedSecondaryMotionCollisionShape>& shapes)
{
    const FModelSecondaryMotionCollisionShape source = { start, end, radius };
    String error;
    return PrepareSecondaryMotionCollisionShapes(&source, 1u, shapes, error);
}

/**
 * 固定寄与だけの点を入力不正扱いせず、内部と外部を区別する。
 */
bool TestFixedOnlyAndCapsule()
{
    Array<FPreparedSecondaryMotionCollisionShape> sphere;
    Array<FPreparedSecondaryMotionCollisionShape> capsule;
    if (!Prepare({}, {}, 1.0f, sphere) || !Prepare({ 0.0f, -2.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, 0.5f, capsule))
    {
        return false;
    }
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.weight = 1.0;
    FSecondaryMotionWeightedContactPoint contact = { 0u, 1u, sphere.Data(), 1u };
    Array<FVector3d> positions;
    bool passed = CheckReach("fixed inside sphere", nullptr, nullptr, &anchor, &contact, 1u, positions, true);
    anchor.constantPosition = { { 2.0, 0.0, 0.0 } };
    passed = CheckReach("fixed outside sphere", nullptr, nullptr, &anchor, &contact, 1u, positions, false) && passed;
    contact.shapes = capsule.Data();
    anchor.constantPosition = { { 0.1, 0.0, 0.0 } };
    passed = CheckReach("fixed inside capsule span", nullptr, nullptr, &anchor, &contact, 1u, positions, true) && passed;
    anchor.constantPosition = { { 0.0, 2.2, 0.0 } };
    passed = CheckReach("fixed inside capsule cap", nullptr, nullptr, &anchor, &contact, 1u, positions, true) && passed;
    anchor.constantPosition = { { 0.0, 3.0, 0.0 } };
    passed = CheckReach("fixed outside capsule cap", nullptr, nullptr, &anchor, &contact, 1u, positions, false) && passed;
    return passed;
}

/**
 * 非零root offsetの回転範囲と、内部boneの上流の長さを到達範囲へ含める。
 */
bool TestMovingRootAndAncestorReach()
{
    Array<FPreparedSecondaryMotionCollisionShape> largeBody;
    Array<FPreparedSecondaryMotionCollisionShape> offsetBody;
    if (!Prepare({}, {}, 1.0f, largeBody) || !Prepare({ 0.1f, 0.0f, 0.0f }, { 0.1f, 0.0f, 0.0f }, 0.05f, offsetBody))
    {
        return false;
    }
    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 2u;
    chain.cosineLimit = -1.0;
    const FSecondaryMotionReferenceSegment references[2] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { 0.0, 1.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0u;
    anchor.definition.direction = references[0].direction;
    anchor.definition.offset = { { 0.1, 0.0, 0.0 } };
    anchor.definition.cosineLimit = -1.0;
    anchor.weight = 1.0;
    FSecondaryMotionWeightedContactPoint contact = { 0u, 1u, largeBody.Data(), 1u };
    const FVector3d initial[3] = { {}, { { 1.0, 0.0, 0.0 } }, { { 1.0, 1.0, 0.0 } } };
    Array<FVector3d> positions;
    if (!positions.AppendRange(initial, 3u))
    {
        return false;
    }
    bool passed = CheckReach("nonzero root offset wholly inside sphere", &chain, references, &anchor, &contact, 1u, positions, true);
    const double boundaryRootX = largeBody.At(0u).radius - largeBody.At(0u).tolerance - 0.1;
    positions.At(0u).value[0] = boundaryRootX;
    positions.At(1u).value[0] = boundaryRootX + 1.0;
    positions.At(2u).value[0] = boundaryRootX + 1.0;
    passed = CheckReach("moving reach ball tangent to accepted boundary", &chain, references, &anchor, &contact, 1u, positions, false) && passed;
    positions.At(0u).value[0] = std::nextafter(boundaryRootX, 0.0);
    passed = CheckReach("moving reach ball one double inside boundary", &chain, references, &anchor, &contact, 1u, positions, false) && passed;
    memcpy(positions.Data(), initial, sizeof(initial));
    contact.shapes = offsetBody.Data();
    passed = CheckReach("nonzero root offset can rotate out", &chain, references, &anchor, &contact, 1u, positions, false) && passed;
    anchor.segmentIndex = 1u;
    anchor.definition.direction = references[1].direction;
    anchor.definition.offset = {};
    Array<FPreparedSecondaryMotionCollisionShape> childBody;
    if (!Prepare({ 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.25f, childBody))
    {
        return false;
    }
    contact.shapes = childBody.Data();
    passed = CheckReach("internal zero offset still follows ancestor", &chain, references, &anchor, &contact, 1u, positions, false) && passed;
    // 上流を回した独立な可行姿勢でも、到達不能と誤分類しない。
    positions.At(1u) = { { 0.0, 1.0, 0.0 } };
    positions.At(2u) = { { 0.0, 2.0, 0.0 } };
    passed = CheckReach("clear rotated ancestor witness", &chain, references, &anchor, &contact, 1u, positions, false) && passed;
    return passed;
}

/**
 * 固定と可動の原weightを保ち、点ごとの寄与範囲と身体形状番号を取り違えない。
 */
bool TestWeightsAndContactGroups()
{
    Array<FPreparedSecondaryMotionCollisionShape> body;
    if (!Prepare({}, {}, 0.1f, body))
    {
        return false;
    }
    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 1u;
    chain.cosineLimit = -1.0;
    const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    anchors[0].weight = 0.999;
    anchors[1].chainIndex = 0u;
    anchors[1].definition = { reference.direction, { { 1.0, 0.0, 0.0 } }, -1.0 };
    anchors[1].weight = 0.001;
    FSecondaryMotionWeightedContactPoint contact = { 0u, 2u, body.Data(), 1u };
    const FVector3d points[2] = { {}, { { 1.0, 0.0, 0.0 } } };
    Array<FVector3d> positions;
    if (!positions.AppendRange(points, 2u))
    {
        return false;
    }
    bool passed = CheckReach("fixed dominant mixed point cannot escape", &chain, &reference, anchors, &contact, 1u, positions, true);
    anchors[0].weight = 0.5;
    anchors[1].weight = 0.5;
    passed = CheckReach("mixed point with enough moving weight", &chain, &reference, anchors, &contact, 1u, positions, false) && passed;
    // private solverは正のweight合計を受け付けるため、診断だけで再正規化してはならない。
    FSecondaryMotionWeightedContactAnchor unnormalized{};
    unnormalized.constantPosition = { { 0.075, 0.0, 0.0 } };
    unnormalized.weight = 2.0;
    const FSecondaryMotionWeightedContactPoint unnormalizedContact = { 0u, 1u, body.Data(), 1u };
    Array<FVector3d> noPositions;
    passed = CheckReach("original weight is not renormalized per point", nullptr, nullptr, &unnormalized, &unnormalizedContact, 1u, noPositions, false) && passed;
    const FModelSecondaryMotionCollisionShape sourceShapes[2] = { { { 10.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, 0.5f }, { {}, {}, 0.5f } };
    Array<FPreparedSecondaryMotionCollisionShape> groups;
    String error;
    if (!PrepareSecondaryMotionCollisionShapes(sourceShapes, 2u, groups, error))
    {
        return false;
    }
    FSecondaryMotionWeightedContactAnchor fixed[2]{};
    fixed[0].weight = 1.0;
    fixed[0].constantPosition = { { 20.0, 0.0, 0.0 } };
    fixed[1].weight = 1.0;
    const FSecondaryMotionWeightedContactPoint contacts[2] = { { 0u, 1u, groups.Data(), 2u }, { 1u, 1u, groups.Data(), 2u } };
    passed = CheckReach("second point second body shape index", nullptr, nullptr, fixed, contacts, 2u, noPositions, true, 1u, 1u) && passed;
    return passed;
}

/**
 * 許容境界と丸め誤差が重なる値は、到達不能を断定せず可解性不明に残す。
 */
bool TestBoundaryTranslationCancellationAndScale()
{
    Array<FPreparedSecondaryMotionCollisionShape> body;
    if (!Prepare({}, {}, 4.0f, body))
    {
        return false;
    }
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.weight = 1.0;
    const FSecondaryMotionWeightedContactPoint contact = { 0u, 1u, body.Data(), 1u };
    Array<FVector3d> noPositions;
    const double boundary = body.At(0u).radius - body.At(0u).tolerance;
    anchor.constantPosition = { { boundary, 0.0, 0.0 } };
    bool passed = CheckReach("exact accepted contact boundary", nullptr, nullptr, &anchor, &contact, 1u, noPositions, false);
    anchor.constantPosition.value[0] = std::nextafter(boundary, 0.0);
    passed = CheckReach("first double inside boundary remains uncertain", nullptr, nullptr, &anchor, &contact, 1u, noPositions, false) && passed;
    anchor.constantPosition.value[0] = boundary - 0.01;
    passed = CheckReach("clear separation from boundary proves containment", nullptr, nullptr, &anchor, &contact, 1u, noPositions, true) && passed;
    Array<FPreparedSecondaryMotionCollisionShape> translatedBody;
    const float translation = 1048576.0f;
    if (!Prepare({ translation, 0.0f, 0.0f }, { translation, 0.0f, 0.0f }, 1.0f, translatedBody))
    {
        return false;
    }
    const FSecondaryMotionWeightedContactPoint translatedContact = { 0u, 1u, translatedBody.Data(), 1u };
    anchor.constantPosition = { { static_cast<double>(translation) + translatedBody.At(0u).radius - translatedBody.At(0u).tolerance, 0.0, 0.0 } };
    passed = CheckReach("translated near boundary remains uncertain", nullptr, nullptr, &anchor, &translatedContact, 1u, noPositions, false) && passed;
    // 実数の重み付き和は0.5だが、通常の加算順では大きな項が小さな項を失って0になる。
    const double large = 18014398509481984.0;
    FSecondaryMotionWeightedContactAnchor cancelling[3]{};
    cancelling[0].constantPosition = { { large, 0.0, 0.0 } };
    cancelling[0].weight = 0.25;
    cancelling[1].constantPosition = { { 1.0, 0.0, 0.0 } };
    cancelling[1].weight = 0.5;
    cancelling[2].constantPosition = { { -large, 0.0, 0.0 } };
    cancelling[2].weight = 0.25;
    Array<FPreparedSecondaryMotionCollisionShape> cancellationBody;
    if (!Prepare({}, {}, 0.5f, cancellationBody))
    {
        return false;
    }
    const FSecondaryMotionWeightedContactPoint cancellationContact = { 0u, 3u, cancellationBody.Data(), 1u };
    passed = CheckReach("large cancelling coordinates cannot prove false containment", nullptr, nullptr, cancelling, &cancellationContact, 1u, noPositions, false) && passed;
    const float scales[3] = { 0.000001f, 1.0f, 1000000.0f };
    for (float scale : scales)
    {
        Array<FPreparedSecondaryMotionCollisionShape> scaledBody;
        if (!Prepare({}, {}, scale, scaledBody))
        {
            return false;
        }
        anchor.constantPosition = {};
        const FSecondaryMotionWeightedContactPoint scaledContact = { 0u, 1u, scaledBody.Data(), 1u };
        passed = CheckReach("deep fixed containment across scales", nullptr, nullptr, &anchor, &scaledContact, 1u, noPositions, true) && passed;
        FSecondaryMotionContactChain chain{};
        chain.segmentCount = 1u;
        chain.cosineLimit = -1.0;
        const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, scale };
        FSecondaryMotionWeightedContactAnchor moving{};
        moving.chainIndex = 0u;
        moving.definition = { reference.direction, { { scale * 0.1, scale * 0.2, scale * -0.1 } }, -1.0 };
        moving.weight = 1.0;
        const FVector3d scaledPoints[2] = { { { scale * 0.2, 0.0, 0.0 } }, { { scale * 1.2, 0.0, 0.0 } } };
        Array<FVector3d> positions;
        if (!positions.AppendRange(scaledPoints, 2u))
        {
            return false;
        }
        passed = CheckReach("moving offset reach ball across scales", &chain, &reference, &moving, &scaledContact, 1u, positions, true) && passed;
    }
    return passed;
}

/**
 * 診断は確保を行わず、solverの不成立・入力不正・確保失敗は位置を保持して異なる既存経路を通す。
 */
bool TestSolverDiagnosticsAndAllocationAtomicity()
{
    Array<FPreparedSecondaryMotionCollisionShape> body;
    if (!Prepare({}, {}, 1.0f, body))
    {
        return false;
    }
    FSecondaryMotionWeightedContactAnchor fixed{};
    fixed.weight = 1.0;
    const FSecondaryMotionWeightedContactPoint contact = { 0u, 1u, body.Data(), 1u };
    Array<FVector3d> noPositions;
    uint32_t pointIndex = 17u;
    uint32_t shapeIndex = 19u;
    SetAllocationFailureAfterForTesting(0u);
    const bool allocationFree = FindUnreachableSecondaryMotionWeightedContact(nullptr, nullptr, &fixed, &contact, 1u, noPositions, pointIndex, shapeIndex);
    ResetAllocationFailureForTesting();
    bool passed = allocationFree && pointIndex == 0u && shapeIndex == 0u;
    String error;
    if (ResolvePreparedSecondaryMotionWeightedContacts(nullptr, 0u, nullptr, 0u, &fixed, 1u, &contact, 1u, noPositions, error) || strstr(error.CStr(), "unreachable: point 0, shape 0") == nullptr || noPositions.Count() != 0u)
    {
        fprintf(stderr, "fixed-only solver did not distinguish unreachable contact: %s\n", error.CStr());
        passed = false;
    }
    fixed.constantPosition = { { 2.0, 0.0, 0.0 } };
    if (!ResolvePreparedSecondaryMotionWeightedContacts(nullptr, 0u, nullptr, 0u, &fixed, 1u, &contact, 1u, noPositions, error) || !error.Empty())
    {
        passed = false;
    }
    fixed.constantPosition = {};
    fixed.weight = 0.0;
    if (ResolvePreparedSecondaryMotionWeightedContacts(nullptr, 0u, nullptr, 0u, &fixed, 1u, &contact, 1u, noPositions, error) || strcmp(error.CStr(), "Secondary motion weighted anchor weight is invalid") != 0)
    {
        fprintf(stderr, "reach diagnosis masked existing invalid-input error: %s\n", error.CStr());
        passed = false;
    }
    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 1u;
    chain.cosineLimit = -1.0;
    const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    FSecondaryMotionWeightedContactAnchor moving{};
    moving.chainIndex = 0u;
    moving.definition = { reference.direction, { { 2.0, 0.0, 0.0 } }, -1.0 };
    moving.weight = 1.0;
    const FVector3d points[2] = { {}, { { 1.0, 0.0, 0.0 } } };
    Array<FVector3d> positions;
    if (!positions.AppendRange(points, 2u))
    {
        return false;
    }
    moving.definition.offset = { { 0.1, 0.0, 0.0 } };
    if (ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1u, &reference, 1u, &moving, 1u, &contact, 1u, positions, error) || strstr(error.CStr(), "unreachable: point 0, shape 0") == nullptr || positions.Count() != 2u || memcmp(points, positions.Data(), sizeof(points)) != 0)
    {
        fprintf(stderr, "unreachable moving reach ball changed published positions or diagnosis: %s\n", error.CStr());
        passed = false;
    }
    moving.definition.offset = { { 2.0, 0.0, 0.0 } };
    if (!error.Assign("reserved diagnostic storage for deterministic allocation failure after all fixture input arrays have been prepared and validated"))
    {
        return false;
    }
    SetAllocationFailureAfterForTesting(0u);
    const bool accepted = ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1u, &reference, 1u, &moving, 1u, &contact, 1u, positions, error);
    ResetAllocationFailureForTesting();
    if (accepted || strstr(error.CStr(), "allocation") == nullptr || positions.Count() != 2u || memcmp(points, positions.Data(), sizeof(points)) != 0)
    {
        fprintf(stderr, "reach integration changed allocation failure or its positions: %s\n", error.CStr());
        passed = false;
    }
    return passed;
}

/**
 * 狭い可行方向を持つ匿名の停滞fixtureを、到達不能と誤分類しない。
 */
bool TestKnownFeasibleStallRemainsUnknown()
{
    FModelSecondaryMotionCollisionShape sourceBodies[3]{};
    const double witness[3] = { 0.3, 0.4, sqrt(0.75) };
    for (uint32_t axis = 0u; axis < 3u; ++axis)
    {
        sourceBodies[axis].start = { axis == 0u ? -1.0f : 0.0f, axis == 1u ? -1.0f : 0.0f, axis == 2u ? -1.0f : 0.0f };
        sourceBodies[axis].end = sourceBodies[axis].start;
        sourceBodies[axis].radius = static_cast<float>(sqrt(2.0 + 2.0 * (witness[axis] - 0.003)));
    }
    Array<FPreparedSecondaryMotionCollisionShape> body;
    Array<FPreparedSecondaryMotionCollisionShape> guardedBody;
    String error;
    FModelSecondaryMotionCollisionShape guardedSources[3] = { sourceBodies[0], sourceBodies[1], sourceBodies[2] };
    for (auto& shape : guardedSources)
    {
        shape.radius = static_cast<float>(shape.radius * 1.001);
    }
    if (!PrepareSecondaryMotionCollisionShapes(sourceBodies, 3u, body, error) || !PrepareSecondaryMotionCollisionShapes(guardedSources, 3u, guardedBody, error))
    {
        return false;
    }
    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 2u;
    chain.cosineLimit = -1.0;
    chain.shapes = guardedBody.Data();
    chain.shapeCount = 3u;
    const FSecondaryMotionReferenceSegment references[2] = { { { { 0.0, 1.0, 0.0 } }, 1.0 }, { { { 0.0, 1.0, 0.0 } }, 0.25 } };
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0u;
    anchor.segmentIndex = 1u;
    anchor.definition = { references[1].direction, {}, -1.0 };
    anchor.weight = 1.0;
    const FSecondaryMotionWeightedContactPoint contact = { 0u, 1u, body.Data(), 3u };
    const FVector3d initial[3] = { {}, { { 0.0, 1.0, 0.0 } }, { { 0.0, 1.25, 0.0 } } };
    Array<FVector3d> positions;
    Array<FVector3d> clear;
    const FVector3d clearPoints[3] = { {}, { { witness[0], witness[1], witness[2] } }, { { witness[0] * 1.25, witness[1] * 1.25, witness[2] * 1.25 } } };
    if (!positions.AppendRange(initial, 3u) || !clear.AppendRange(clearPoints, 3u))
    {
        return false;
    }
    bool passed = CheckReach("known feasible narrow region remains unknown", &chain, references, &anchor, &contact, 1u, positions, false);
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1u, references, 2u, &anchor, 1u, &contact, 1u, clear, error, 1u))
    {
        fprintf(stderr, "independent clear witness for reach-unknown fixture failed: %s\n", error.CStr());
        return false;
    }
    const bool accepted = ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1u, references, 2u, &anchor, 1u, &contact, 1u, positions, error, 1u);
    if (accepted || !error.Empty() || positions.Count() != 3u || memcmp(initial, positions.Data(), sizeof(initial)) != 0)
    {
        fprintf(stderr, "reach proof confused search stall with unreachable geometry: %s\n", error.CStr());
        passed = false;
    }
    return passed;
}

/**
 * 反平行境界で回転の成立と不成立を分け、実回転で退避できる姿勢は到達不能にしない。
 */
bool TestNearAntiparallelClearWitness()
{
    // 反対方向の境界だけを調べる、単位X方向と非零offset。
    const FSecondaryMotionAttachmentDefinition boundaryDefinition = { { { 1.0, 0.0, 0.0 } }, { { 0.0, 1.0, 0.0 } }, -1.0 };
    // Runtimeの回転と診断で共有する実部の成立境界。
    const double threshold = kSecondaryMotionAttachmentAntiparallelThreshold;
    // doubleの丸め幅より離れた、境界直下と直上の比率。
    const double boundaryFractions[2] = { 0.998, 1.002 };
    // 実評価と独立接触検査の失敗理由。
    String error;
    // 不成立側から成立側の順に確認する番号。
    for (uint32_t side = 0u; side < 2u; ++side)
    {
        // X軸のほぼ反対方向を向く、単位長に近い子位置。
        const FVector3d child = { { -1.0, sqrt(2.0 * threshold * boundaryFractions[side]), 0.0 } };
        // 実部の大小を、入力方向の正規化から独立に確認する成分。
        const double normalizedX = child.value[0] / sqrt(child.value[0] * child.value[0] + child.value[1] * child.value[1]);
        // 基準が単位X方向の場合の、最小回転の実部。
        const double scalar = 1.0 + normalizedX;
        // 境界直上の場合だけ回転が成立する。
        const bool expectedDefined = side == 1u;
        // 失敗時の位置と微分の保持を調べる出力。
        FSecondaryMotionAttachmentEvaluation evaluation{};
        evaluation.position = { { 17.0, 19.0, 23.0 } };
        // 特異な入力で上書きされてはいけない評価値。
        const FSecondaryMotionAttachmentEvaluation before = evaluation;
        // Runtimeの実際の四元数回転と微分を通した成否。
        const bool defined = EvaluateSecondaryMotionAttachment(boundaryDefinition, {}, child, evaluation, error);
        if ((scalar > threshold) != expectedDefined || defined != expectedDefined || (defined && !error.Empty()) || (!defined && (error.Empty() || memcmp(&before, &evaluation, sizeof(evaluation)) != 0)))
        {
            fprintf(stderr, "near-antiparallel definition boundary failed: scalar=%.17g threshold=%.17g defined=%d error=%s\n", scalar, threshold, defined ? 1 : 0, error.CStr());
            return false;
        }
        // 探索で使う位置専用評価も、同じ境界と失敗原子性を保つ。
        FVector3d position = before.position;
        const bool positionDefined = EvaluateSecondaryMotionAttachmentPosition(boundaryDefinition, {}, child, position, error);
        if (positionDefined != expectedDefined || (positionDefined && (memcmp(&position, &evaluation.position, sizeof(position)) != 0 || !error.Empty())) || (!positionDefined && (error.Empty() || memcmp(&position, &before.position, sizeof(position)) != 0)))
        {
            fprintf(stderr, "near-antiparallel position-only boundary failed: scalar=%.17g error=%s\n", scalar, error.CStr());
            return false;
        }
        // 不成立側はsolverの事前評価を通らないため、到達診断の入力にはしない。
    }

    // 斜めのほぼ反対方向では、正規化と四元数の丸めで実offsetが理想長よりわずかに伸びる。
    const FVector3d baseDirection = { { -0.20540646525614004, -0.8691682489227499, 0.19687759979996966 } };
    // 同じ基準方向を単位長にした、solverの参照方向とoffset。
    const FVector3d referenceDirection = { { -0.22459781465813128, -0.9503755835282743, 0.21527208802834677 } };
    // 最小回転が成立し、丸めでoffsetが伸びることを再現する子位置。
    const FVector3d child = { { 0.22459643756539777, 0.9503759089701563, -0.21527208802834677 } };
    // 一つの可動骨へ実weightをそのまま与える接触寄与。
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0u;
    anchor.definition = { baseDirection, referenceDirection, -1.0 };
    anchor.weight = 1.0;
    // 理想的な長さ保存を仮定せずに得る、実回転後の位置。
    FSecondaryMotionAttachmentEvaluation witness{};
    if (!EvaluateSecondaryMotionAttachment(anchor.definition, {}, child, witness, error))
    {
        fprintf(stderr, "near-antiparallel clear witness is undefined: %s\n", error.CStr());
        return false;
    }
    // 理想offsetの球を覆い、実評価の位置は外にある身体形状。
    Array<FPreparedSecondaryMotionCollisionShape> body;
    if (!Prepare({}, {}, 1.0013f, body))
    {
        return false;
    }
    // 実評価位置が通常weightedの許容幅でも外にあることを示す値。
    double penetration = 0.0;
    // 点への縮約でも接触queryへ渡す線分上の割合。
    double spanAmount = 0.0;
    // 接触queryが返す身体外向き方向。
    FVector3d normal{};
    // 理想回転の長さではなく、実評価位置を通常weightedと同じ半分許容で独立に検査する。
    if (!QueryPreparedSecondaryMotionFreeSegmentContact(body.At(0u), witness.position, witness.position, penetration, spanAmount, normal, error, 0.5) || penetration > 0.0)
    {
        fprintf(stderr, "near-antiparallel witness did not clear the body: penetration=%.17g error=%s\n", penetration, error.CStr());
        return false;
    }
    // 全回転を許す、固定rootと一つの自由方向の鎖。
    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 1u;
    chain.cosineLimit = -1.0;
    // 基準方向と保持する単位骨長。
    const FSecondaryMotionReferenceSegment reference = { referenceDirection, 1.0 };
    // 独立にclearと確認した身体形状を適用する接触点。
    const FSecondaryMotionWeightedContactPoint contact = { 0u, 1u, body.Data(), 1u };
    // 到達診断で固定するrootと、実評価した子位置。
    const FVector3d points[2] = { {}, child };
    // 診断による書き換えがないことも既存helperで確認する配列。
    Array<FVector3d> positions;
    if (!positions.AppendRange(points, 2u))
    {
        return false;
    }
    return CheckReach("actual near-antiparallel rotation has a clear witness", &chain, &reference, &anchor, &contact, 1u, positions, false);
}
}

int main()
{
    bool passed = TestFixedOnlyAndCapsule();
    passed = TestMovingRootAndAncestorReach() && passed;
    passed = TestWeightsAndContactGroups() && passed;
    passed = TestBoundaryTranslationCancellationAndScale() && passed;
    passed = TestSolverDiagnosticsAndAllocationAtomicity() && passed;
    passed = TestKnownFeasibleStallRemainsUnknown() && passed;
    passed = TestNearAntiparallelClearWitness() && passed;
    return passed ? 0 : 1;
}
