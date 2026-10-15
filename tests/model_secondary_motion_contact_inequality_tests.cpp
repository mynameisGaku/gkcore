// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FSecondaryMotionContactInequalitySystem.h"
#include "foundation/Memory.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
using gk::model::animation::FSecondaryMotionContactInequalitySystem;
/**
 * 解析解との比較が失敗した理由を表示する。
 */
bool Fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return false;
}
/**
 * 非制約問題の解析解と空の乗数配列を確認する。
 */
bool TestUnconstrainedAnalyticSolution()
{
    FSecondaryMotionContactInequalitySystem system;
    gk::String error;
    const double gradient[2] = { -2.0, 1.0 };
    const double start[2] = { 0.0, 0.0 };
    gk::Array<double> result, multipliers;
    if (!system.Reset(2u, error) || !system.SetObjective(gradient, 2u, error) || !system.Solve(start, 2u, result, multipliers, error))
    {
        return Fail("非制約二次計画の解析解を得られません");
    }
    return result.Count() == 2u && fabs(result.At(0u) - 2.0) < 1.0e-10 && fabs(result.At(1u) + 1.0) < 1.0e-10 && multipliers.Count() == 0u || Fail("非制約二次計画の解が解析解と異なります");
}
/**
 * 一つの下限を補助関数なしに追加する。
 */
bool AddRow(FSecondaryMotionContactInequalitySystem& system, double bound, double first, double second, gk::String& error)
{
    return system.BeginRow(bound, error) && system.AddCoefficient(0u, first, error) && system.AddCoefficient(1u, second, error);
}
/**
 * 正の境界と負のinactive境界を区別し、KKT乗数の符号も確認する。
 */
bool TestSignedInactiveBounds()
{
    FSecondaryMotionContactInequalitySystem system;
    gk::String error;
    const double start[2] = { 1.0, 0.0 };
    gk::Array<double> result, multipliers;
    if (!system.Reset(2u, error) || !AddRow(system, 1.0, 1.0, 0.0, error) || !AddRow(system, -2.0, 0.0, 1.0, error) || !AddRow(system, -3.0, 0.0, -1.0, error) || !system.Solve(start, 2u, result, multipliers, error))
    {
        return Fail("符号付き下限の二次計画を解けません");
    }
    return fabs(result.At(0u) - 1.0) < 1.0e-10 && fabs(result.At(1u)) < 1.0e-10 && multipliers.Count() == 3u && fabs(multipliers.At(0u) - 1.0) < 1.0e-10 && multipliers.At(1u) == 0.0 && multipliers.At(2u) == 0.0 || Fail("inactive境界が解または乗数を変えました");
}
/**
 * 一度入った境界を負乗数で外し、正しい面上の解析解へ移る。
 */
bool TestActiveRowRemoval()
{
    FSecondaryMotionContactInequalitySystem system;
    gk::String error;
    const double start[2] = { 2.0, 6.0 };
    gk::Array<double> result, multipliers;
    if (!system.Reset(2u, error) || !AddRow(system, 1.0, 1.0, 0.0, error) || !AddRow(system, 2.5, 1.0, 1.0, error) || !system.Solve(start, 2u, result, multipliers, error))
    {
        return Fail("負乗数の境界を外した解を得られません");
    }
    return fabs(result.At(0u) - 1.25) < 1.0e-9 && fabs(result.At(1u) - 1.25) < 1.0e-9 && fabs(multipliers.At(0u)) < 1.0e-9 && fabs(multipliers.At(1u) - 1.25) < 1.0e-9 || Fail("境界の入替え後の解析解が異なります");
}
/**
 * 同じ変数の合算、同率の重複行、反対向きの等号を安定して扱う。
 */
