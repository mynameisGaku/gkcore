// SPDX-License-Identifier: NOASSERTION
#include "foundation/Array.h"
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "model/animation/ModelSecondaryMotionCollision.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

using namespace gk::model::animation;

/**
 * 失敗理由を表示してテストを停止する。
 */
bool Fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return false;
}

/**
 * 三成分が許容誤差内で一致するか調べる。
 */
bool NearVector(const gk::FVector3d& actual, const gk::FVector3d& expected, double tolerance)
{
    return fabs(actual.value[0] - expected.value[0]) <= tolerance && fabs(actual.value[1] - expected.value[1]) <= tolerance && fabs(actual.value[2] - expected.value[2]) <= tolerance;
}

/**
 * 接触点の位置と子位置に対する数値微分を、評価APIのヤコビアンと照合する。
 */
bool TestAttachmentEvaluationAndJacobian()
{
    FSecondaryMotionAttachmentDefinition definition{};
    definition.direction = { { 0.0, 1.0, 0.0 } };
    definition.offset = { { 0.0, -0.07, 0.02 } };
    definition.cosineLimit = -1.0;
    const gk::FVector3d parent = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d child = { { 0.0, 1.0, 0.0 } };
    FSecondaryMotionAttachmentEvaluation evaluation{};
    gk::String error;
    if (!EvaluateSecondaryMotionAttachment(definition, parent, child, evaluation, error))
        return Fail(error.CStr());
    const gk::FVector3d expectedPosition = { { 0.0, -0.07, 0.02 } };
    if (!NearVector(evaluation.position, expectedPosition, 1.0e-12))
        return Fail("rest-pose attachment offset did not produce its expected contact position");

    const double step = 1.0e-6;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        gk::FVector3d positive = child;
        gk::FVector3d negative = child;
        positive.value[axis] += step;
        negative.value[axis] -= step;
        FSecondaryMotionAttachmentEvaluation positiveEvaluation{};
        FSecondaryMotionAttachmentEvaluation negativeEvaluation{};
        if (!EvaluateSecondaryMotionAttachment(definition, parent, positive, positiveEvaluation, error) || !EvaluateSecondaryMotionAttachment(definition, parent, negative, negativeEvaluation, error))
            return Fail(error.CStr());
        for (uint32_t row = 0; row < 3; ++row)
        {
            const double numerical = (positiveEvaluation.position.value[row] - negativeEvaluation.position.value[row]) / (2.0 * step);
            if (fabs(numerical - evaluation.childJacobian[row * 3 + axis]) > 2.0e-7)
                return Fail("attachment child Jacobian did not match an independent finite difference");
        }
    }

    definition.offset = { { 0.0, 0.1, 0.02 } };
    const gk::FVector3d quarterTurnChild = { { 1.0, 0.0, 0.0 } };
    if (!EvaluateSecondaryMotionAttachment(definition, parent, quarterTurnChild, evaluation, error))
        return Fail(error.CStr());
    const gk::FVector3d expectedQuarterTurn = { { 0.1, 0.0, 0.02 } };
    if (!NearVector(evaluation.position, expectedQuarterTurn, 1.0e-12))
        return Fail("attachment offset did not follow the known rotation mapping reference Y to current X");
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        gk::FVector3d positive = quarterTurnChild;
        gk::FVector3d negative = quarterTurnChild;
        positive.value[axis] += step;
        negative.value[axis] -= step;
        FSecondaryMotionAttachmentEvaluation positiveEvaluation{};
        FSecondaryMotionAttachmentEvaluation negativeEvaluation{};
        if (!EvaluateSecondaryMotionAttachment(definition, parent, positive, positiveEvaluation, error) || !EvaluateSecondaryMotionAttachment(definition, parent, negative, negativeEvaluation, error))
            return Fail(error.CStr());
        for (uint32_t row = 0; row < 3; ++row)
        {
            const double numerical = (positiveEvaluation.position.value[row] - negativeEvaluation.position.value[row]) / (2.0 * step);
            if (fabs(numerical - evaluation.childJacobian[row * 3 + axis]) > 2.0e-7)
                return Fail("rotated attachment child Jacobian did not match an independent finite difference");
        }
    }
    return true;
}

