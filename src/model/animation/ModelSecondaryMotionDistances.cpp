// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionDistances.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace gk::model::animation
{
namespace
{

/**
 * 座標が有限でfloat保存可能な範囲にあるか調べる。
 */
bool IsFiniteFloatRange(const gk::FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]) && fabs(value.value[0]) <= FLT_MAX && fabs(value.value[1]) <= FLT_MAX && fabs(value.value[2]) <= FLT_MAX;
}

/**
 * 出力領域どうしのメモリ重複を調べる。
 */
bool Overlaps(const void* first, size_t firstSize, const void* second, size_t secondSize)
{
    // 比較する二つの出力領域の開始アドレス。
    const uintptr_t firstAddress = reinterpret_cast<uintptr_t>(first);
    const uintptr_t secondAddress = reinterpret_cast<uintptr_t>(second);
    return firstAddress <= secondAddress ? secondAddress - firstAddress < firstSize : firstAddress - secondAddress < secondSize;
}

/**
 * XPBD距離制約の柔らかさ係数を計算する。
 */
double ComputeAlpha(double compliance, double stepSeconds)
{
    return compliance == 0.0 ? 0.0 : compliance / (stepSeconds * stepSeconds);
}

/**
 * float範囲内の二点間距離を倍精度で計算する。
 */
double Distance(const gk::FVector3d& first, const gk::FVector3d& second)
{
    // 両端の差を倍精度で取り、float範囲内の大きい位置でも平方を保持する。
    const double x = second.value[0] - first.value[0];
    const double y = second.value[1] - first.value[1];
    const double z = second.value[2] - first.value[2];
    return sqrt(x * x + y * y + z * z);
}

/**
 * 距離制約の係数と許容割合を検証する。
 */
bool ValidateParameters(double restDistance, double compliance, double stepSeconds, double lambda, double toleranceFraction, gk::String& error)
{
    if (!isfinite(restDistance) || !(restDistance > 0.0) || restDistance > FLT_MAX || !isfinite(compliance) || compliance < 0.0 || !isfinite(stepSeconds) || !(stepSeconds > 0.0) || !isfinite(lambda) || !isfinite(toleranceFraction) || toleranceFraction < 0.0 || toleranceFraction > 1.0)
    {
        error.Assign("Secondary motion distance parameters are invalid");
        return false;
    }
    // 秒数に合わせた柔らかさが数値範囲を超えないことを先に確認する。
    const double alpha = ComputeAlpha(compliance, stepSeconds);
    if (!isfinite(alpha))
    {
        error.Assign("Secondary motion distance compliance exceeds numeric range");
        return false;
    }
    return true;
}

}

bool ProjectSecondaryMotionDistance(const gk::FVector3d& referenceFirst, const gk::FVector3d& referenceSecond, double restDistance, double compliance, double stepSeconds, double& lambda, gk::FVector3d& first, gk::FVector3d& second, gk::String& error)
{
    error.Clear();
    if (Overlaps(&first, sizeof(first), &second, sizeof(second)) || Overlaps(&lambda, sizeof(lambda), &first, sizeof(first)) || Overlaps(&lambda, sizeof(lambda), &second, sizeof(second)) || !IsFiniteFloatRange(referenceFirst) || !IsFiniteFloatRange(referenceSecond) || !IsFiniteFloatRange(first) || !IsFiniteFloatRange(second) || !ValidateParameters(restDistance, compliance, stepSeconds, lambda, 0.0, error))
    {
        if (error.Empty())
            error.Assign("Secondary motion distance projection input is invalid");
        return false;
    }
    // 補正前の長さから、伸びと縮みを同じ式で求める。
    const double length = Distance(first, second);
    if (!isfinite(length))
    {
        error.Assign("Secondary motion distance length is invalid");
        return false;
    }
    // 二点が重なる場合は基準姿勢、基準も重なる場合はX方向を使う。
    gk::FVector3d direction{};
    if (length > 0.0)
    {
        direction = { { (second.value[0] - first.value[0]) / length, (second.value[1] - first.value[1]) / length, (second.value[2] - first.value[2]) / length } };
    }
    else
    {
        // 基準姿勢に二点を離す向きが残っているか調べる。
        const double referenceLength = Distance(referenceFirst, referenceSecond);
        if (referenceLength > 0.0 && isfinite(referenceLength))
        {
            direction = { { (referenceSecond.value[0] - referenceFirst.value[0]) / referenceLength, (referenceSecond.value[1] - referenceFirst.value[1]) / referenceLength, (referenceSecond.value[2] - referenceFirst.value[2]) / referenceLength } };
        }
        else
        {
            direction.value[0] = 1.0;
        }
    }
    // 更新秒数に合わせた柔らかさ。
    const double alpha = ComputeAlpha(compliance, stepSeconds);
    // 両端の動きやすさを1ずつとして分ける今回の補正量。
    const double deltaLambda = ((length - restDistance) - alpha * lambda) / (2.0 + alpha);
    // 同じ更新の反復で持ち越す補正の累計。
    const double nextLambda = lambda + deltaLambda;
    // すべての成分を検査するまで呼び出し元の点を変更しない。
    gk::FVector3d nextFirst = first;
    gk::FVector3d nextSecond = second;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        nextFirst.value[axis] += direction.value[axis] * deltaLambda;
        nextSecond.value[axis] -= direction.value[axis] * deltaLambda;
    }
    if (!isfinite(deltaLambda) || !isfinite(nextLambda) || !IsFiniteFloatRange(nextFirst) || !IsFiniteFloatRange(nextSecond))
    {
        error.Assign("Secondary motion distance projection exceeds float range");
        return false;
    }
    first = nextFirst;
    second = nextSecond;
    lambda = nextLambda;
    error.Clear();
    return true;
}

bool CheckSecondaryMotionDistance(double restDistance, double compliance, double stepSeconds, double lambda, const gk::FVector3d& first, const gk::FVector3d& second, gk::String& error, double toleranceFraction)
{
    error.Clear();
    if (!IsFiniteFloatRange(first) || !IsFiniteFloatRange(second) || !ValidateParameters(restDistance, compliance, stepSeconds, lambda, toleranceFraction, error))
    {
        if (error.Empty())
            error.Assign("Secondary motion distance check input is invalid");
        return false;
    }
    // 点位置から取り直す、候補または描画姿勢の実距離。
    const double length = Distance(first, second);
    // 更新秒数に合わせた柔らかさ。
    const double alpha = ComputeAlpha(compliance, stepSeconds);
    // 伸びを許す補正累計も含めて調べる制約誤差。
    const double residual = length - restDistance - alpha * lambda;
    // 基準距離に対する相対誤差。候補側はFK丸め用の余白を残す。
    const double tolerance = restDistance * 0.001 * toleranceFraction;
    if (!isfinite(length) || !isfinite(residual) || !isfinite(tolerance) || fabs(residual) > tolerance)
    {
        error.Assign("Secondary motion distance constraint is not satisfied");
        return false;
    }
    return true;
}

}
