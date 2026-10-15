// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FSecondaryMotionContactInequalitySystem.h"
#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{
// 呼出し側へ安全に戻せる作業量の上限。
constexpr uint32_t kMaximumVariables = 65u;
constexpr uint32_t kMaximumRows = 4096u;
constexpr uint32_t kMaximumEntries = 65536u;
/**
 * 後続の確保失敗を記録する領域を、作業を始める前に準備する。
 */
bool PrepareError(gk::String& error)
{
    if (!error.Assign("Secondary motion contact inequality allocation failed; diagnostic storage reserved"))
    {
        return false;
    }
    error.Clear();
    return true;
}
/**
 * 失敗理由を既存の文字列領域へ保存する。
 */
bool Fail(gk::String& error, const char* message)
{
    error.Assign(message);
    return false;
}
/**
 * 有限な対称行列のCholesky下三角因子を作り、正定値でなければ失敗する。
 */
bool Factor(const double* matrix, uint32_t count, uint32_t stride, double* lower)
{
    for (uint32_t row = 0u; row < count; ++row)
    {
        for (uint32_t column = 0u; column <= row; ++column)
        {
            double value = matrix[row * stride + column];
            for (uint32_t item = 0u; item < column; ++item)
            {
                value -= lower[row * stride + item] * lower[column * stride + item];
            }
            if (!isfinite(value) || (row == column && !(value > 0.0)))
            {
                return false;
            }
            lower[row * stride + column] = row == column ? sqrt(value) : value / lower[column * stride + column];
            if (!isfinite(lower[row * stride + column]))
            {
                return false;
            }
        }
    }
    return true;
}
/**
 * Cholesky因子から有限な線形方程式の解を求める。右辺と出力の別領域は不要。
 */
bool SolveFactor(const double* lower, uint32_t count, uint32_t stride, const double* rhs, double* output)
{
    for (uint32_t row = 0u; row < count; ++row)
    {
        double value = rhs[row];
        for (uint32_t column = 0u; column < row; ++column)
        {
            value -= lower[row * stride + column] * output[column];
        }
        output[row] = value / lower[row * stride + row];
        if (!isfinite(output[row]))
        {
            return false;
        }
    }
    for (uint32_t offset = 0u; offset < count; ++offset)
    {
        const uint32_t row = count - offset - 1u;
        double value = output[row];
        for (uint32_t column = row + 1u; column < count; ++column)
        {
            value -= lower[column * stride + row] * output[column];
        }
        output[row] = value / lower[row * stride + row];
        if (!isfinite(output[row]))
        {
            return false;
        }
    }
    return true;
}
/**
 * 要素数の小さい倍精度配列へ0を確保する。
 */
bool ZeroArray(uint32_t count, gk::Array<double>& output)
{
    if (!output.Reserve(count))
    {
        return false;
    }
    for (uint32_t index = 0u; index < count; ++index)
    {
        if (!output.Append(0.0))
        {
            return false;
        }
    }
    return true;
}
}

FSecondaryMotionContactInequalitySystem::FSecondaryMotionContactInequalitySystem() : variableCount(0u)
{
}

bool FSecondaryMotionContactInequalitySystem::Reset(uint32_t count, gk::String& error)
{
    error.Clear();
    if (count > kMaximumVariables)
    {
        return false;
    }
    if (!PrepareError(error))
    {
        return false;
    }
    if (count == 0u)
    {
        return Fail(error, "Secondary motion contact inequality variable count is invalid");
    }
    // 新しい目的を完成させてから、以前の系と置き換える。
    gk::Array<double> nextHessian;
    gk::Array<double> nextObjective;
    if (!ZeroArray(count * count, nextHessian) || !ZeroArray(count, nextObjective))
    {
        return Fail(error, "Secondary motion contact inequality allocation failed");
    }
    for (uint32_t index = 0u; index < count; ++index)
    {
        nextHessian.At(index * count + index) = 1.0;
    }
    hessian.MoveFrom(nextHessian);
    objective.MoveFrom(nextObjective);
    variableCount = count;
    ClearRows();
    return true;
}