/**
 * 評価不能な入力で出力を保ち、準備済み形状の照会値を独立な球距離と照合する。
 */
bool TestEvaluationFailureAtomicityAndContactQuery()
{
    FSecondaryMotionAttachmentDefinition definition{};
    definition.direction = { { 0.0, 1.0, 0.0 } };
    definition.offset = { { 0.03, 0.0, 0.0 } };
    definition.cosineLimit = -1.0;
    const gk::FVector3d parent = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d invalidChild = parent;
    FSecondaryMotionAttachmentEvaluation evaluation{};
    evaluation.position = { { 8.0, 9.0, 10.0 } };
    for (uint32_t index = 0; index < 9; ++index)
        evaluation.childJacobian[index] = static_cast<double>(index) + 20.0;
    const FSecondaryMotionAttachmentEvaluation original = evaluation;
    gk::String error;
    if (EvaluateSecondaryMotionAttachment(definition, parent, invalidChild, evaluation, error) || error.Empty())
        return Fail("zero-length attachment direction was accepted or omitted its diagnostic");
    if (!NearVector(evaluation.position, original.position, 0.0))
        return Fail("failed attachment evaluation changed its output position");
    for (uint32_t index = 0; index < 9; ++index)
    {
        if (evaluation.childJacobian[index] != original.childJacobian[index])
            return Fail("failed attachment evaluation changed its output Jacobian");
    }

    const FModelSecondaryMotionCollisionShape rawShape = { { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 0.5f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    if (!PrepareSecondaryMotionFreeSegmentShapes(&rawShape, 1, 0.0f, prepared, error) || prepared.Count() != 1)
        return Fail(error.CStr());
    const gk::FVector3d first = { { 1.1, 2.0, 3.0 } };
    const gk::FVector3d second = { { 1.3, 2.0, 3.0 } };
    double penetration = -1.0;
    double spanAmount = -1.0;
    gk::FVector3d normal = { { -9.0, -9.0, -9.0 } };
    if (!QueryPreparedSecondaryMotionFreeSegmentContact(prepared.At(0), first, second, penetration, spanAmount, normal, error))
        return Fail(error.CStr());
    const double spanX = second.value[0] - first.value[0];
    const double expectedAmount = fmax(0.0, fmin(1.0, (1.0 - first.value[0]) / spanX));
    const double closestX = first.value[0] + spanX * expectedAmount;
    const double expectedPenetration = static_cast<double>(rawShape.radius) - prepared.At(0).tolerance * 0.5 - fabs(closestX - 1.0);
    if (fabs(penetration - expectedPenetration) > 1.0e-12 || fabs(spanAmount - expectedAmount) > 1.0e-12)
        return Fail("prepared sphere query returned an unexpected independent closest-point distance");
    const gk::FVector3d expectedNormal = { { 1.0, 0.0, 0.0 } };
    if (!NearVector(normal, expectedNormal, 1.0e-12))
        return Fail("prepared sphere query returned an unexpected outward normal");
    return true;
}

/**
 * 接触点だけが身体球へ入る姿勢を補正し、親子長と固定pivotを保つ。
 */
bool TestSharedBoneProjectionAndAtomicFailure()
{
    FSecondaryMotionAttachmentDefinition definition{};
    definition.direction = { { 0.0, 1.0, 0.0 } };
    definition.offset = { { 0.2, 0.0, 0.0 } };
    definition.cosineLimit = -1.0;
    const FModelSecondaryMotionCollisionShape rawShape = { { 0.17f, 0.04f, 0.0f }, { 0.17f, 0.04f, 0.0f }, 0.12f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionFreeSegmentShapes(&rawShape, 1, 0.0f, prepared, error) || prepared.Count() != 1)
        return Fail(error.CStr());

    gk::FVector3d pivot = { { 0.0, 0.0, 0.0 } };
    gk::FVector3d child = { { 0.0, 1.0, 0.0 } };
    const gk::FVector3d originalPivot = pivot;
    gk::FVector3d* points[4] = { &pivot, &child, &pivot, &child };
    const double pivotDistance = sqrt((pivot.value[0] - rawShape.start.x) * (pivot.value[0] - rawShape.start.x) + (pivot.value[1] - rawShape.start.y) * (pivot.value[1] - rawShape.start.y) + pivot.value[2] * pivot.value[2]);
    const double childDistance = sqrt((child.value[0] - rawShape.start.x) * (child.value[0] - rawShape.start.x) + (child.value[1] - rawShape.start.y) * (child.value[1] - rawShape.start.y) + child.value[2] * child.value[2]);
    if (pivotDistance <= rawShape.radius || childDistance <= rawShape.radius)
        return Fail("projection fixture did not keep the old bone positions outside the body sphere");
    FSecondaryMotionAttachmentEvaluation initialAttachment{};
    if (!EvaluateSecondaryMotionAttachment(definition, pivot, child, initialAttachment, error))
        return Fail(error.CStr());
    const double initialAttachmentDistance = sqrt((initialAttachment.position.value[0] - rawShape.start.x) * (initialAttachment.position.value[0] - rawShape.start.x) + (initialAttachment.position.value[1] - rawShape.start.y) * (initialAttachment.position.value[1] - rawShape.start.y) + initialAttachment.position.value[2] * initialAttachment.position.value[2]);
    if (initialAttachmentDistance >= rawShape.radius)
        return Fail("projection fixture did not place the offset attachment inside the body sphere");
    if (!ProjectSecondaryMotionAttachmentContacts(prepared.Data(), prepared.Count(), definition, definition, points, error))
        return Fail(error.CStr());
    if (pivot.value[0] != originalPivot.value[0] || pivot.value[1] != originalPivot.value[1] || pivot.value[2] != originalPivot.value[2])
        return Fail("attachment projection moved a fixed parent pivot");
    if (!isfinite(child.value[0]) || !isfinite(child.value[1]) || !isfinite(child.value[2]))
        return Fail("attachment projection produced a non-finite child position");
    const double childLength = sqrt(child.value[0] * child.value[0] + child.value[1] * child.value[1] + child.value[2] * child.value[2]);
    if (fabs(childLength - 1.0) > 2.0e-6)
        return Fail("attachment projection changed the shared bone length");
    FSecondaryMotionAttachmentEvaluation correctedAttachment{};
    if (!EvaluateSecondaryMotionAttachment(definition, pivot, child, correctedAttachment, error))
        return Fail(error.CStr());
    const double correctedDistance = sqrt((correctedAttachment.position.value[0] - rawShape.start.x) * (correctedAttachment.position.value[0] - rawShape.start.x) + (correctedAttachment.position.value[1] - rawShape.start.y) * (correctedAttachment.position.value[1] - rawShape.start.y) + correctedAttachment.position.value[2] * correctedAttachment.position.value[2]);
    if (correctedDistance + 2.0e-6 < rawShape.radius)
        return Fail("shared-bone attachment remained inside the independently measured body sphere");

    const FModelSecondaryMotionCollisionShape impossibleShape = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.3f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> impossiblePrepared;
    if (!PrepareSecondaryMotionFreeSegmentShapes(&impossibleShape, 1, 0.0f, impossiblePrepared, error))
        return Fail(error.CStr());
    const gk::FVector3d beforeFailurePivot = pivot;
    const gk::FVector3d beforeFailureChild = child;
    gk::FVector3d* impossiblePoints[4] = { &pivot, &child, &pivot, &child };
    if (ProjectSecondaryMotionAttachmentContacts(impossiblePrepared.Data(), impossiblePrepared.Count(), definition, definition, impossiblePoints, error) || error.Empty())
        return Fail("unreachable body sphere was accepted or omitted its diagnostic");
    if (!NearVector(pivot, beforeFailurePivot, 0.0) || !NearVector(child, beforeFailureChild, 0.0))
        return Fail("failed attachment projection changed shared-bone positions");
    return true;
}

/**
 * 形状数が0なら、重複する骨点の入力をそのまま返す。
 */
bool TestEmptyShapeProjectionIsNoOp()
{
    FSecondaryMotionAttachmentDefinition definition{};
    definition.direction = { { 0.0, 1.0, 0.0 } };
    definition.offset = { { 0.0, 0.0, 0.0 } };
    definition.cosineLimit = -1.0;
    gk::FVector3d pivot = { { -0.0, 0.0, 1.0 } };
    gk::FVector3d child = { { 0.0, 1.0, 0.0 } };
    const gk::FVector3d originalPivot = pivot;
    const gk::FVector3d originalChild = child;
    gk::FVector3d* points[4] = { &pivot, &child, &pivot, &child };
    gk::String error;
    if (!ProjectSecondaryMotionAttachmentContacts(nullptr, 0, definition, definition, points, error))
        return Fail(error.CStr());
    if (memcmp(&pivot, &originalPivot, sizeof(pivot)) != 0 || memcmp(&child, &originalChild, sizeof(child)) != 0)
        return Fail("empty-shape attachment projection changed the aliased bone positions");
    return true;
}

/**
 * 法線勾配が0でも、骨の回転で退避できる球接触を解く。
 */
bool TestStationaryContactEscapesByRotation()
{
    FSecondaryMotionAttachmentDefinition definition{};
    definition.direction = { { 0.0, 1.0, 0.0 } };
    definition.offset = { { 0.0, -0.2, 0.0 } };
    definition.cosineLimit = 0.5;
    const FModelSecondaryMotionCollisionShape body = { { 0.0f, -0.25f, 0.0f }, { 0.0f, -0.25f, 0.0f }, 0.1f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> shapes;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&body, 1, shapes, error))
        return Fail(error.CStr());
    gk::FVector3d parent = { { 0.0, 0.0, 0.0 } };
    gk::FVector3d child = { { 0.0, 1.0, 0.0 } };
    gk::FVector3d* points[4] = { &parent, &child, &parent, &child };
    if (!ProjectSecondaryMotionAttachmentContacts(shapes.Data(), 1, definition, definition, points, error))
        return Fail(error.CStr());
    FSecondaryMotionAttachmentEvaluation result{};
    if (!EvaluateSecondaryMotionAttachment(definition, parent, child, result, error))
        return Fail(error.CStr());
    const double dx = result.position.value[0];
    const double dy = result.position.value[1] + 0.25;
    const double dz = result.position.value[2];
    if (sqrt(dx * dx + dy * dy + dz * dz) < static_cast<double>(body.radius) - shapes.At(0).tolerance * 0.5 || fabs(sqrt(child.value[0] * child.value[0] + child.value[1] * child.value[1] + child.value[2] * child.value[2]) - 1.0) > 1.0e-10 || child.value[1] < 0.5 || parent.value[0] != 0.0 || parent.value[1] != 0.0 || parent.value[2] != 0.0)
        return Fail("stationary contact escape changed the pivot, length, cone, or left penetration");
    return true;
}

/**
 * 実モデルのloop中に停止した8身体形状の入力を再現する。
 */
bool TestRecordedNativeLoopContact()
{
    FSecondaryMotionAttachmentDefinition definitions[2]{};
    definitions[0].direction = { { -0.12102086792559291, -0.9922600670181646, 0.027819218674210128 } };
    definitions[0].offset = { { 0.055121781616520149, -0.012408251180976437, -0.033184064221020604 } };
    definitions[0].cosineLimit = 0.50000000000000011;
    definitions[1].direction = { { -0.067595697136062685, -0.99638636399203684, -0.051429907441274646 } };
    definitions[1].offset = { { -0.010496615836650021, -0.014841841204310802, -0.0013021308294716427 } };
    definitions[1].cosineLimit = 0.50000000000000011;
    const FModelSecondaryMotionCollisionShape bodies[8] = {
        { { 0.13322991132736206f, 0.63181227445602417f, 0.069782346487045288f }, { 0.13322991132736206f, 0.63181227445602417f, 0.069782346487045288f }, 0.080491006374359131f }, { { 0.14372947812080383f, 0.68486833572387695f, 0.072173513472080231f }, { 0.16409377753734589f, 0.77831220626831055f, 0.12651999294757843f }, 0.090491011738777161f }, { { 0.16576598584651947f, 0.78598529100418091f, 0.13098262250423431f }, { 0.1864168643951416f, 0.90991777181625366f, 0.16989630460739136f }, 0.090491019189357758f }, { { 0.19990172982215881f, 1.0398807525634766f, 0.22377495467662811f }, { 0.19990172982215881f, 1.0398807525634766f, 0.22377495467662811f }, 0.10049101710319519f }, { { 0.19379658997058868f, 0.56950467824935913f, 0.053248360753059387f }, { 0.26451277732849121f, 0.41523441672325134f, 0.17694121599197388f }, 0.060491010546684265f }, { { 0.054289072751998901f, 0.59092634916305542f, 0.055290728807449341f }, { -0.041384704411029816f, 0.42834103107452393f, 0.14755411446094513f }, 0.060491006821393967f }, { { 0.28085768222808838f, 0.36346063017845154f, 0.18456216156482697f }, { 0.32865250110626221f, 0.10340007394552231f, 0.065464906394481659f }, 0.050491005182266235f }, { { -0.072376742959022522f, 0.37751758098602295f, 0.15522494912147522f }, { -0.22286602854728699f, 0.13960157334804535f, 0.085590794682502747f }, 0.050491001456975937f },
    };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(bodies, 8, prepared, error))
    {
        fprintf(stderr, "TestRecordedNativeLoopContact: %s\n", error.CStr());
        return Fail(error.CStr());
    }
    gk::FVector3d point0 = { { 0.17687134444713593, 0.55249112844467163, 0.17959032952785492 } };
    gk::FVector3d point1 = { { 0.15347865223884583, 0.53682750463485718, 0.20362858474254608 } };
    gk::FVector3d point2 = { { 0.27204042673110962, 0.57383602857589722, 0.16277195513248444 } };
    gk::FVector3d point3 = { { 0.27176687121391296, 0.54447191953659058, 0.18639585375785828 } };
    gk::FVector3d* points[4] = { &point0, &point1, &point2, &point3 };
    const gk::FVector3d firstPivot = point0;
    const gk::FVector3d secondPivot = point2;
    // 独立方向samplingで得た姿勢を厳密照会し、この固定pivot入力が可解なことを先に確認する。
    const gk::FVector3d feasibleChildren[2] = { { { 0.19935151249935226, 0.5304907523635897, 0.16007033029607015 } }, { { 0.2718702983272517, 0.5532596472052366, 0.19434722875870258 } } };
    FSecondaryMotionAttachmentEvaluation feasibleEndpoints[2]{};
    if (!EvaluateSecondaryMotionAttachment(definitions[0], point0, feasibleChildren[0], feasibleEndpoints[0], error) || !EvaluateSecondaryMotionAttachment(definitions[1], point2, feasibleChildren[1], feasibleEndpoints[1], error) || !CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), feasibleEndpoints[0].position, feasibleEndpoints[1].position, error, 0.5))
    {
        return Fail("recorded fixture has no valid independently selected feasible pose");
    }
    if (!ProjectSecondaryMotionAttachmentContacts(prepared.Data(), prepared.Count(), definitions[0], definitions[1], points, error))
    {
        fprintf(stderr, "TestRecordedNativeLoopContact: %s\n", error.CStr());
        return Fail(error.CStr());
    }
    FSecondaryMotionAttachmentEvaluation endpoints[2]{};
    if (!EvaluateSecondaryMotionAttachment(definitions[0], point0, point1, endpoints[0], error) || !EvaluateSecondaryMotionAttachment(definitions[1], point2, point3, endpoints[1], error) || !CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), endpoints[0].position, endpoints[1].position, error, 0.5))
    {
        fprintf(stderr, "TestRecordedNativeLoopContact: %s\n", error.CStr());
        return Fail(error.CStr());
    }
    if (!NearVector(point0, firstPivot, 0.0) || !NearVector(point2, secondPivot, 0.0))
    {
        return Fail("recorded attachment projection changed its fixed pivots");
    }
    return true;
}

