// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionContactMinimax.h"

#include "model/animation/FSecondaryMotionContactInequalitySystem.h"
#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{

// 骨方向の接線二成分と、共通の侵入上限一成分に使う容量。
const uint32_t kMaximumSelectedSegments = 32u;
// 物理的な許容幅は変えず、QP内の距離単位だけmmに揃える。
const double kDistanceScale = 1000.0;
// 方向座標に対する中央差分の幅。
const double kDerivativeStep = 1.0e-6;

/**
 * 有限な単位方向へ正規化する。
 */
bool Normalize(gk::FVector3d& value)
{
    const double squared = value.value[0] * value.value[0] + value.value[1] * value.value[1] + value.value[2] * value.value[2];
    if (!(squared > 0.0) || !isfinite(squared))
    {
        return false;
    }
    const double inverse = 1.0 / sqrt(squared);
    for (uint32_t axis = 0; axis < 3u; ++axis)
    {
        value.value[axis] *= inverse;
    }
    return true;
}

/**
 * 二方向の内積を求める。
 */
double Dot(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return first.value[0] * second.value[0] + first.value[1] * second.value[1] + first.value[2] * second.value[2];
}

/**
 * 二方向に直交する方向を求める。
 */
gk::FVector3d Cross(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return { { first.value[1] * second.value[2] - first.value[2] * second.value[1], first.value[2] * second.value[0] - first.value[0] * second.value[2], first.value[0] * second.value[1] - first.value[1] * second.value[0] } };
}

/**
 * 確保できなかった作業配列を呼出し側へ知らせる。
 */
bool AllocationFailure(gk::String& error)
{
    error.Assign("Secondary motion minimax workspace allocation failed");
    return false;
}

/**
 * 指定個数の0を持つ作業配列を用意する。
 */
bool FillZeros(gk::Array<double>& values, uint32_t count, gk::String& error)
{
    values.Clear();
    if (!values.Reserve(count))
    {
        return AllocationFailure(error);
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        if (!values.Append(0.0))
        {
            return AllocationFailure(error);
        }
    }
    return true;
}

/**
 * 一つの接触へ影響する、親から対象節までの方向を選ぶ。
 */
void MarkAncestors(const FSecondaryMotionContactChain& chain, uint32_t segment, gk::Array<uint8_t>& selected)
{
    for (uint32_t ancestor = 0; ancestor <= segment; ++ancestor)
    {
        selected.At(chain.firstSegment + ancestor) = 1u;
    }
}

/**
 * 選択方向のいずれかが対象節までの位置へ影響するか確認する。
 */
bool HasSelectedAncestor(const FSecondaryMotionContactChain& chain, uint32_t segment, const gk::Array<uint8_t>& selected)
{
    for (uint32_t ancestor = 0; ancestor <= segment; ++ancestor)
    {
        if (selected.At(chain.firstSegment + ancestor))
        {
            return true;
        }
    }
    return false;
}

/**
 * 近接行の祖先を選び、その方向が影響する全行を同じ順番で集める。
 */