void FSecondaryMotionContactInequalitySystem::ClearRows()
{
    rows.Clear();
    entries.Clear();
}

bool FSecondaryMotionContactInequalitySystem::SetObjective(const double* gradient, uint32_t count, gk::String& error)
{
    if (!PrepareError(error))
    {
        return false;
    }
    if (!gradient || count != variableCount || count == 0u)
    {
        return Fail(error, "Secondary motion contact inequality objective range is invalid");
    }
    for (uint32_t index = 0u; index < count; ++index)
    {
        if (!isfinite(gradient[index]))
        {
            return Fail(error, "Secondary motion contact inequality objective is not finite");
        }
    }
    gk::Array<double> candidate;
    if (!candidate.AppendRange(gradient, count))
    {
        return Fail(error, "Secondary motion contact inequality allocation failed");
    }
    objective.MoveFrom(candidate);
    return true;
}

bool FSecondaryMotionContactInequalitySystem::BeginRow(double lowerBound, gk::String& error)
{
    error.Clear();
    if (rows.Count() >= kMaximumRows)
    {
        return false;
    }
    if (!PrepareError(error))
    {
        return false;
    }
    if (variableCount == 0u || !isfinite(lowerBound))
    {
        return Fail(error, "Secondary motion contact inequality row is invalid");
    }
    const FRow row = { entries.Count(), 0u, lowerBound };
    if (!rows.Append(row))
    {
        return Fail(error, "Secondary motion contact inequality allocation failed");
    }
    return true;
}

bool FSecondaryMotionContactInequalitySystem::AddCoefficient(uint32_t variableIndex, double coefficient, gk::String& error)
{
    if (!PrepareError(error))
    {
        return false;
    }
    if (rows.Count() == 0u || variableIndex >= variableCount || !isfinite(coefficient))
    {
        return Fail(error, "Secondary motion contact inequality coefficient is invalid");
    }
    auto& row = rows.At(rows.Count() - 1u);
    for (uint32_t offset = 0u; offset < row.entryCount; ++offset)
    {
        auto& entry = entries.At(row.firstEntry + offset);
        if (entry.variable == variableIndex)
        {
            const double sum = entry.value + coefficient;
            if (!isfinite(sum))
            {
                return Fail(error, "Secondary motion contact inequality coefficient overflowed");
            }
            entry.value = sum;
            return true;
        }
    }
    if (coefficient == 0.0)
    {
        return true;
    }
    if (entries.Count() >= kMaximumEntries)
    {
        return false;
    }
    const FEntry entry = { variableIndex, coefficient };
    if (!entries.Append(entry))
    {
        return Fail(error, "Secondary motion contact inequality allocation failed");
    }
    ++row.entryCount;
    return true;
}

