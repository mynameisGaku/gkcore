// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FSecondaryMotionContactLinearSystem.h"
#include "foundation/Memory.h"

#include <math.h>
#include <stdio.h>

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
 * 三次元ベクトルの成分が期待値に近いか確認する。
 */
bool Near(const gk::FVector3d& value, double x, double y, double z, double tolerance)
{
    return fabs(value.value[0] - x) <= tolerance && fabs(value.value[1] - y) <= tolerance && fabs(value.value[2] - z) <= tolerance;
}

/**
 * 2本の接触行から連立最小二乗解を求める。
 */
bool TestCoupledRowsSolveTheNormalEquations()
{
    const gk::FVector3d directions[2] = { { { 0.0, 0.0, 1.0 } }, { { 0.0, 0.0, 1.0 } } };
    FSecondaryMotionContactLinearSystem system;
    gk::String error;
    gk::Array<gk::FVector3d> result;
    if (!result.Append({ { 9.0, 8.0, 7.0 } }) || !system.Reset(directions, 2, error) || !system.BeginRow(1.0, error) || !system.AddCoefficient(0, { { 1.0, 0.0, 0.0 } }, error) || !system.AddCoefficient(1, { { 0.0, 1.0, 0.0 } }, error) || !system.BeginRow(0.2, error) || !system.AddCoefficient(0, { { 1.0, 0.0, 0.0 } }, error) || !system.AddCoefficient(1, { { 0.0, -1.0, 0.0 } }, error) || !system.Solve(1.0e-12, result, error))
    {
        return Fail("coupled contact rows were rejected");
    }
    if (result.Count() != 2 || !Near(result.At(0), 0.6, 0.0, 0.0, 1.0e-7) || !Near(result.At(1), 0.0, 0.4, 0.0, 1.0e-7))
    {
        return Fail("matrix-free solve did not preserve cross-row coupling");
    }
    return true;
}

/**
 * 同一segmentの重複係数が一つにまとめた係数と同じ行応答になるか調べる。
 */
bool TestDuplicateCoefficientsPreserveRowSum()
{
    const gk::FVector3d direction = { { 0.0, 0.0, 1.0 } };
    FSecondaryMotionContactLinearSystem duplicateSystem;
    FSecondaryMotionContactLinearSystem mergedSystem;
    gk::String error;
    gk::Array<gk::FVector3d> duplicateResult;
    gk::Array<gk::FVector3d> mergedResult;
    if (!duplicateSystem.Reset(&direction, 1, error) || !duplicateSystem.BeginRow(0.75, error) || !duplicateSystem.AddCoefficient(0, { { 0.5, 0.0, 0.0 } }, error) || !duplicateSystem.AddCoefficient(0, { { 1.5, 0.0, 0.0 } }, error) || !duplicateSystem.Solve(1.0e-5, duplicateResult, error) || !mergedSystem.Reset(&direction, 1, error) || !mergedSystem.BeginRow(0.75, error) || !mergedSystem.AddCoefficient(0, { { 2.0, 0.0, 0.0 } }, error) || !mergedSystem.Solve(1.0e-5, mergedResult, error))
    {
        return Fail("duplicate coefficient comparison systems were rejected");
    }
    if (duplicateResult.Count() != 1 || mergedResult.Count() != 1 || !Near(duplicateResult.At(0), mergedResult.At(0).value[0], mergedResult.At(0).value[1], mergedResult.At(0).value[2], 1.0e-12))
    {
        return Fail("duplicate segment coefficients were not summed in Jx and Jt");
    }
    return true;
}

/**
 * 方向に平行な導関数が接線空間でゼロになり、解が安定するか調べる。
 */