bool SelectRows(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionWeightedContactAnchor* anchors, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<double>& values, const gk::Array<double>& tolerances, gk::Array<uint8_t>& selected, gk::Array<uint32_t>& rows, gk::String& error)
{
    // 一度目は近接する行から方向を選び、二度目は選んだ方向に関係する全行を残す。
    for (uint32_t pass = 0; pass < 2u; ++pass)
    {
        uint32_t row = 0u;
        for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
        {
            const FSecondaryMotionWeightedContactPoint& contact = contacts[contactIndex];
            for (uint32_t shape = 0; shape < contact.shapeCount; ++shape, ++row)
            {
                if (row >= values.Count())
                {
                    error.Assign("Secondary motion minimax signed row order is invalid");
                    return false;
                }
                bool relevant = false;
                for (uint32_t local = 0; local < contact.anchorCount; ++local)
                {
                    const FSecondaryMotionWeightedContactAnchor& anchor = anchors[contact.firstAnchor + local];
                    if (anchor.chainIndex == UINT32_MAX || !(anchor.weight > 0.0))
                    {
                        continue;
                    }
                    const FSecondaryMotionContactChain& chain = chains[anchor.chainIndex];
                    if (pass == 0u && values.At(row) >= -2.0 * tolerances.At(row))
                    {
                        MarkAncestors(chain, anchor.segmentIndex, selected);
                    }
                    relevant = relevant || HasSelectedAncestor(chain, anchor.segmentIndex, selected);
                }
                if (pass == 1u && relevant && !rows.Append(row))
                {
                    return AllocationFailure(error);
                }
            }
        }
        for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
        {
            const FSecondaryMotionContactChain& chain = chains[chainIndex];
            for (uint32_t shape = 0; shape < chain.shapeCount; ++shape)
            {
                for (uint32_t segment = 0; segment < chain.segmentCount; ++segment)
                {
                    for (uint32_t kind = 0; kind < 2u; ++kind, ++row)
                    {
                        if (row >= values.Count())
                        {
                            error.Assign("Secondary motion minimax signed row order is invalid");
                            return false;
                        }
                        if (pass == 0u && values.At(row) >= -2.0 * tolerances.At(row))
                        {
                            MarkAncestors(chain, segment, selected);
                        }
                        if (pass == 1u && HasSelectedAncestor(chain, segment, selected) && !rows.Append(row))
                        {
                            return AllocationFailure(error);
                        }
                    }
                }
            }
            for (uint32_t contactIndex = 0; contactIndex < chain.attachmentCount; ++contactIndex)
            {
                const FSecondaryMotionChainAttachmentContact& contact = chain.attachments[contactIndex];
                for (uint32_t shape = 0; shape < contact.shapeCount; ++shape, ++row)
                {
                    if (row >= values.Count())
                    {
                        error.Assign("Secondary motion minimax signed row order is invalid");
                        return false;
                    }
                    if (pass == 0u && values.At(row) >= -2.0 * tolerances.At(row))
                    {
                        MarkAncestors(chain, contact.segmentIndex, selected);
                    }
                    if (pass == 1u && HasSelectedAncestor(chain, contact.segmentIndex, selected) && !rows.Append(row))
                    {
                        return AllocationFailure(error);
                    }
                }
            }
        }
        if (row != values.Count())
        {
            error.Assign("Secondary motion minimax signed row count is invalid");
            return false;
        }
    }
    return true;
}

/**
 * 固定した基準姿勢の接線座標から位置と全制約を再評価する。
 */
struct FEvaluationContext
{
    // 借用する鎖と、その要素数。
    const FSecondaryMotionContactChain* chains;
    uint32_t chainCount;
    // 各節の基準方向と保持する骨長。
    const FSecondaryMotionReferenceSegment* references;
    // 重み付き接触の寄与点と、その要素数。
    const FSecondaryMotionWeightedContactAnchor* anchors;
    uint32_t anchorCount;
    // 身体形状を借用する接触点と、その要素数。
    const FSecondaryMotionWeightedContactPoint* contacts;
    uint32_t contactCount;
    // 各鎖で固定する始点。
    const gk::Array<gk::FVector3d>& roots;
    // 接線座標の原点とする全方向。
    const gk::Array<gk::FVector3d>& base;
    // 座標二成分を割り当てる全体節番号。
    const gk::Array<uint32_t>& segments;
    // 選択方向が影響する、元評価配列内の行番号。
    const gk::Array<uint32_t>& rows;
    // 基準方向に直交する二本の単位方向。
    const gk::Array<gk::FVector3d>& firstTangents;
    const gk::Array<gk::FVector3d>& secondTangents;