/**
 * 実モデルの連続再生中に停止した8身体形状の入力を再現する。
 */
bool TestRecordedNativeFirstClipContact()
{
    FSecondaryMotionAttachmentDefinition definitions[2]{};
    definitions[0].direction = { { 0.045244671217009098, -0.99663386974011825, -0.068365557214885367 } };
    definitions[0].offset = { { 0.0632503293312352, -0.0020495132406538469, 0.0023343726294258182 } };
    definitions[0].cosineLimit = 0.50000000000000011;
    definitions[1].direction = { { 0.11267144721939272, -0.99234811123000155, -0.050501199191068401 } };
    definitions[1].offset = { { -0.0033965322348548099, -0.0085985418460181179, -0.0089985410164508395 } };
    definitions[1].cosineLimit = 0.50000000000000011;
    const FModelSecondaryMotionCollisionShape bodies[8] = {
        { { 0.2886403501033783f, 0.64046907424926758f, -0.075791589915752411f }, { 0.2886403501033783f, 0.64046907424926758f, -0.075791589915752411f }, 0.081711962819099426f }, { { 0.28529661893844604f, 0.69396805763244629f, -0.068203061819076538f }, { 0.27782678604125977f, 0.79312282800674438f, -0.0211655143648386f }, 0.091711968183517456f }, { { 0.27721339464187622f, 0.80126488208770752f, -0.017303058877587318f }, { 0.27305150032043457f, 0.92954432964324951f, 0.011455483734607697f }, 0.091711975634098053f }, { { 0.27638337016105652f, 1.0643899440765381f, 0.055963553488254547f }, { 0.27638337016105652f, 1.0643899440765381f, 0.055963553488254547f }, 0.10171197354793549f }, { { 0.3560689389705658f, 0.59493070840835571f, -0.073793418705463409f }, { 0.36643895506858826f, 0.4488658607006073f, 0.076730780303478241f }, 0.061711966991424561f }, { { 0.23801782727241516f, 0.5824657678604126f, -0.1225348562002182f }, { 0.2267780601978302f, 0.37278437614440918f, -0.12525174021720886f }, 0.061711963266134262f }, { { 0.37218096852302551f, 0.39584910869598389f, 0.097120881080627441f }, { 0.41152623295783997f, 0.11098525673151016f, 0.059644054621458054f }, 0.051711961627006531f }, { { 0.2278003990650177f, 0.31575161218643188f, -0.14446192979812622f }, { 0.25519919395446777f, 0.091207712888717651f, -0.32592633366584778f }, 0.051711961627006531f },
    };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(bodies, 8, prepared, error))
    {
        fprintf(stderr, "TestRecordedNativeFirstClipContact: %s\n", error.CStr());
        return Fail(error.CStr());
    }
    gk::FVector3d point0 = { { 0.26910892128944397, 0.55390697717666626, 0.022712966427206993 } };
    gk::FVector3d point1 = { { 0.23767541348934174, 0.53386223316192627, 0.021634707227349281 } };
    gk::FVector3d point2 = { { 0.32795217633247375, 0.58005821704864502, 0.073123916983604431 } };
    gk::FVector3d point3 = { { 0.31473466753959656, 0.55357927083969116, 0.094505168497562408 } };
    gk::FVector3d* points[4] = { &point0, &point1, &point2, &point3 };
    const gk::FVector3d firstPivot = point0;
    const gk::FVector3d secondPivot = point2;
    // 独立方向samplingで得た姿勢を厳密照会し、この固定pivot入力が可解なことを先に確認する。
    const gk::FVector3d feasibleChildren[2] = { { { 0.29958010279944636, 0.5359135410288037, 0.010933188368393645 } }, { { 0.3040947234073798, 0.5594790677695735, 0.054676272087678 } } };
    FSecondaryMotionAttachmentEvaluation feasibleEndpoints[2]{};
    if (!EvaluateSecondaryMotionAttachment(definitions[0], point0, feasibleChildren[0], feasibleEndpoints[0], error) || !EvaluateSecondaryMotionAttachment(definitions[1], point2, feasibleChildren[1], feasibleEndpoints[1], error) || !CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), feasibleEndpoints[0].position, feasibleEndpoints[1].position, error, 0.5))
    {
        return Fail("recorded fixture has no valid independently selected feasible pose");
    }
    if (!ProjectSecondaryMotionAttachmentContacts(prepared.Data(), prepared.Count(), definitions[0], definitions[1], points, error))
    {
        fprintf(stderr, "TestRecordedNativeFirstClipContact: %s\n", error.CStr());
        return Fail(error.CStr());
    }
    FSecondaryMotionAttachmentEvaluation endpoints[2]{};
    if (!EvaluateSecondaryMotionAttachment(definitions[0], point0, point1, endpoints[0], error) || !EvaluateSecondaryMotionAttachment(definitions[1], point2, point3, endpoints[1], error) || !CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), endpoints[0].position, endpoints[1].position, error, 0.5))
    {
        fprintf(stderr, "TestRecordedNativeFirstClipContact: %s\n", error.CStr());
        return Fail(error.CStr());
    }
    if (!NearVector(point0, firstPivot, 0.0) || !NearVector(point2, secondPivot, 0.0))
    {
        return Fail("recorded attachment projection changed its fixed pivots");
    }
    return true;
}