bool TestRadialDerivativeAndDegenerateRowsAreStable()
{
    const gk::FVector3d direction = { { 1.0, 0.0, 0.0 } };
    FSecondaryMotionContactLinearSystem system;
    gk::String error;
    gk::Array<gk::FVector3d> result;
    if (!system.Reset(&direction, 1, error) || !system.BeginRow(1.0, error) || !system.AddCoefficient(0, { { 3.0, 0.0, 0.0 } }, error) || !system.BeginRow(0.5, error) || !system.Solve(1.0e-3, result, error))
    {
        return Fail("radial or empty contact row was rejected");
    }
    if (result.Count() != 1 || !Near(result.At(0), 0.0, 0.0, 0.0, 1.0e-14) || !isfinite(system.GetMaximumDiagonal()) || system.GetMaximumDiagonal() != 0.0 || system.GetRowCount() != 2)
    {
        return Fail("radial-only and empty rows did not produce a finite zero step");
    }
    return true;
}

/**
 * 斜めの接触導関数から鎖方向成分だけを除いて解くか確認する。
 */
bool TestMixedDerivativeKeepsOnlyTangentComponents()
{
    const gk::FVector3d direction = { { 0.0, 0.0, 1.0 } };
    FSecondaryMotionContactLinearSystem system;
    gk::String error;
    gk::Array<gk::FVector3d> result;
    if (!system.Reset(&direction, 1, error) || !system.BeginRow(1.0, error) || !system.AddCoefficient(0, { { 3.0, 4.0, 7.0 } }, error) || !system.Solve(1.0e-12, result, error))
    {
        return Fail("mixed radial and tangent derivative was rejected");
    }
    if (result.Count() != 1 || !Near(result.At(0), 3.0 / 25.0, 4.0 / 25.0, 0.0, 1.0e-10) || fabs(result.At(0).value[2]) > 1.0e-12)
    {
        return Fail("linearization retained the radial derivative component");
    }
    return true;
}

/**
 * ランク不足の接触行でも減衰付き解が有限値へ収束するか調べる。
 */
bool TestDampingStabilizesRankDeficientRows()
{
    const gk::FVector3d directions[2] = { { { 0.0, 0.0, 1.0 } }, { { 0.0, 0.0, 1.0 } } };
    FSecondaryMotionContactLinearSystem system;
    gk::String error;
    gk::Array<gk::FVector3d> result;
    if (!system.Reset(directions, 2, error) || !system.BeginRow(1.0, error) || !system.AddCoefficient(0, { { 1.0, 0.0, 0.0 } }, error) || !system.AddCoefficient(1, { { 1.0, 0.0, 0.0 } }, error) || !system.BeginRow(2.0, error) || !system.AddCoefficient(0, { { 2.0, 0.0, 0.0 } }, error) || !system.AddCoefficient(1, { { 2.0, 0.0, 0.0 } }, error) || !system.Solve(1.0e-3, result, error))
    {
        return Fail("damped rank-deficient contact rows were rejected");
    }
    if (result.Count() != 2 || !isfinite(result.At(0).value[0]) || !isfinite(result.At(1).value[0]) || fabs(result.At(0).value[0] - result.At(1).value[0]) > 1.0e-8 || result.At(0).value[0] <= 0.0 || result.At(0).value[0] >= 0.51)
    {
        return Fail("damping did not stabilize the rank-deficient normal system");
    }
    return true;
}

/**
 * 24方向の結合した低ランク行を、有限な接線更新として解くか確認する。
 */