bool TestRedundantAndTiedRows()
{
    FSecondaryMotionContactInequalitySystem system;
    gk::String error;
    const double start[1] = { 1.0 };
    gk::Array<double> result, multipliers;
    if (!system.Reset(1u, error) || !system.BeginRow(1.0, error) || !system.AddCoefficient(0u, .25, error) || !system.AddCoefficient(0u, .75, error) || !system.BeginRow(2.0, error) || !system.AddCoefficient(0u, 2.0, error) || !system.BeginRow(-1.0, error) || !system.AddCoefficient(0u, -1.0, error) || !system.Solve(start, 1u, result, multipliers, error))
    {
        return Fail("冗長・同率・反対向きの行を扱えません");
    }
    return fabs(result.At(0u) - 1.0) < 1.0e-10 && fabs(multipliers.At(0u) - 1.0) < 1.0e-10 && multipliers.At(1u) == 0.0 && multipliers.At(2u) == 0.0 || Fail("同率の行選択または係数合算が安定しません");
}
/**
 * 相反条件または不正値による失敗で、両出力と所有領域を保持する。
 */
bool TestFailureIsAtomic()
{
    FSecondaryMotionContactInequalitySystem system;
    gk::String error;
    const double start[1] = { 0.0 };
    gk::Array<double> result, multipliers;
    result.Append(71.0);
    multipliers.Append(93.0);
    const double* savedResult = result.Data();
    const double* savedMultipliers = multipliers.Data();
    if (!system.Reset(1u, error) || !system.BeginRow(1.0, error) || !system.AddCoefficient(0u, 1.0, error) || !system.BeginRow(0.0, error) || !system.AddCoefficient(0u, -1.0, error))
    {
        return Fail("相反条件を準備できません");
    }
    if (system.Solve(start, 1u, result, multipliers, error) || !error.Empty() || result.Data() != savedResult || multipliers.Data() != savedMultipliers || result.Count() != 1u || multipliers.Count() != 1u || result.At(0u) != 71.0 || multipliers.At(0u) != 93.0)
    {
        return Fail("不成立の二次計画が出力を変えました");
    }
    const double invalid[1] = { HUGE_VAL };
    if (system.SetObjective(invalid, 1u, error) || error.Empty() || system.AddCoefficient(1u, 1.0, error) || error.Empty() || system.BeginRow(HUGE_VAL, error) || error.Empty())
    {
        return Fail("二次計画が数値・変数範囲の不正を拒否しません");
    }
    system.ClearRows();
    if (system.Solve(start, 1u, result, result, error) || error.Empty())
    {
        return Fail("同じ出力配列の二重指定を拒否しません");
    }
    return true;
}
/**
 * BFGSの割線条件、負曲率の減衰、ClearRowsによる目的維持を解析解で検査する。
 */
bool TestHessianUpdatesAndClearRows()
{
    FSecondaryMotionContactInequalitySystem system;
    gk::String error;
    const double step[2] = { 1.0, 1.0 };
    const double difference[2] = { 3.0, 1.0 };
    const double gradient[2] = { -3.0, -1.0 };
    const double start[2] = { 0.0, 0.0 };
    gk::Array<double> result, multipliers;
    if (!system.Reset(2u, error) || !system.SetObjective(gradient, 2u, error) || !system.UpdateHessian(step, difference, 2u, error) || !AddRow(system, -4.0, 1.0, 0.0, error))
    {
        return Fail("BFGS割線条件を準備できません");
    }
    system.ClearRows();
    if (!system.Solve(start, 2u, result, multipliers, error) || fabs(result.At(0u) - 1.0) > 1.0e-9 || fabs(result.At(1u) - 1.0) > 1.0e-9 || multipliers.Count() != 0u)
    {
        return Fail("BFGSまたはClearRowsが目的行列・勾配を保ちません");
    }
    const double negativeDifference[2] = { -1.0, 0.0 };
    const double axisStep[2] = { 1.0, 0.0 };
    const double axisGradient[2] = { -1.0, 0.0 };
    if (!system.Reset(2u, error) || !system.SetObjective(axisGradient, 2u, error) || !system.UpdateHessian(axisStep, negativeDifference, 2u, error) || !system.Solve(start, 2u, result, multipliers, error) || fabs(result.At(0u) - 5.0) > 1.0e-8 || fabs(result.At(1u)) > 1.0e-10)
    {
        return Fail("Powell減衰が負曲率の正定値性を保ちません");
    }
    const double invalidStep[2] = { HUGE_VAL, 0.0 };
    if (system.UpdateHessian(invalidStep, negativeDifference, 2u, error) || error.Empty() || !system.Solve(start, 2u, result, multipliers, error) || fabs(result.At(0u) - 5.0) > 1.0e-8)
    {
        return Fail("失敗したBFGSが目的行列を書き換えました");
    }
    return true;
}
/**
 * workspaceと両出力の各確保境界を止め、ポインタと内容を保持する。
 */