    /**
     * 位置配列と、c>=0の形式に揃えた接触、角度、座標範囲を作る。
     */
    bool Evaluate(const gk::Array<double>& coordinates, gk::Array<gk::FVector3d>& positions, gk::Array<double>& constraints, gk::String& error) const
    {
        gk::Array<gk::FVector3d> directions;
        if (!directions.AppendRange(base.Data(), base.Count()))
        {
            return AllocationFailure(error);
        }
        for (uint32_t local = 0; local < segments.Count(); ++local)
        {
            gk::FVector3d& direction = directions.At(segments.At(local));
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                direction.value[axis] += coordinates.At(local * 2u) * firstTangents.At(local).value[axis] + coordinates.At(local * 2u + 1u) * secondTangents.At(local).value[axis];
            }
            if (!Normalize(direction))
            {
                error.Assign("Secondary motion minimax candidate direction is invalid");
                return false;
            }
        }
        positions.Clear();
        for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
        {
            const FSecondaryMotionContactChain& chain = chains[chainIndex];
            if (positions.Count() != chain.firstPoint || !positions.Append(roots.At(chainIndex)))
            {
                return AllocationFailure(error);
            }
            gk::FVector3d point = roots.At(chainIndex);
            for (uint32_t segment = 0; segment < chain.segmentCount; ++segment)
            {
                const uint32_t index = chain.firstSegment + segment;
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    point.value[axis] += references[index].length * directions.At(index).value[axis];
                }
                if (!positions.Append(point))
                {
                    return AllocationFailure(error);
                }
            }
        }
        gk::Array<double> signedValues;
        gk::Array<double> tolerances;
        if (!EvaluateSecondaryMotionContactSignedRows(chains, chainCount, anchors, anchorCount, contacts, contactCount, positions.Data(), positions.Count(), signedValues, tolerances, error))
        {
            return false;
        }
        constraints.Clear();
        const double slack = coordinates.At(coordinates.Count() - 1u);
        for (uint32_t row = 0; row < rows.Count(); ++row)
        {
            const uint32_t source = rows.At(row);
            if (source >= signedValues.Count() || !constraints.Append(slack - kDistanceScale * (signedValues.At(source) + 0.125 * tolerances.At(source))))
            {
                return AllocationFailure(error);
            }
        }
        for (uint32_t local = 0; local < segments.Count(); ++local)
        {
            const uint32_t segment = segments.At(local);
            double cosineLimit = -1.0;
            for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
            {
                const FSecondaryMotionContactChain& chain = chains[chainIndex];
                if (segment >= chain.firstSegment && segment - chain.firstSegment < chain.segmentCount)
                {
                    cosineLimit = chain.cosineLimit;
                    break;
                }
            }
            gk::FVector3d reference = references[segment].direction;
            if (!Normalize(reference) || !constraints.Append(Dot(reference, directions.At(segment)) - cosineLimit))
            {
                return AllocationFailure(error);
            }
        }
        const double bound = tan(0.35);
        for (uint32_t variable = 0; variable + 1u < coordinates.Count(); ++variable)
        {
            if (!constraints.Append(bound - coordinates.At(variable)) || !constraints.Append(bound + coordinates.At(variable)))
            {
                return AllocationFailure(error);
            }
        }
        if (!constraints.Append(slack))
        {
            return AllocationFailure(error);
        }
        for (uint32_t row = 0; row < constraints.Count(); ++row)
        {
            if (!isfinite(constraints.At(row)))
            {
                error.Assign("Secondary motion minimax constraint is outside the numeric range");
                return false;
            }
        }
        return true;
    }

    /**
     * 全制約の座標微分を、固定行順の中央差分で求める。
     */
    bool Differentiate(const gk::Array<double>& coordinates, uint32_t rowCount, gk::Array<double>& jacobian, gk::String& error) const
    {
        const uint32_t variables = coordinates.Count();
        if (!FillZeros(jacobian, rowCount * variables, error))
        {
            return false;
        }
        gk::Array<double> shifted;
        if (!shifted.AppendRange(coordinates.Data(), variables))
        {
            return AllocationFailure(error);
        }
        gk::Array<double> positive;
        gk::Array<double> negative;
        gk::Array<gk::FVector3d> positions;
        for (uint32_t column = 0; column + 1u < variables; ++column)
        {
            shifted.At(column) = coordinates.At(column) + kDerivativeStep;
            if (!Evaluate(shifted, positions, positive, error))
            {
                return false;
            }
            shifted.At(column) = coordinates.At(column) - kDerivativeStep;
            if (!Evaluate(shifted, positions, negative, error))
            {
                return false;
            }
            shifted.At(column) = coordinates.At(column);
            if (positive.Count() != rowCount || negative.Count() != rowCount)
            {
                error.Assign("Secondary motion minimax derivative row count changed");
                return false;
            }
            for (uint32_t row = 0; row < rowCount; ++row)
            {
                const double value = (positive.At(row) - negative.At(row)) / (2.0 * kDerivativeStep);
                if (!isfinite(value))
                {
                    error.Assign("Secondary motion minimax derivative is outside the numeric range");
                    return false;
                }
                jacobian.At(row * variables + column) = value;
            }
        }
        for (uint32_t row = 0; row < rows.Count(); ++row)
        {
            jacobian.At(row * variables + variables - 1u) = 1.0;
        }
        jacobian.At((rowCount - 1u) * variables + variables - 1u) = 1.0;
        return true;
    }
};

