// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FSecondaryMotionContactLinearSystem.h"

#include <math.h>
#include <stdint.h>

namespace gk::model::animation
{
namespace
{

// 大きな接触問題は呼出し側の既存探索へ戻せる範囲に制限する。
const uint32_t kMaximumDirections = 256u;
const uint32_t kMaximumRows = 4096u;
const uint32_t kMaximumEntries = 65536u;

/**
 * ベクトルの3成分が有限値であることを確認する。
 */
bool IsFinite(const gk::FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]);
}

/**
 * ベクトルの内積を計算する。
 */
double Dot(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return first.value[0] * second.value[0] + first.value[1] * second.value[1] + first.value[2] * second.value[2];
}

/**
 * 方向が有限な単位ベクトルであることを確認する。
 */
bool IsUnitDirection(const gk::FVector3d& direction)
{
    if (!IsFinite(direction))
    {
        return false;
    }
    const double lengthSquared = Dot(direction, direction);
    return isfinite(lengthSquared) && fabs(lengthSquared - 1.0) <= 1.0e-8;
}

/**
 * 失敗理由を設定する。
 */
bool SetError(gk::String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

/**
 * 疎な接触行を掛け、Jの転置と減衰項を加えた値を計算する。
 */
bool FSecondaryMotionContactLinearSystem::MultiplyNormal(const gk::Array<gk::FVector3d>& input, double damping, gk::Array<gk::FVector3d>& output) const
{
    for (uint32_t index = 0; index < output.Count(); ++index)
    {
        output.At(index) = {};
    }
    for (uint32_t rowIndex = 0; rowIndex < rows_.Count(); ++rowIndex)
    {
        const FRow& row = rows_.At(rowIndex);
        double rowValue = 0.0;
        for (uint32_t localEntry = 0; localEntry < row.entryCount; ++localEntry)
        {
            const FEntry& entry = entries_.At(row.firstEntry + localEntry);
            rowValue += Dot(entry.derivative, input.At(entry.segmentIndex));
        }
        if (!isfinite(rowValue))
        {
            return false;
        }
        for (uint32_t localEntry = 0; localEntry < row.entryCount; ++localEntry)
        {
            const FEntry& entry = entries_.At(row.firstEntry + localEntry);
            gk::FVector3d& value = output.At(entry.segmentIndex);
            value.value[0] += entry.derivative.value[0] * rowValue;
            value.value[1] += entry.derivative.value[1] * rowValue;
            value.value[2] += entry.derivative.value[2] * rowValue;
        }
    }
    for (uint32_t index = 0; index < output.Count(); ++index)
    {
        output.At(index).value[0] += damping * input.At(index).value[0];
        output.At(index).value[1] += damping * input.At(index).value[1];
        output.At(index).value[2] += damping * input.At(index).value[2];
        if (!IsFinite(output.At(index)))
        {
            return false;
        }
    }
    return true;
}

FSecondaryMotionContactLinearSystem::FSecondaryMotionContactLinearSystem() : directions_(nullptr), directionCount_(0), maximumDiagonal_(0.0)
{
}

bool FSecondaryMotionContactLinearSystem::Reset(const gk::FVector3d* directions, uint32_t directionCount, gk::String& error)
{
    error.Clear();
    if (directionCount > 0 && !directions)
    {
        return SetError(error, "Secondary motion contact linear-system directions are invalid");
    }
    if (directionCount > kMaximumDirections)
    {
        return false;
    }
    for (uint32_t index = 0; index < directionCount; ++index)
    {
        if (!IsUnitDirection(directions[index]))
        {
            return SetError(error, "Secondary motion contact linear-system direction is not a finite unit vector");
        }
    }
    directions_ = directions;
    directionCount_ = directionCount;
    entries_.Clear();
    rows_.Clear();
    maximumDiagonal_ = 0.0;
    return true;
}

bool FSecondaryMotionContactLinearSystem::BeginRow(double penetration, gk::String& error)
{
    error.Clear();
    if (!isfinite(penetration) || penetration < 0.0)
    {
        return SetError(error, "Secondary motion contact linear-system row is invalid");
    }
    if (rows_.Count() >= kMaximumRows)
    {
        return false;
    }
    const FRow row = { entries_.Count(), 0, penetration };
    if (!rows_.Append(row))
    {
        return SetError(error, "Secondary motion contact linear-system row allocation failed");
    }
    return true;
}

bool FSecondaryMotionContactLinearSystem::AddCoefficient(uint32_t segmentIndex, const gk::FVector3d& derivative, gk::String& error)
{
    error.Clear();
    if (rows_.Count() == 0 || segmentIndex >= directionCount_ || !IsFinite(derivative) || !IsUnitDirection(directions_[segmentIndex]))
    {
        return SetError(error, "Secondary motion contact linear-system coefficient is invalid");
    }
    const double radialAmount = Dot(derivative, directions_[segmentIndex]);
    if (!isfinite(radialAmount))
    {
        return SetError(error, "Secondary motion contact linear-system coefficient is not finite");
    }
    FEntry entry{};
    entry.segmentIndex = segmentIndex;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        entry.derivative.value[axis] = derivative.value[axis] - directions_[segmentIndex].value[axis] * radialAmount;
    }
    if (!IsFinite(entry.derivative))
    {
        return SetError(error, "Secondary motion contact linear-system projected coefficient overflowed");
    }
    FRow& row = rows_.At(rows_.Count() - 1u);
    for (uint32_t localEntry = 0; localEntry < row.entryCount; ++localEntry)
    {
        FEntry& existing = entries_.At(row.firstEntry + localEntry);
        if (existing.segmentIndex == segmentIndex)
        {
            gk::FVector3d combined{};
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                combined.value[axis] = existing.derivative.value[axis] + entry.derivative.value[axis];
            }
            if (!IsFinite(combined))
            {
                return SetError(error, "Secondary motion contact linear-system merged coefficient overflowed");
            }
            existing.derivative = combined;
            return true;
        }
    }
    if (entries_.Count() >= kMaximumEntries)
    {
        return false;
    }
    if (!entries_.Append(entry))
    {
        return SetError(error, "Secondary motion contact linear-system coefficient allocation failed");
    }
    ++row.entryCount;
    return true;
}