bool TestLargeCoupledLowRankSystem()
{
    const uint32_t segmentCount = 24;
    gk::FVector3d directions[24]{};
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        directions[index] = { { 0.0, 0.0, 1.0 } };
    }
    FSecondaryMotionContactLinearSystem system;
    gk::String error;
    gk::Array<gk::FVector3d> result;
    if (!system.Reset(directions, segmentCount, error) || !system.BeginRow(4.0, error))
    {
        return Fail("large low-rank system could not begin its first row");
    }
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        if (!system.AddCoefficient(index, { { 1.0, 0.0, 0.0 } }, error))
        {
            return Fail("large low-rank system rejected its first row coefficient");
        }
    }
    if (!system.BeginRow(1.6, error))
    {
        return Fail("large low-rank system could not begin its second row");
    }
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        const double coefficient = index < 8u ? -1.0 : (index < 16u ? 1.0 : 0.0);
        if (!system.AddCoefficient(index, { { coefficient, 0.0, 0.0 } }, error))
        {
            return Fail("large low-rank system rejected its second row coefficient");
        }
    }
    if (!system.BeginRow(2.4, error))
    {
        return Fail("large low-rank system could not begin its third row");
    }
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        const double coefficient = index < 8u ? 0.0 : (index < 16u ? 1.0 : -1.0);
        if (!system.AddCoefficient(index, { { coefficient, 0.0, 0.0 } }, error))
        {
            return Fail("large low-rank system rejected its third row coefficient");
        }
    }
    if (!system.Solve(1.0e-12, result, error) || result.Count() != segmentCount)
    {
        return Fail("large coupled low-rank system did not solve");
    }
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        const double expected = index < 8u ? 1.0 / 7.5 : (index < 16u ? 1.0 / 3.0 : 1.0 / 30.0);
        if (!isfinite(result.At(index).value[0]) || fabs(result.At(index).value[0] - expected) > 1.0e-7 || fabs(result.At(index).value[1]) > 1.0e-12 || fabs(result.At(index).value[2]) > 1.0e-12)
        {
            return Fail("large coupled low-rank solve returned an incorrect or non-tangent update");
        }
    }
    return true;
}

/**
 * 反復上限の未収束を入力異常と区別し、既存出力を保持するか確認する。
 */
bool TestIterationLimitIsAQuietAtomicFailure()
{
    const uint32_t segmentCount = 128;
    gk::FVector3d directions[128]{};
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        directions[index] = { { 0.0, 0.0, 1.0 } };
    }
    FSecondaryMotionContactLinearSystem system;
    gk::String error;
    gk::Array<gk::FVector3d> result;
    if (!result.Append({ { 4.0, 5.0, 6.0 } }) || !system.Reset(directions, segmentCount, error))
    {
        return Fail("could not prepare the iteration-limit contract case");
    }
    const double inverseRootCount = 1.0 / sqrt(static_cast<double>(segmentCount));
    for (uint32_t rowIndex = 0; rowIndex < segmentCount; ++rowIndex)
    {
        const double eigenvalue = pow(1000.0, static_cast<double>(rowIndex) / static_cast<double>(segmentCount - 1u));
        const double rowScale = sqrt(eigenvalue) * inverseRootCount;
        if (!system.BeginRow(static_cast<double>(rowIndex + 1u) / static_cast<double>(segmentCount), error))
        {
            return Fail("could not append a row to the iteration-limit case");
        }
        for (uint32_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
        {
            uint32_t parity = 0;
            for (uint32_t bit = 1; bit < segmentCount; bit <<= 1u)
            {
                parity ^= ((rowIndex & segmentIndex & bit) != 0u) ? 1u : 0u;
            }
            const double sign = parity ? -1.0 : 1.0;
            if (!system.AddCoefficient(segmentIndex, { { rowScale * sign, 0.0, 0.0 } }, error))
            {
                return Fail("could not append a coupled coefficient to the iteration-limit case");
            }
        }
    }
    if (system.Solve(1.0e-12, result, error) || !error.Empty())
    {
        return Fail("iteration-limit nonconvergence was reported as an input error or accepted as a solution");
    }
    if (result.Count() != 1 || !Near(result.At(0), 4.0, 5.0, 6.0, 0.0))
    {
        return Fail("iteration-limit nonconvergence modified the existing output");
    }
    return true;
}

/**
 * 接線方向数、接触行数、重複をまとめた係数数の資源上限を確認する。
 */