/**
 * 実モデルの連続再生中に停止した8身体形状の入力を再現する。
 */
bool TestRecordedNativeSkinningContact()
{
    FSecondaryMotionAttachmentDefinition definitions[2]{};
    definitions[0].direction = { { 0.034378800036357141, -0.99937908406922105, 0.007716503937976538 } };
    definitions[0].offset = { { -0.012386735133663174, -0.024457055996883095, -0.0090023880996501198 } };
    definitions[0].cosineLimit = 0.50000000000000011;
    definitions[1].direction = { { -0.035502921390646057, -0.99933409932954853, 0.0084202428664333553 } };
    definitions[1].offset = { { 0.05527310978658484, -0.026374712192124922, -0.0033837043217999074 } };
    definitions[1].cosineLimit = 0.50000000000000011;
    const FModelSecondaryMotionCollisionShape bodies[8] = {
        { { 0.016984444111585617f, 0.92489689588546753f, 0.73721092939376831f }, { 0.016984444111585617f, 0.92489689588546753f, 0.73721092939376831f }, 0.07992330938577652f }, { { 0.015060985460877419f, 0.97888100147247314f, 0.74080580472946167f }, { 0.011476121842861176f, 1.0887347459793091f, 0.74520033597946167f }, 0.08992331475019455f }, { { 0.011181753128767014f, 1.0977553129196167f, 0.74556118249893188f }, { 0.0094436677172780037f, 1.2185074090957642f, 0.69344675540924072f }, 0.08992331475019455f }, { { 0.012458458542823792f, 1.3578834533691406f, 0.65250611305236816f }, { 0.012458458542823792f, 1.3578834533691406f, 0.65250611305236816f }, 0.099923312664031982f }, { { 0.080892622470855713f, 0.87460625171661377f, 0.7252577543258667f }, { 0.10447004437446594f, 0.71211975812911987f, 0.85618621110916138f }, 0.059923317283391953f }, { { -0.044866882264614105f, 0.8776935338973999f, 0.72956043481826782f }, { -0.075469620525836945f, 0.76533973217010498f, 0.90431767702102661f }, 0.059923309832811356f }, { { 0.1094970628619194f, 0.66322135925292969f, 0.85772174596786499f }, { 0.12134783715009689f, 0.44376027584075928f, 0.6685224175453186f }, 0.049923311918973923f }, { { -0.083556331694126129f, 0.72032374143600464f, 0.91959565877914429f }, { -0.11403576284646988f, 0.46026387810707092f, 0.79493677616119385f }, 0.049923308193683624f },
    };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(bodies, 8, prepared, error))
    {
        fprintf(stderr, "TestRecordedNativeSkinningContact: %s\n", error.CStr());
        return Fail(error.CStr());
    }
    gk::FVector3d point0 = { { -0.03379565104842186, 0.88632726669311523, 0.88971847295761108 } };
    gk::FVector3d point1 = { { -0.049111388623714447, 0.86100345849990845, 0.91431325674057007 } };
    gk::FVector3d point2 = { { -0.11094807088375092, 0.89307188987731934, 0.84962004423141479 } };
    gk::FVector3d point3 = { { -0.11811470240354538, 0.8996385931968689, 0.81896054744720459 } };
    gk::FVector3d* points[4] = { &point0, &point1, &point2, &point3 };
    const gk::FVector3d firstPivot = point0;
    const gk::FVector3d secondPivot = point2;
    // 独立方向samplingで得た姿勢を厳密照会し、この固定pivot入力が可解なことを先に確認する。
    const gk::FVector3d feasibleChildren[2] = { { { -0.016148124545036387, 0.8678820485038302, 0.9185127293722858 } }, { { -0.0877192602073861, 0.8723428139279384, 0.8576956066102767 } } };
    FSecondaryMotionAttachmentEvaluation feasibleEndpoints[2]{};
    if (!EvaluateSecondaryMotionAttachment(definitions[0], point0, feasibleChildren[0], feasibleEndpoints[0], error) || !EvaluateSecondaryMotionAttachment(definitions[1], point2, feasibleChildren[1], feasibleEndpoints[1], error) || !CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), feasibleEndpoints[0].position, feasibleEndpoints[1].position, error, 0.5))
    {
        return Fail("recorded fixture has no valid independently selected feasible pose");
    }
    if (!ProjectSecondaryMotionAttachmentContacts(prepared.Data(), prepared.Count(), definitions[0], definitions[1], points, error))
    {
        fprintf(stderr, "TestRecordedNativeSkinningContact: %s\n", error.CStr());
        return Fail(error.CStr());
    }
    FSecondaryMotionAttachmentEvaluation endpoints[2]{};
    if (!EvaluateSecondaryMotionAttachment(definitions[0], point0, point1, endpoints[0], error) || !EvaluateSecondaryMotionAttachment(definitions[1], point2, point3, endpoints[1], error) || !CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), endpoints[0].position, endpoints[1].position, error, 0.5))
    {
        fprintf(stderr, "TestRecordedNativeSkinningContact: %s\n", error.CStr());
        return Fail(error.CStr());
    }
    if (!NearVector(point0, firstPivot, 0.0) || !NearVector(point2, secondPivot, 0.0))
    {
        return Fail("recorded attachment projection changed its fixed pivots");
    }
    return true;
}

}

int main()
{
    return TestAttachmentEvaluationAndJacobian() && TestEvaluationFailureAtomicityAndContactQuery() && TestSharedBoneProjectionAndAtomicFailure() && TestEmptyShapeProjectionIsNoOp() && TestStationaryContactEscapesByRotation() && TestRecordedNativeLoopContact() && TestRecordedNativeFirstClipContact() && TestRecordedNativeSkinningContact() ? 0 : 1;
}