bool FSecondaryMotionContactLinearSystem::Solve(double relativeDamping, gk::Array<gk::FVector3d>& result, gk::String& error)
{
    error.Clear();
    if (!isfinite(relativeDamping) || relativeDamping < 0.0)
    {
        return SetError(error, "Secondary motion contact linear-system damping is invalid");
    }
    for (uint32_t index = 0; index < directionCount_; ++index)
    {
        if (!IsUnitDirection(directions_[index]))
        {
            return SetError(error, "Secondary motion contact linear-system borrowed direction changed");
        }
    }
    for (uint32_t rowIndex = 0; rowIndex < rows_.Count(); ++rowIndex)
    {
        const FRow& row = rows_.At(rowIndex);
        if (row.firstEntry > entries_.Count() || row.entryCount > entries_.Count() - row.firstEntry || !isfinite(row.penetration) || row.penetration < 0.0)
        {
            return SetError(error, "Secondary motion contact linear-system row storage is invalid");
        }
    }
    gk::Array<gk::FVector3d> candidate;
    gk::Array<gk::FVector3d> rightHandSide;
    gk::Array<gk::FVector3d> residual;
    gk::Array<gk::FVector3d> preconditioned;
    gk::Array<gk::FVector3d> searchDirection;
    gk::Array<gk::FVector3d> product;
    gk::Array<double> diagonal;
    if (!candidate.Reserve(directionCount_) || !rightHandSide.Reserve(directionCount_) || !residual.Reserve(directionCount_) || !preconditioned.Reserve(directionCount_) || !searchDirection.Reserve(directionCount_) || !product.Reserve(directionCount_) || !diagonal.Reserve(directionCount_))
    {
        return SetError(error, "Secondary motion contact linear-system workspace allocation failed");
    }
    const gk::FVector3d zero{};
    for (uint32_t index = 0; index < directionCount_; ++index)
    {
        if (!candidate.Append(zero) || !rightHandSide.Append(zero) || !residual.Append(zero) || !preconditioned.Append(zero) || !searchDirection.Append(zero) || !product.Append(zero) || !diagonal.Append(0.0))
        {
            return SetError(error, "Secondary motion contact linear-system workspace allocation failed");
        }
    }
    double maximumDiagonal = 0.0;
    for (uint32_t entryIndex = 0; entryIndex < entries_.Count(); ++entryIndex)
    {
        const FEntry& entry = entries_.At(entryIndex);
        const double squaredLength = Dot(entry.derivative, entry.derivative);
        if (entry.segmentIndex >= directionCount_ || !IsFinite(entry.derivative) || !isfinite(squaredLength))
        {
            return SetError(error, "Secondary motion contact linear-system coefficient storage is invalid");
        }
        diagonal.At(entry.segmentIndex) += squaredLength;
        if (!isfinite(diagonal.At(entry.segmentIndex)))
        {
            return SetError(error, "Secondary motion contact linear-system diagonal overflowed");
        }
    }
    for (uint32_t index = 0; index < directionCount_; ++index)
    {
        if (diagonal.At(index) > maximumDiagonal)
        {
            maximumDiagonal = diagonal.At(index);
        }
    }
    maximumDiagonal_ = maximumDiagonal;
    if (maximumDiagonal == 0.0)
    {
        result.MoveFrom(candidate);
        return true;
    }
    const double damping = fmax(maximumDiagonal * relativeDamping, maximumDiagonal * 1.0e-12);
    if (!(damping > 0.0) || !isfinite(damping))
    {
        return SetError(error, "Secondary motion contact linear-system damping overflowed");
    }
    for (uint32_t rowIndex = 0; rowIndex < rows_.Count(); ++rowIndex)
    {
        const FRow& row = rows_.At(rowIndex);
        for (uint32_t localEntry = 0; localEntry < row.entryCount; ++localEntry)
        {
            const FEntry& entry = entries_.At(row.firstEntry + localEntry);
            gk::FVector3d& value = rightHandSide.At(entry.segmentIndex);
            value.value[0] += entry.derivative.value[0] * row.penetration;
            value.value[1] += entry.derivative.value[1] * row.penetration;
            value.value[2] += entry.derivative.value[2] * row.penetration;
        }
    }
    double rightHandSideSquared = 0.0;
    for (uint32_t index = 0; index < directionCount_; ++index)
    {
        if (!IsFinite(rightHandSide.At(index)))
        {
            return SetError(error, "Secondary motion contact linear-system right-hand side overflowed");
        }
        rightHandSideSquared += Dot(rightHandSide.At(index), rightHandSide.At(index));
    }
    if (!isfinite(rightHandSideSquared))
    {
        return SetError(error, "Secondary motion contact linear-system residual overflowed");
    }
    if (rightHandSideSquared == 0.0)
    {
        result.MoveFrom(candidate);
        return true;
    }
    const double targetResidual = sqrt(rightHandSideSquared) * 1.0e-7;
    for (uint32_t index = 0; index < directionCount_; ++index)
    {
        residual.At(index) = rightHandSide.At(index);
        const double preconditioner = diagonal.At(index) + damping;
        preconditioned.At(index) = { { residual.At(index).value[0] / preconditioner, residual.At(index).value[1] / preconditioner, residual.At(index).value[2] / preconditioner } };
        searchDirection.At(index) = preconditioned.At(index);
    }
    double residualPreconditioned = 0.0;
    for (uint32_t index = 0; index < directionCount_; ++index)
    {
        residualPreconditioned += Dot(residual.At(index), preconditioned.At(index));
    }
    if (!(residualPreconditioned > 0.0) || !isfinite(residualPreconditioned))
    {
        return SetError(error, "Secondary motion contact linear-system initial residual is invalid");
    }
    const uint32_t iterationLimit = directionCount_ >= 22u ? 64u : directionCount_ * 3u;
    bool converged = false;
    for (uint32_t iteration = 0; iteration < iterationLimit; ++iteration)
    {
        if (!MultiplyNormal(searchDirection, damping, product))
        {
            return SetError(error, "Secondary motion contact linear-system product overflowed");
        }
        double curvature = 0.0;
        for (uint32_t index = 0; index < directionCount_; ++index)
        {
            curvature += Dot(searchDirection.At(index), product.At(index));
        }
        if (!(curvature > 0.0) || !isfinite(curvature))
        {
            return SetError(error, "Secondary motion contact linear-system curvature is invalid");
        }
        const double step = residualPreconditioned / curvature;
        if (!isfinite(step))
        {
            return SetError(error, "Secondary motion contact linear-system step is invalid");
        }
        double residualSquared = 0.0;
        for (uint32_t index = 0; index < directionCount_; ++index)
        {
            candidate.At(index).value[0] += step * searchDirection.At(index).value[0];
            candidate.At(index).value[1] += step * searchDirection.At(index).value[1];
            candidate.At(index).value[2] += step * searchDirection.At(index).value[2];
            residual.At(index).value[0] -= step * product.At(index).value[0];
            residual.At(index).value[1] -= step * product.At(index).value[1];
            residual.At(index).value[2] -= step * product.At(index).value[2];
            if (!IsFinite(candidate.At(index)) || !IsFinite(residual.At(index)))
            {
                return SetError(error, "Secondary motion contact linear-system iteration overflowed");
            }
            residualSquared += Dot(residual.At(index), residual.At(index));
        }
        if (!isfinite(residualSquared))
        {
            return SetError(error, "Secondary motion contact linear-system residual overflowed");
        }
        if (sqrt(residualSquared) <= targetResidual)
        {
            converged = true;
            break;
        }
        for (uint32_t index = 0; index < directionCount_; ++index)
        {
            const double preconditioner = diagonal.At(index) + damping;
            preconditioned.At(index) = { { residual.At(index).value[0] / preconditioner, residual.At(index).value[1] / preconditioner, residual.At(index).value[2] / preconditioner } };
        }
        double nextResidualPreconditioned = 0.0;
        for (uint32_t index = 0; index < directionCount_; ++index)
        {
            nextResidualPreconditioned += Dot(residual.At(index), preconditioned.At(index));
        }
        if (!(nextResidualPreconditioned > 0.0) || !isfinite(nextResidualPreconditioned))
        {
            return SetError(error, "Secondary motion contact linear-system residual update is invalid");
        }
        const double conjugateScale = nextResidualPreconditioned / residualPreconditioned;
        if (!isfinite(conjugateScale))
        {
            return SetError(error, "Secondary motion contact linear-system conjugate direction is invalid");
        }
        for (uint32_t index = 0; index < directionCount_; ++index)
        {
            searchDirection.At(index).value[0] = preconditioned.At(index).value[0] + conjugateScale * searchDirection.At(index).value[0];
            searchDirection.At(index).value[1] = preconditioned.At(index).value[1] + conjugateScale * searchDirection.At(index).value[1];
            searchDirection.At(index).value[2] = preconditioned.At(index).value[2] + conjugateScale * searchDirection.At(index).value[2];
            if (!IsFinite(searchDirection.At(index)))
            {
                return SetError(error, "Secondary motion contact linear-system search direction overflowed");
            }
        }
        residualPreconditioned = nextResidualPreconditioned;
    }
    if (!converged)
    {
        error.Clear();
        return false;
    }
    result.MoveFrom(candidate);
    return true;
}

double FSecondaryMotionContactLinearSystem::GetMaximumDiagonal() const
{
    return maximumDiagonal_;
}

uint32_t FSecondaryMotionContactLinearSystem::GetRowCount() const
{
    return rows_.Count();
}

}