bool TestAllocationFailuresAreAtomic()
{
    for (uint32_t budget = 0u; budget < 3u; ++budget)
    {
        FSecondaryMotionContactInequalitySystem system;
        gk::String error;
        const double start[2] = { 1.0, 0.0 };
        gk::Array<double> result, multipliers;
        if (!system.Reset(2u, error) || !AddRow(system, 1.0, 1.0, 0.0, error) || !result.Append(71.0) || !multipliers.Append(93.0))
        {
            return Fail("二次計画の確保失敗検査を準備できません");
        }
        const double* savedResult = result.Data();
        const double* savedMultipliers = multipliers.Data();
        gk::SetAllocationFailureAfterForTesting(budget);
        const bool solved = system.Solve(start, 2u, result, multipliers, error);
        gk::ResetAllocationFailureForTesting();
        if (solved || error.Empty() || result.Data() != savedResult || multipliers.Data() != savedMultipliers || result.Count() != 1u || multipliers.Count() != 1u || result.At(0u) != 71.0 || multipliers.At(0u) != 93.0)
        {
            return Fail("二次計画の確保失敗が両出力を保ちません");
        }
        if (!system.Solve(start, 2u, result, multipliers, error) || fabs(result.At(0u) - 1.0) > 1.0e-10)
        {
            return Fail("確保失敗後に二次計画を再開できません");
        }
    }
    return true;
}
/**
 * 変数数・行数・疎係数数の上限を有限の入力で検査する。
 */
bool TestWorkBounds()
{
    FSecondaryMotionContactInequalitySystem system;
    gk::String error;
    if (system.Reset(66u, error) || !error.Empty() || system.Reset(0u, error) || error.Empty() || !system.Reset(1u, error))
    {
        return Fail("二次計画の変数上限を守りません");
    }
    for (uint32_t row = 0u; row < 4096u; ++row)
    {
        if (!system.BeginRow(-1.0, error))
        {
            return Fail("上限内の制約行を登録できません");
        }
    }
    if (system.BeginRow(-1.0, error) || !error.Empty())
    {
        return Fail("二次計画の行数上限を守りません");
    }
    if (!system.Reset(65u, error))
    {
        return Fail("二次計画の最大変数数を登録できません");
    }
    uint32_t entries = 0u;
    while (entries < 65536u)
    {
        if (!system.BeginRow(-1.0, error))
        {
            return Fail("係数数上限の行を準備できません");
        }
        for (uint32_t variable = 0u; variable < 65u && entries < 65536u; ++variable)
        {
            if (!system.AddCoefficient(variable, 1.0, error))
            {
                return Fail("上限内の疎係数を登録できません");
            }
            ++entries;
        }
    }
    // 最後の行は16係数なので、次の変数は新しい係数になる。
    if (system.AddCoefficient(16u, 1.0, error) || !error.Empty())
    {
        return Fail("二次計画の疎係数数上限を守りません");
    }
    return true;
}
/**
 * 小さい固有値とactive行が重なる場合も、大数差引で停滞せず解析解へ達する。
 */