bool TestResourceBudgetsFallbackQuietly()
{
    const gk::FVector3d direction = { { 0.0, 0.0, 1.0 } };
    gk::FVector3d tooManyDirections[257]{};
    for (uint32_t index = 0; index < 257u; ++index)
    {
        tooManyDirections[index] = direction;
    }
    FSecondaryMotionContactLinearSystem system;
    gk::String error;
    if (system.Reset(tooManyDirections, 257u, error) || !error.Empty())
    {
        return Fail("direction budget exhaustion was not a quiet fallback");
    }
    if (!system.Reset(&direction, 1u, error))
    {
        return Fail("could not reset after direction budget fallback");
    }
    for (uint32_t rowIndex = 0; rowIndex < 4096u; ++rowIndex)
    {
        if (!system.BeginRow(0.0, error))
        {
            return Fail("row budget rejected an allowed row");
        }
    }
    if (system.BeginRow(0.0, error) || !error.Empty() || system.GetRowCount() != 4096u)
    {
        return Fail("row budget exhaustion was not a quiet fallback");
    }
    gk::FVector3d directions[256]{};
    for (uint32_t index = 0; index < 256u; ++index)
    {
        directions[index] = direction;
    }
    if (!system.Reset(directions, 256u, error))
    {
        return Fail("could not reset before the sparse-entry budget case");
    }
    for (uint32_t rowIndex = 0; rowIndex < 256u; ++rowIndex)
    {
        if (!system.BeginRow(0.0, error))
        {
            return Fail("sparse-entry case could not append an allowed row");
        }
        for (uint32_t segmentIndex = 0; segmentIndex < 256u; ++segmentIndex)
        {
            if (!system.AddCoefficient(segmentIndex, { { 1.0, 0.0, 0.0 } }, error))
            {
                return Fail("sparse-entry case rejected an allowed distinct coefficient");
            }
        }
    }
    if (!system.AddCoefficient(0u, { { 0.5, 0.0, 0.0 } }, error) || !system.BeginRow(0.0, error) || system.AddCoefficient(0u, { { 1.0, 0.0, 0.0 } }, error) || !error.Empty())
    {
        return Fail("distinct-entry cap did not allow duplicates then quietly reject growth");
    }
    return true;
}

/**
 * 不正入力と作業領域確保失敗で、既存の出力値を保つか確認する。
 */
bool TestInvalidInputsAndAllocationFailurePreserveOutput()
{
    const gk::FVector3d direction = { { 0.0, 0.0, 1.0 } };
    const gk::FVector3d nonUnit = { { 0.0, 0.0, 2.0 } };
    FSecondaryMotionContactLinearSystem system;
    gk::String error;
    gk::Array<gk::FVector3d> result;
    const gk::FVector3d sentinel = { { 4.0, 5.0, 6.0 } };
    if (!result.Append(sentinel) || system.Reset(nullptr, 1, error) || system.Reset(&nonUnit, 1, error) || !system.Reset(&direction, 1, error) || !system.BeginRow(1.0, error) || system.AddCoefficient(1, { { 1.0, 0.0, 0.0 } }, error) || system.AddCoefficient(0, { { NAN, 0.0, 0.0 } }, error) || system.Solve(-1.0, result, error))
    {
        return Fail("invalid linear-system input was accepted");
    }
    if (result.Count() != 1 || !Near(result.At(0), 4.0, 5.0, 6.0, 0.0))
    {
        return Fail("invalid-input rejection modified the existing result");
    }
    if (error.Empty())
    {
        return Fail("invalid damping did not report a nonempty error");
    }
    if (!system.Reset(&direction, 1, error) || !system.BeginRow(1.0, error) || !system.AddCoefficient(0, { { 1.0, 0.0, 0.0 } }, error))
    {
        return Fail("could not prepare allocation-failure case");
    }
    gk::SetAllocationFailureAfterForTesting(0);
    const bool solved = system.Solve(1.0e-3, result, error);
    gk::ResetAllocationFailureForTesting();
    if (solved || error.Empty() || result.Count() != 1 || !Near(result.At(0), 4.0, 5.0, 6.0, 0.0))
    {
        return Fail("allocation failure did not preserve the existing result");
    }
    return true;
}

}

int main()
{
    return TestCoupledRowsSolveTheNormalEquations() && TestDuplicateCoefficientsPreserveRowSum() && TestRadialDerivativeAndDegenerateRowsAreStable() && TestMixedDerivativeKeepsOnlyTangentComponents() && TestDampingStabilizesRankDeficientRows() && TestLargeCoupledLowRankSystem() && TestIterationLimitIsAQuietAtomicFailure() && TestResourceBudgetsFallbackQuietly() && TestInvalidInputsAndAllocationFailurePreserveOutput() ? 0 : 1;
}