bool FSecondaryMotionContactInequalitySystem::Solve(const double* feasibleStart, uint32_t count, gk::Array<double>& result, gk::Array<double>& multipliers, gk::String& error)
{
    if (!PrepareError(error))
    {
        return false;
    }
    if (!feasibleStart || count != variableCount || count == 0u || &result == &multipliers)
    {
        return Fail(error, "Secondary motion contact inequality solve range is invalid");
    }
    // 小さいベクトルだけをstackに置き、密行列はfoundationが所有する。
    double point[kMaximumVariables] = {};
    double gradient[kMaximumVariables] = {};
    double direction[kMaximumVariables] = {};
    double rowVector[kMaximumVariables] = {};
    double activeRhs[kMaximumVariables] = {};
    double activeMultipliers[kMaximumVariables] = {};
    uint32_t active[kMaximumVariables] = {};
    uint32_t activeCount = 0u;
    const uint32_t square = count * count;
    gk::Array<double> workspace;
    if (!ZeroArray(5u * square + rows.Count(), workspace))
    {
        return Fail(error, "Secondary motion contact inequality allocation failed");
    }
    double* activeBasis = workspace.Data();
    double* triangular = activeBasis + square;
    double* nullBasis = triangular + square;
    double* reduced = nullBasis + square;
    double* reducedFactor = reduced + square;
    double* rowNorms = reducedFactor + square;
    if (!Factor(hessian.Data(), count, count, reducedFactor))
    {
        return Fail(error, "Secondary motion contact inequality Hessian is not positive definite");
    }
    for (uint32_t index = 0u; index < count; ++index)
    {
        if (!isfinite(feasibleStart[index]))
        {
            return Fail(error, "Secondary motion contact inequality start is not finite");
        }
        point[index] = feasibleStart[index];
    }
    for (uint32_t rowIndex = 0u; rowIndex < rows.Count(); ++rowIndex)
    {
        const auto& row = rows.At(rowIndex);
        double largest = 0.0;
        for (uint32_t offset = 0u; offset < row.entryCount; ++offset)
        {
            largest = fmax(largest, fabs(entries.At(row.firstEntry + offset).value));
        }
        double normSquared = 0.0;
        for (uint32_t offset = 0u; largest > 0.0 && offset < row.entryCount; ++offset)
        {
            const double value = entries.At(row.firstEntry + offset).value / largest;
            normSquared += value * value;
        }
        rowNorms[rowIndex] = largest * sqrt(normSquared);
        if (!isfinite(rowNorms[rowIndex]))
        {
            return Fail(error, "Secondary motion contact inequality row norm overflowed");
        }
        if (rowNorms[rowIndex] == 0.0)
        {
            if (row.lowerBound > 0.0)
            {
                return false;
            }
            continue;
        }
        double value = 0.0;
        for (uint32_t offset = 0u; offset < row.entryCount; ++offset)
        {
            const auto& entry = entries.At(row.firstEntry + offset);
            value += (entry.value / rowNorms[rowIndex]) * point[entry.variable];
        }
        const double bound = row.lowerBound / rowNorms[rowIndex];
        if (!isfinite(value) || !isfinite(bound))
        {
            return Fail(error, "Secondary motion contact inequality row evaluation overflowed");
        }
        if (value < bound - 1.0e-10 * (1.0 + fabs(bound) + fabs(value)))
        {
            return false;
        }
    }
    // 退化した制約でも無制限に入替え続けず、有限回で呼出し側へ戻す。
    const uint32_t maximumIterations = 128u + 16u * count;
    for (uint32_t iteration = 0u; iteration < maximumIterations; ++iteration)
    {
        double pointScale = 0.0;
        double gradientScale = 0.0;
        for (uint32_t row = 0u; row < count; ++row)
        {
            gradient[row] = objective.At(row);
            for (uint32_t column = 0u; column < count; ++column)
            {
                gradient[row] += hessian.At(row * count + column) * point[column];
            }
            if (!isfinite(gradient[row]))
            {
                return Fail(error, "Secondary motion contact inequality gradient overflowed");
            }
            pointScale = fmax(pointScale, fabs(point[row]));
            gradientScale = fmax(gradientScale, fabs(gradient[row]));
        }
        // active行を二重の修正Gram-Schmidtで直交化し、Hの逆行列の差引を避ける。
        for (uint32_t item = 0u; item < activeCount; ++item)
        {
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                rowVector[variable] = 0.0;
            }
            const auto& row = rows.At(active[item]);
            for (uint32_t offset = 0u; offset < row.entryCount; ++offset)
            {
                const auto& entry = entries.At(row.firstEntry + offset);
                rowVector[entry.variable] = entry.value / rowNorms[active[item]];
            }
            for (uint32_t other = 0u; other < item; ++other)
            {
                triangular[item * count + other] = 0.0;
            }
            for (uint32_t pass = 0u; pass < 2u; ++pass)
            {
                for (uint32_t other = 0u; other < item; ++other)
                {
                    double projection = 0.0;
                    for (uint32_t variable = 0u; variable < count; ++variable)
                    {
                        projection += rowVector[variable] * activeBasis[other * count + variable];
                    }
                    triangular[item * count + other] += projection;
                    for (uint32_t variable = 0u; variable < count; ++variable)
                    {
                        rowVector[variable] -= projection * activeBasis[other * count + variable];
                    }
                }
            }
            double norm = 0.0;
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                norm += rowVector[variable] * rowVector[variable];
            }
            norm = sqrt(norm);
            if (!(norm > 128.0 * DBL_EPSILON) || !isfinite(norm))
            {
                return false;
            }
            triangular[item * count + item] = norm;
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                activeBasis[item * count + variable] = rowVector[variable] / norm;
            }
        }
        // active面に沿う直交基底を、座標軸から再直交化して作る。
        uint32_t nullCount = 0u;
        for (uint32_t axis = 0u; axis < count && nullCount < count - activeCount; ++axis)
        {
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                rowVector[variable] = variable == axis ? 1.0 : 0.0;
            }
            for (uint32_t pass = 0u; pass < 2u; ++pass)
            {
                for (uint32_t item = 0u; item < activeCount + nullCount; ++item)
                {
                    const double* basis = item < activeCount ? activeBasis + item * count : nullBasis + (item - activeCount) * count;
                    double projection = 0.0;
                    for (uint32_t variable = 0u; variable < count; ++variable)
                    {
                        projection += rowVector[variable] * basis[variable];
                    }
                    for (uint32_t variable = 0u; variable < count; ++variable)
                    {
                        rowVector[variable] -= projection * basis[variable];
                    }
                }
            }
            double norm = 0.0;
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                norm += rowVector[variable] * rowVector[variable];
            }
            norm = sqrt(norm);
            if (norm <= 1.0e-8)
            {
                continue;
            }
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                nullBasis[nullCount * count + variable] = rowVector[variable] / norm;
            }
            ++nullCount;
        }
        if (nullCount != count - activeCount)
        {
            return false;
        }
        // reduced Hessianを直接作り、許される接線だけで二次目的を最小化する。
        for (uint32_t item = 0u; item < nullCount; ++item)
        {
            for (uint32_t row = 0u; row < count; ++row)
            {
                rowVector[row] = 0.0;
                for (uint32_t column = 0u; column < count; ++column)
                {
                    rowVector[row] += hessian.At(row * count + column) * nullBasis[item * count + column];
                }
            }
            activeRhs[item] = 0.0;
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                activeRhs[item] -= nullBasis[item * count + variable] * gradient[variable];
            }
            for (uint32_t other = 0u; other <= item; ++other)
            {
                double value = 0.0;
                for (uint32_t variable = 0u; variable < count; ++variable)
                {
                    value += nullBasis[other * count + variable] * rowVector[variable];
                }
                reduced[item * count + other] = value;
                reduced[other * count + item] = value;
            }
        }
        if (nullCount > 0u && (!Factor(reduced, nullCount, count, reducedFactor) || !SolveFactor(reducedFactor, nullCount, count, activeRhs, activeMultipliers)))
        {
            return Fail(error, "Secondary motion contact inequality reduced solve is invalid");
        }
        double directionScale = 0.0;
        for (uint32_t variable = 0u; variable < count; ++variable)
        {
            direction[variable] = 0.0;
            for (uint32_t item = 0u; item < nullCount; ++item)
            {
                direction[variable] += nullBasis[item * count + variable] * activeMultipliers[item];
            }
            if (!isfinite(direction[variable]))
            {
                return Fail(error, "Secondary motion contact inequality step is not finite");
            }
            directionScale = fmax(directionScale, fabs(direction[variable]));
        }
        // 正規化したactive行のQR因子から、元の不等式向きの乗数を得る。
        for (uint32_t offset = 0u; offset < activeCount; ++offset)
        {
            const uint32_t item = activeCount - offset - 1u;
            double value = 0.0;
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                value += activeBasis[item * count + variable] * gradient[variable];
            }
            for (uint32_t other = item + 1u; other < activeCount; ++other)
            {
                value -= triangular[other * count + item] * activeMultipliers[other];
            }
            activeMultipliers[item] = value / triangular[item * count + item];
            if (!isfinite(activeMultipliers[item]))
            {
                return Fail(error, "Secondary motion contact inequality multiplier is not finite");
            }
        }
        if (directionScale <= 1.0e-11 * (1.0 + pointScale))
        {
            // 負乗数の行だけを番号順に外し、同率での選択を固定する。
            uint32_t remove = UINT32_MAX;
            for (uint32_t item = 0u; item < activeCount; ++item)
            {
                if (activeMultipliers[item] < -1.0e-10 * (1.0 + gradientScale) && (remove == UINT32_MAX || active[item] < active[remove]))
                {
                    remove = item;
                }
            }
            if (remove != UINT32_MAX)
            {
                for (uint32_t item = remove + 1u; item < activeCount; ++item)
                {
                    active[item - 1u] = active[item];
                }
                --activeCount;
                continue;
            }
            // 全行とKKT残差を再検査し、両出力の確保が済むまで公開しない。
            gk::Array<double> candidate;
            gk::Array<double> candidateMultipliers;
            if (!candidate.AppendRange(point, count) || !ZeroArray(rows.Count(), candidateMultipliers))
            {
                return Fail(error, "Secondary motion contact inequality allocation failed");
            }
            for (uint32_t item = 0u; item < activeCount; ++item)
            {
                const double multiplier = fmax(0.0, activeMultipliers[item]) / rowNorms[active[item]];
                if (!isfinite(multiplier))
                {
                    return Fail(error, "Secondary motion contact inequality multiplier overflowed");
                }
                candidateMultipliers.At(active[item]) = multiplier;
            }
            double stationarity[kMaximumVariables] = {};
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                stationarity[variable] = gradient[variable];
            }
            for (uint32_t rowIndex = 0u; rowIndex < rows.Count(); ++rowIndex)
            {
                const auto& row = rows.At(rowIndex);
                double value = 0.0;
                for (uint32_t offset = 0u; offset < row.entryCount; ++offset)
                {
                    const auto& entry = entries.At(row.firstEntry + offset);
                    if (rowNorms[rowIndex] > 0.0)
                    {
                        value += (entry.value / rowNorms[rowIndex]) * point[entry.variable];
                    }
                    stationarity[entry.variable] -= entry.value * candidateMultipliers.At(rowIndex);
                }
                if (rowNorms[rowIndex] > 0.0)
                {
                    const double bound = row.lowerBound / rowNorms[rowIndex];
                    if (!isfinite(value) || value < bound - 1.0e-9 * (1.0 + fabs(bound) + fabs(value)))
                    {
                        return false;
                    }
                }
            }
            for (uint32_t variable = 0u; variable < count; ++variable)
            {
                if (!isfinite(stationarity[variable]))
                {
                    return Fail(error, "Secondary motion contact inequality KKT residual overflowed");
                }
                if (fabs(stationarity[variable]) > 1.0e-8 * (1.0 + gradientScale))
                {
                    return false;
                }
            }
            result.MoveFrom(candidate);
            multipliers.MoveFrom(candidateMultipliers);
            return true;
        }
        double fraction = 1.0;
        uint32_t blocking = UINT32_MAX;
        for (uint32_t rowIndex = 0u; rowIndex < rows.Count(); ++rowIndex)
        {
            bool isActive = false;
            for (uint32_t item = 0u; item < activeCount; ++item)
            {
                isActive = isActive || active[item] == rowIndex;
            }
            if (isActive || rowNorms[rowIndex] == 0.0)
            {
                continue;
            }
            const auto& row = rows.At(rowIndex);
            double slope = 0.0;
            double value = 0.0;
            for (uint32_t offset = 0u; offset < row.entryCount; ++offset)
            {
                const auto& entry = entries.At(row.firstEntry + offset);
                const double coefficient = entry.value / rowNorms[rowIndex];
                slope += coefficient * direction[entry.variable];
                value += coefficient * point[entry.variable];
            }
            if (!isfinite(slope) || !isfinite(value))
            {
                return Fail(error, "Secondary motion contact inequality line search overflowed");
            }
            if (slope >= -64.0 * DBL_EPSILON * (1.0 + directionScale))
            {
                continue;
            }
            const double candidateFraction = fmax(0.0, (value - row.lowerBound / rowNorms[rowIndex]) / -slope);
            if (!isfinite(candidateFraction))
            {
                return Fail(error, "Secondary motion contact inequality step fraction overflowed");
            }
            if (candidateFraction < fraction || (candidateFraction == fraction && blocking != UINT32_MAX && rowIndex < blocking))
            {
                fraction = candidateFraction;
                blocking = rowIndex;
            }
        }
        for (uint32_t variable = 0u; variable < count; ++variable)
        {
            point[variable] += fraction * direction[variable];
            if (!isfinite(point[variable]))
            {
                return Fail(error, "Secondary motion contact inequality solution overflowed");
            }
        }
        if (blocking != UINT32_MAX)
        {
            if (activeCount == count)
            {
                return false;
            }
            active[activeCount++] = blocking;
        }
    }
    return false;
}