/**
 * 共通侵入上限に制約違反の重み付き和を加えた、線形探索用の値を求める。
 */
double Merit(const gk::Array<double>& coordinates, const gk::Array<double>& constraints, const gk::Array<double>& penalties)
{
    double value = coordinates.At(coordinates.Count() - 1u);
    for (uint32_t row = 0; row < constraints.Count(); ++row)
    {
        value += penalties.At(row) * fmax(0.0, -constraints.At(row));
    }
    return value;
}

}

bool RefineSecondaryMotionContactMinimax(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, uint32_t referenceCount, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& roots, const gk::Array<gk::FVector3d>& baseDirections, gk::Array<gk::FVector3d>& candidatePositions, bool& foundClear, gk::String& error, uint32_t maximumIterations)
{
    error.Clear();
    foundClear = false;
    if (chainCount > 1024u || referenceCount > 16384u || anchorCount > 16384u || contactCount > 1024u || (chainCount > 0u && !chains) || (referenceCount > 0u && !references) || (anchorCount > 0u && !anchors) || (contactCount > 0u && !contacts) || roots.Count() != chainCount || baseDirections.Count() != referenceCount || maximumIterations == 0u || maximumIterations > 32u)
    {
        error.Assign("Secondary motion minimax input is invalid");
        return false;
    }
    if (chainCount == 0u || referenceCount == 0u)
    {
        return true;
    }
    uint32_t nextPoint = 0u;
    uint32_t nextSegment = 0u;
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        if (chain.firstPoint != nextPoint || chain.firstSegment != nextSegment || chain.segmentCount > referenceCount - nextSegment || chain.segmentCount == UINT32_MAX || nextPoint > UINT32_MAX - chain.segmentCount - 1u || !isfinite(chain.cosineLimit) || chain.cosineLimit < -1.0 || chain.cosineLimit > 1.0)
        {
            error.Assign("Secondary motion minimax chain ranges are invalid");
            return false;
        }
        nextPoint += chain.segmentCount + 1u;
        nextSegment += chain.segmentCount;
    }
    if (nextSegment != referenceCount)
    {
        error.Assign("Secondary motion minimax reference count is invalid");
        return false;
    }
    for (uint32_t index = 0; index < referenceCount; ++index)
    {
        const double squared = Dot(baseDirections.At(index), baseDirections.At(index));
        const double referenceSquared = Dot(references[index].direction, references[index].direction);
        if (!isfinite(squared) || fabs(squared - 1.0) > 1.0e-8 || !(referenceSquared > 0.0) || !isfinite(referenceSquared) || !(references[index].length > 0.0) || !isfinite(references[index].length))
        {
            error.Assign("Secondary motion minimax base direction or length is invalid");
            return false;
        }
    }
    gk::Array<uint8_t> selected;
    for (uint32_t index = 0; index < referenceCount; ++index)
    {
        if (!selected.Append(0u))
        {
            return AllocationFailure(error);
        }
    }
    gk::Array<uint32_t> segments;
    gk::Array<uint32_t> rows;
    gk::Array<gk::FVector3d> firstTangents;
    gk::Array<gk::FVector3d> secondTangents;
    const FEvaluationContext context{ chains, chainCount, references, anchors, anchorCount, contacts, contactCount, roots, baseDirections, segments, rows, firstTangents, secondTangents };
    gk::Array<double> coordinates;
    gk::Array<double> constraints;
    gk::Array<gk::FVector3d> positions;
    if (!coordinates.Append(0.0))
    {
        return AllocationFailure(error);
    }
    if (!context.Evaluate(coordinates, positions, constraints, error))
    {
        return error.Empty();
    }
    gk::Array<double> signedValues;
    gk::Array<double> tolerances;
    if (!EvaluateSecondaryMotionContactSignedRows(chains, chainCount, anchors, anchorCount, contacts, contactCount, positions.Data(), positions.Count(), signedValues, tolerances, error))
    {
        return error.Empty();
    }
    if (!SelectRows(chains, chainCount, anchors, contacts, contactCount, signedValues, tolerances, selected, rows, error))
    {
        return false;
    }
    for (uint32_t index = 0; index < referenceCount; ++index)
    {
        if (!selected.At(index))
        {
            continue;
        }
        if (segments.Count() == kMaximumSelectedSegments)
        {
            return true;
        }
        gk::FVector3d axis{};
        uint32_t minimumAxis = 0u;
        for (uint32_t candidate = 1u; candidate < 3u; ++candidate)
        {
            if (fabs(baseDirections.At(index).value[candidate]) < fabs(baseDirections.At(index).value[minimumAxis]))
            {
                minimumAxis = candidate;
            }
        }
        axis.value[minimumAxis] = 1.0;
        gk::FVector3d tangent = Cross(baseDirections.At(index), axis);
        if (!Normalize(tangent) || !segments.Append(index) || !firstTangents.Append(tangent) || !secondTangents.Append(Cross(baseDirections.At(index), tangent)))
        {
            return AllocationFailure(error);
        }
    }
    if (segments.Count() == 0u)
    {
        return true;
    }
    const uint32_t variables = segments.Count() * 2u + 1u;
    const uint32_t constraintCount = rows.Count() + segments.Count() + 2u * (variables - 1u) + 1u;
    if (constraintCount > 4096u)
    {
        return true;
    }
    if (!FillZeros(coordinates, variables, error))
    {
        return false;
    }
    for (uint32_t row = 0; row < signedValues.Count(); ++row)
    {
        coordinates.At(variables - 1u) = fmax(coordinates.At(variables - 1u), kDistanceScale * (signedValues.At(row) + 0.125 * tolerances.At(row)));
    }
    if (!context.Evaluate(coordinates, positions, constraints, error))
    {
        return error.Empty();
    }
    FSecondaryMotionContactInequalitySystem system;
    gk::Array<double> objective;
    gk::Array<double> penalties;
    gk::Array<double> jacobian;
    if (!system.Reset(variables, error) || !FillZeros(objective, variables, error) || !FillZeros(penalties, constraints.Count(), error))
    {
        return false;
    }
    if (!context.Differentiate(coordinates, constraints.Count(), jacobian, error))
    {
        return error.Empty();
    }
    objective.At(variables - 1u) = 1.0;
    if (!system.SetObjective(objective.Data(), variables, error))
    {
        return false;
    }
    for (uint32_t iteration = 0; iteration < maximumIterations; ++iteration)
    {
        bool clear = false;
        if (!CheckSecondaryMotionWeightedStoredCandidate(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions, clear, error))
        {
            return false;
        }
        if (clear)
        {
            candidatePositions.MoveFrom(positions);
            foundClear = true;
            return true;
        }
        system.ClearRows();
        for (uint32_t row = 0; row < constraints.Count(); ++row)
        {
            if (!system.BeginRow(-constraints.At(row), error))
            {
                return error.Empty();
            }
            for (uint32_t variable = 0; variable < variables; ++variable)
            {
                const double value = jacobian.At(row * variables + variable);
                if (value != 0.0 && !system.AddCoefficient(variable, value, error))
                {
                    return error.Empty();
                }
            }
        }
        gk::Array<double> feasibleStart;
        if (!FillZeros(feasibleStart, variables, error))
        {
            return false;
        }
        // 現在の接触違反は共通上限を増やすだけで線形制約の可行開始点へ戻せる。
        for (uint32_t row = 0; row < rows.Count(); ++row)
        {
            feasibleStart.At(variables - 1u) = fmax(feasibleStart.At(variables - 1u), -constraints.At(row));
        }
        feasibleStart.At(variables - 1u) = fmax(feasibleStart.At(variables - 1u), -coordinates.At(variables - 1u));
        gk::Array<double> step;
        gk::Array<double> multipliers;
        if (!system.Solve(feasibleStart.Data(), variables, step, multipliers, error))
        {
            return error.Empty();
        }
        if (step.Count() != variables || multipliers.Count() != constraints.Count())
        {
            error.Assign("Secondary motion minimax QP result dimensions are invalid");
            return false;
        }
        for (uint32_t row = 0; row < constraints.Count(); ++row)
        {
            const double multiplier = fabs(multipliers.At(row));
            penalties.At(row) = fmax(multiplier, 0.5 * (penalties.At(row) + multiplier));
        }
        double prediction = step.At(variables - 1u);
        for (uint32_t row = 0; row < constraints.Count(); ++row)
        {
            prediction -= penalties.At(row) * fmax(0.0, -constraints.At(row));
        }
        const double currentMerit = Merit(coordinates, constraints, penalties);
        if (!isfinite(prediction) || !isfinite(currentMerit))
        {
            error.Assign("Secondary motion minimax line-search merit is outside the numeric range");
            return false;
        }
        bool accepted = false;
        double amount = 1.0;
        gk::Array<double> trialCoordinates;
        gk::Array<double> trialConstraints;
        gk::Array<gk::FVector3d> trialPositions;
        for (uint32_t search = 0; search < 14u; ++search)
        {
            trialCoordinates.Clear();
            for (uint32_t variable = 0; variable < variables; ++variable)
            {
                if (!trialCoordinates.Append(coordinates.At(variable) + amount * step.At(variable)))
                {
                    return AllocationFailure(error);
                }
            }
            if (!context.Evaluate(trialCoordinates, trialPositions, trialConstraints, error))
            {
                if (!error.Empty())
                {
                    return false;
                }
                amount *= 0.5;
                continue;
            }
            // 角度と座標範囲は共通slackで緩めず、全検査も同じ条件で行う。
            bool hardConstraintsClear = true;
            for (uint32_t row = rows.Count(); row < trialConstraints.Count(); ++row)
            {
                hardConstraintsClear = hardConstraintsClear && trialConstraints.At(row) >= -64.0 * DBL_EPSILON;
            }
            const double trialMerit = Merit(trialCoordinates, trialConstraints, penalties);
            if (!isfinite(trialMerit))
            {
                error.Assign("Secondary motion minimax trial merit is outside the numeric range");
                return false;
            }
            if (hardConstraintsClear && trialMerit <= currentMerit + 0.1 * amount * prediction)
            {
                accepted = true;
                break;
            }
            amount *= 0.5;
        }
        if (!accepted)
        {
            return true;
        }
        if (!CheckSecondaryMotionWeightedStoredCandidate(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, trialPositions, clear, error))
        {
            return false;
        }
        if (clear)
        {
            candidatePositions.MoveFrom(trialPositions);
            foundClear = true;
            return true;
        }
        gk::Array<double> nextJacobian;
        gk::Array<double> gradientDifference;
        gk::Array<double> actualStep;
        if (!context.Differentiate(trialCoordinates, trialConstraints.Count(), nextJacobian, error))
        {
            return error.Empty();
        }
        if (!FillZeros(gradientDifference, variables, error) || !FillZeros(actualStep, variables, error))
        {
            return false;
        }
        double maximumActualStep = 0.0;
        for (uint32_t variable = 0; variable < variables; ++variable)
        {
            actualStep.At(variable) = trialCoordinates.At(variable) - coordinates.At(variable);
            maximumActualStep = fmax(maximumActualStep, fabs(actualStep.At(variable)));
            for (uint32_t row = 0; row < constraints.Count(); ++row)
            {
                gradientDifference.At(variable) -= multipliers.At(row) * (nextJacobian.At(row * variables + variable) - jacobian.At(row * variables + variable));
            }
        }
        if (maximumActualStep == 0.0)
        {
            return true;
        }
        if (!system.UpdateHessian(actualStep.Data(), gradientDifference.Data(), variables, error))
        {
            return error.Empty();
        }
        coordinates.MoveFrom(trialCoordinates);
        constraints.MoveFrom(trialConstraints);
        positions.MoveFrom(trialPositions);
        jacobian.MoveFrom(nextJacobian);
    }
    bool clear = false;
    if (!CheckSecondaryMotionWeightedStoredCandidate(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions, clear, error))
    {
        return false;
    }
    if (clear)
    {
        candidatePositions.MoveFrom(positions);
        foundClear = true;
    }
    return true;
}

}