bool TestIllConditionedActiveTangent()
{
    FSecondaryMotionContactInequalitySystem system;
    gk::String error;
    const double step[2] = { 1.0, 0.0 };
    const double difference[2] = { 1.0e-9, 0.0 };
    const double gradient[2] = { -1.0, .123 };
    const double start[2] = { 0.0, 0.0 };
    gk::Array<double> result, multipliers;
    if (!system.Reset(2u, error) || !system.SetObjective(gradient, 2u, error))
    {
        return Fail("悪条件の接線問題を準備できません");
    }
    for (uint32_t update = 0u; update < 14u; ++update)
    {
        if (!system.UpdateHessian(step, difference, 2u, error))
        {
            return Fail("正定値を保った小さい固有値を準備できません");
        }
    }
    if (!AddRow(system, 0.0, -.3, -.7, error) || !system.Solve(start, 2u, result, multipliers, error))
    {
        return Fail("悪条件のactive接線でQPが停滞しました");
    }
    const double expectedY = -(7.0 / 3.0 + .123) / (1.0 + (49.0 / 9.0) * 1.0e-9);
    const double expectedX = -(7.0 / 3.0) * expectedY;
    const double expectedMultiplier = (1.0 - 1.0e-9 * expectedX) / .3;
    return fabs(result.At(0u) - expectedX) < 1.0e-8 && fabs(result.At(1u) - expectedY) < 1.0e-8 && fabs(multipliers.At(0u) - expectedMultiplier) < 1.0e-8 || Fail("悪条件のactive接線の解析解が異なります");
}
/**
 * 行の単位が小さい場合も、元の係数向きのKKT乗数を返す。
 */
bool TestScaledRowMultiplier()
{
    FSecondaryMotionContactInequalitySystem system;
    gk::String error;
    const double start[1] = { 1.0 };
    gk::Array<double> result, multipliers;
    if (!system.Reset(1u, error) || !system.BeginRow(1.0e-12, error) || !system.AddCoefficient(0u, 1.0e-12, error) || !system.Solve(start, 1u, result, multipliers, error))
    {
        return Fail("小さい単位の不等式を解けません");
    }
    return fabs(result.At(0u) - 1.0) < 1.0e-10 && fabs(1.0e-12 * multipliers.At(0u) - 1.0) < 1.0e-10 || Fail("行正規化が元のKKT乗数を変えました");
}
/**
 * 行列・勾配の更新が途中で確保に失敗しても、以前の二次目的を使える。
 */
bool TestObjectiveMutationFailuresAreAtomic()
{
    for (uint32_t operation = 0u; operation < 3u; ++operation)
    {
        const uint32_t budgetCount = operation == 1u ? 1u : 2u;
        for (uint32_t budget = 0u; budget < budgetCount; ++budget)
        {
            FSecondaryMotionContactInequalitySystem system;
            gk::String error;
            const double step[2] = { 1.0, 0.0 };
            const double oldDifference[2] = { 2.0, 0.0 };
            const double nextDifference[2] = { 3.0, 0.0 };
            const double oldGradient[2] = { -2.0, 0.0 };
            const double nextGradient[2] = { -7.0, 0.0 };
            const double start[2] = { 0.0, 0.0 };
            if (!system.Reset(2u, error) || !system.SetObjective(oldGradient, 2u, error) || !system.UpdateHessian(step, oldDifference, 2u, error))
            {
                return Fail("目的更新の確保失敗検査を準備できません");
            }
            gk::SetAllocationFailureAfterForTesting(budget);
            const bool changed = operation == 0u ? system.Reset(3u, error) : (operation == 1u ? system.SetObjective(nextGradient, 2u, error) : system.UpdateHessian(step, nextDifference, 2u, error));
            gk::ResetAllocationFailureForTesting();
            if (changed || error.Empty())
            {
                return Fail("目的更新の確保失敗を明示しません");
            }
            gk::Array<double> result, multipliers;
            if (!system.Solve(start, 2u, result, multipliers, error) || fabs(result.At(0u) - 1.0) > 1.0e-10 || fabs(result.At(1u)) > 1.0e-10)
            {
                return Fail("失敗した更新が以前の目的行列または勾配を変えました");
            }
        }
    }
    return true;
}
}
int main()
{
    return TestIllConditionedActiveTangent() && TestScaledRowMultiplier() && TestObjectiveMutationFailuresAreAtomic() && TestUnconstrainedAnalyticSolution() && TestSignedInactiveBounds() && TestActiveRowRemoval() && TestRedundantAndTiedRows() && TestFailureIsAtomic() && TestHessianUpdatesAndClearRows() && TestAllocationFailuresAreAtomic() && TestWorkBounds() ? 0 : 1;
}