bool FSecondaryMotionContactInequalitySystem::UpdateHessian(const double* step, const double* lagrangianGradientDifference, uint32_t count, gk::String& error)
{
    if (!PrepareError(error))
    {
        return false;
    }
    if (!step || !lagrangianGradientDifference || count != variableCount || count == 0u)
    {
        return Fail(error, "Secondary motion contact inequality BFGS range is invalid");
    }
    double product[kMaximumVariables] = {};
    double correction[kMaximumVariables] = {};
    double curvature = 0.0;
    double observed = 0.0;
    for (uint32_t row = 0u; row < count; ++row)
    {
        if (!isfinite(step[row]) || !isfinite(lagrangianGradientDifference[row]))
        {
            return Fail(error, "Secondary motion contact inequality BFGS input is not finite");
        }
        for (uint32_t column = 0u; column < count; ++column)
        {
            product[row] += hessian.At(row * count + column) * step[column];
        }
        curvature += step[row] * product[row];
        observed += step[row] * lagrangianGradientDifference[row];
    }
    if (!isfinite(curvature) || !isfinite(observed) || !(curvature > 0.0))
    {
        return Fail(error, "Secondary motion contact inequality BFGS curvature is invalid");
    }
    // 負の曲率も目的行列側へ寄せ、更新後の正定値性を保つ。
    const double theta = observed >= 0.2 * curvature ? 1.0 : 0.8 * curvature / (curvature - observed);
    double dampedCurvature = 0.0;
    for (uint32_t variable = 0u; variable < count; ++variable)
    {
        correction[variable] = theta * lagrangianGradientDifference[variable] + (1.0 - theta) * product[variable];
        dampedCurvature += step[variable] * correction[variable];
    }
    if (!isfinite(theta) || !isfinite(dampedCurvature) || !(dampedCurvature > 0.0))
    {
        return Fail(error, "Secondary motion contact inequality damped curvature is invalid");
    }
    gk::Array<double> candidate;
    gk::Array<double> factor;
    if (!ZeroArray(count * count, candidate) || !ZeroArray(count * count, factor))
    {
        return Fail(error, "Secondary motion contact inequality allocation failed");
    }
    for (uint32_t row = 0u; row < count; ++row)
    {
        for (uint32_t column = 0u; column <= row; ++column)
        {
            const double value = hessian.At(row * count + column) - product[row] * (product[column] / curvature) + correction[row] * (correction[column] / dampedCurvature);
            if (!isfinite(value))
            {
                return Fail(error, "Secondary motion contact inequality BFGS matrix overflowed");
            }
            candidate.At(row * count + column) = value;
            candidate.At(column * count + row) = value;
        }
    }
    if (!Factor(candidate.Data(), count, count, factor.Data()))
    {
        return Fail(error, "Secondary motion contact inequality BFGS lost positive definiteness");
    }
    hessian.MoveFrom(candidate);
    return true;
}
}
