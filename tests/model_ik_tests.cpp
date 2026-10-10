// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelIk.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>

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
 * 2本の単位長boneを持つ簡単な階層を構築する。
 */
bool MakeChain(FModelSkeleton& skeleton)
{
    const int32_t parents[3] = { -1, 0, 1 };
    FModelBoneTransform transforms[3]{};
    transforms[1].position[0] = 1.0f;
    transforms[2].position[0] = 1.0f;
    return skeleton.parents.AppendRange(parents, 3) && skeleton.restLocalTransforms.AppendRange(transforms, 3);
}

/**
 * rest姿勢をIK入力へ複製する。
 */
bool MakePose(FModelPose& pose)
{
    FModelBoneTransform transforms[3]{};
    transforms[1].position[0] = 1.0f;
    transforms[2].position[0] = 1.0f;
    return pose.localTransforms.AppendRange(transforms, 3);
}

/**
 * end boneのmodel-space位置をFKで取得する。
 */
bool EndPosition(const FModelSkeleton& skeleton, const FModelPose& pose, float output[3])
{
    gk::Array<float> matrices;
    gk::String error;
    if (!EvaluateModelPose(skeleton, pose, matrices, error) || matrices.Count() != 48)
    {
        return false;
    }
    for (uint32_t index = 0; index < matrices.Count(); ++index)
        if (!isfinite(matrices.At(index)))
        {
            return false;
        }
    output[0] = matrices.At(44);
    output[1] = matrices.At(45);
    output[2] = matrices.At(46);
    return true;
}

/**
 * 単位長を保ちながら目標へ到達するか調べる。
 */
bool CheckSolvedChain(const FModelSkeleton& skeleton, const FModelPose& pose, const float target[3], float tolerance)
{
    gk::Array<float> matrices;
    gk::String error;
    if (!EvaluateModelPose(skeleton, pose, matrices, error) || matrices.Count() != 48)
    {
        return false;
    }
    // FK出力からchainの3点を取り出す。
    const float root[3] = { matrices.At(12), matrices.At(13), matrices.At(14) };
    const float middle[3] = { matrices.At(28), matrices.At(29), matrices.At(30) };
    const float end[3] = { matrices.At(44), matrices.At(45), matrices.At(46) };
    // 倍精度距離で長さ比と到達誤差を測る。
    double firstSquared = 0.0, secondSquared = 0.0, targetSquared = 0.0;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        // 各節と目標への差分を求める。
        const double firstDelta = static_cast<double>(middle[axis]) - root[axis];
        const double secondDelta = static_cast<double>(end[axis]) - middle[axis];
        const double targetDelta = static_cast<double>(end[axis]) - target[axis];
        firstSquared += firstDelta * firstDelta;
        secondSquared += secondDelta * secondDelta;
        targetSquared += targetDelta * targetDelta;
    }
    // FKで測った長さとrest poseの期待長。
    const double firstLength = sqrt(firstSquared), secondLength = sqrt(secondSquared), distance = sqrt(targetSquared);
    const double expectedFirst = sqrt(static_cast<double>(skeleton.restLocalTransforms.At(1).position[0]) * skeleton.restLocalTransforms.At(1).position[0] + static_cast<double>(skeleton.restLocalTransforms.At(1).position[1]) * skeleton.restLocalTransforms.At(1).position[1] + static_cast<double>(skeleton.restLocalTransforms.At(1).position[2]) * skeleton.restLocalTransforms.At(1).position[2]);
    const double expectedSecond = sqrt(static_cast<double>(skeleton.restLocalTransforms.At(2).position[0]) * skeleton.restLocalTransforms.At(2).position[0] + static_cast<double>(skeleton.restLocalTransforms.At(2).position[1]) * skeleton.restLocalTransforms.At(2).position[1] + static_cast<double>(skeleton.restLocalTransforms.At(2).position[2]) * skeleton.restLocalTransforms.At(2).position[2]);
    return fabs(firstLength - expectedFirst) <= tolerance * expectedFirst + 1.0e-30 && fabs(secondLength - expectedSecond) <= tolerance * expectedSecond + 1.0e-30 && distance <= tolerance * (firstLength + secondLength) + 1.0e-30;
}

/**
 * float丸め範囲のほぼ一様なscaleで、端点と各節長を保つ。
 */
bool CheckNearlyUniformScaleResult(const FModelSkeleton& skeleton, const FModelPose& pose, const float target[3], double expectedFirst, double expectedSecond)
{
    gk::Array<float> matrices;
    gk::String error;
    if (!EvaluateModelPose(skeleton, pose, matrices, error) || matrices.Count() != 48)
    {
        return false;
    }
    // 位置だけでなく姿勢行列の全成分が有限であることを確認する。
    for (uint32_t index = 0; index < matrices.Count(); ++index)
    {
        if (!isfinite(matrices.At(index)))
        {
            return false;
        }
    }
    // FK結果からroot、中間、endの位置を取り出す。
    const float root[3] = { matrices.At(12), matrices.At(13), matrices.At(14) };
    const float middle[3] = { matrices.At(28), matrices.At(29), matrices.At(30) };
    const float end[3] = { matrices.At(44), matrices.At(45), matrices.At(46) };
    double firstSquared = 0.0, secondSquared = 0.0;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        const double firstDelta = static_cast<double>(middle[axis]) - root[axis];
        const double secondDelta = static_cast<double>(end[axis]) - middle[axis];
        firstSquared += firstDelta * firstDelta;
        secondSquared += secondDelta * secondDelta;
        if (!isfinite(end[axis]) || fabsf(end[axis] - target[axis]) > 2.0e-5f)
        {
            return false;
        }
    }
    // 中間boneのX scaleは子boneの長さに反映される。
    return fabs(sqrt(firstSquared) - expectedFirst) <= 2.0e-5 && fabs(sqrt(secondSquared) - expectedSecond) <= 2.0e-5;
}

/**
 * float丸め誤差内のscale差を持つchainを3種類のsolverで解く。
 */
bool TestNearlyUniformScaleCase(float rootScale, float middleScale, double expectedFirst, double expectedSecond)
{
    FModelSkeleton skeleton;
    FModelPose source, twoBone, fabrik, ccd;
    gk::String error;
    const float target[3] = { 1.0f, 1.0f, 0.0f };
    const float pole[3] = { 0.0f, 1.0f, 0.0f };
    const uint32_t chain[3] = { 0, 1, 2 };
    if (!MakeChain(skeleton) || !MakePose(source))
    {
        return Fail("nearly uniform-scale fixture allocation failed");
    }
    source.localTransforms.At(0).scale[0] = rootScale;
    source.localTransforms.At(1).scale[0] = middleScale;
    if (!SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 1.0f, twoBone, error) || !SolveFabrikIk(skeleton, source, chain, 3, target, 1.0f, 1.0e-6f, 256, fabrik, error) || !SolveCcdIk(skeleton, source, chain, 3, target, 1.0f, 1.0e-6f, 256, ccd, error))
    {
        return Fail(error.CStr());
    }
    if (!CheckNearlyUniformScaleResult(skeleton, twoBone, target, expectedFirst, expectedSecond) || !CheckNearlyUniformScaleResult(skeleton, fabrik, target, expectedFirst, expectedSecond) || !CheckNearlyUniformScaleResult(skeleton, ccd, target, expectedFirst, expectedSecond))
    {
        return Fail("nearly uniform-scale IK changed bone lengths, produced non-finite output, or missed its target");
    }
    return true;
}

/**
 * quaternionの符号違いを許して同じ回転か比較する。
 */
bool SameRotation(const float first[4], const float second[4], double tolerance)
{
    double dot = 0.0;
    for (uint32_t component = 0; component < 4; ++component)
    {
        dot += static_cast<double>(first[component]) * second[component];
    }
    return fabs(fabs(dot) - 1.0) <= tolerance;
}

/**
 * 実モデルで観測したscale差と、rootから積み上がる同じ差を確認する。
 */
bool TestNearlyUniformScaleRounding()
{
    const float scale = 0.999998987f;
    const double expectedSecond = static_cast<double>(scale) * scale;
    return TestNearlyUniformScaleCase(1.0f, scale, 1.0, scale) && TestNearlyUniformScaleCase(scale, scale, scale, expectedSecond);
}

/**
 * 長さ比、root姿勢、親移動、反平行目標を保って解く。
 */
bool TestScaledAndRotatedChains()
{
    // 固定単位誤差に依存しないことを調べる両端の骨長。
    const float sizes[2] = { 1.0e-20f, 1.0e20f };
    for (float unit : sizes)
    {
        // root回転・移動と異なる2節長を持つchain。
        FModelSkeleton skeleton;
        FModelPose source, output;
        gk::String error;
        // 各節の親番号とrest変換。
        const int32_t parents[3] = { -1, 0, 1 };
        FModelBoneTransform transforms[3]{};
        transforms[0].position[0] = 3.0f * unit;
        transforms[0].position[1] = 4.0f * unit;
        // rootに90度のZ回転を設定する。
        const float halfRoot = static_cast<float>(sqrt(0.5));
        transforms[0].rotation[2] = halfRoot;
        transforms[0].rotation[3] = halfRoot;
        transforms[1].position[0] = 2.0f * unit;
        transforms[2].position[0] = 0.5f * unit;
        // root位置から測った到達可能な目標とpole。
        const float target[3] = { 3.0f * unit + 0.9f * unit, 4.0f * unit + 1.5f * unit, 0.0f };
        const float pole[3] = { 3.0f * unit, 5.0f * unit, 0.0f };
        if (!skeleton.parents.AppendRange(parents, 3) || !skeleton.restLocalTransforms.AppendRange(transforms, 3) || !source.localTransforms.AppendRange(transforms, 3))
        {
            return Fail("scaled rotated-chain fixture allocation failed");
        }
        if (!SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 1.0f, output, error))
        {
            return Fail(error.CStr());
        }
        if (!CheckSolvedChain(skeleton, output, target, 0.003f))
        {
            return Fail("scaled or rotated unequal-length chain missed its target or changed bone lengths");
        }
    }
    // 最大到達距離の反平行目標。
    FModelSkeleton antiParallelSkeleton;
    FModelPose antiParallelSource, antiParallelOutput;
    gk::String error;
    if (!MakeChain(antiParallelSkeleton) || !MakePose(antiParallelSource))
    {
        return Fail("anti-parallel chain fixture allocation failed");
    }
    // 目標はchainの逆方向、poleは直交方向。
    const float antiParallelTarget[3] = { -4.0f, 0.0f, 0.0f };
    const float antiParallelPole[3] = { 0.0f, 1.0f, 0.0f };
    if (!SolveTwoBoneIk(antiParallelSkeleton, antiParallelSource, 0, 1, 2, antiParallelTarget, antiParallelPole, 1.0f, antiParallelOutput, error))
    {
        return Fail(error.CStr());
    }
    // 解いた先端位置。
    float end[3]{};
    if (!EndPosition(antiParallelSkeleton, antiParallelOutput, end) || fabsf(end[0] + 2.0f) > 0.002f || fabsf(end[1]) > 0.002f)
    {
        return Fail("anti-parallel target did not preserve maximum reach");
    }
    return true;
}

/**
 * chainと平行なpoleでも毎回同じ安定方向へ曲げる。
 */
bool TestCollinearPoleFallback()
{
    // 直線chain上にtargetとpoleを置く。
    FModelSkeleton skeleton;
    FModelPose source, first, second;
    gk::String error;
    // 目標方向と平行なpoleは固定軸fallbackを要求する。
    const float target[3] = { 1.0f, 0.0f, 0.0f };
    const float pole[3] = { 8.0f, 0.0f, 0.0f };
    if (!MakeChain(skeleton) || !MakePose(source) || !SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 1.0f, first, error) || !SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 1.0f, second, error))
    {
        return Fail(error.CStr());
    }
    if (first.localTransforms.Count() != second.localTransforms.Count())
    {
        return Fail("collinear-pole fallback changed output size");
    }
    for (uint32_t bone = 0; bone < first.localTransforms.Count(); ++bone)
        for (uint32_t component = 0; component < 4; ++component)
            if (first.localTransforms.At(bone).rotation[component] != second.localTransforms.At(bone).rotation[component])
            {
                return Fail("collinear-pole fallback was not deterministic");
            }
    // 出力の先端と中間jointを検査する。
    float end[3]{};
    gk::Array<float> matrices;
    if (!EndPosition(skeleton, first, end) || !EvaluateModelPose(skeleton, first, matrices, error) || fabsf(end[0] - target[0]) > 0.002f || fabsf(end[1] - target[1]) > 0.002f || fabsf(matrices.At(30)) < 0.1f)
    {
        return Fail("collinear pole did not produce a stable bent pose");
    }
    return true;
}

/**
 * 平行移動したrootからモデル空間targetへ解く。
 */
bool TestTranslatedRootTarget()
{
    FModelSkeleton skeleton;
    FModelPose source, output;
    gk::String error;
    if (!MakeChain(skeleton) || !MakePose(source))
    {
        return Fail("translated-root fixture allocation failed");
    }
    source.localTransforms.At(0).position[0] = 1.0f;
    const float target[3] = { 0.0f, 1.0f, 0.0f };
    const float pole[3] = { 0.0f, 1.0f, 0.0f };
    if (!SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 1.0f, output, error))
    {
        return Fail(error.CStr());
    }
    float end[3]{};
    if (!EndPosition(skeleton, output, end) || fabsf(end[0] - target[0]) > 0.002f || fabsf(end[1] - target[1]) > 0.002f)
    {
        fprintf(stderr, "translated-root IK result was (%.3f,%.3f)\n", end[0], end[1]);
        return Fail("translated-root IK did not reach its model-space target");
    }
    return true;
}

/**
 * 回転・平行移動と不均衡なリンクでも元のchain回転を保つ。
 */
bool TestUnchangedTargetPreservesRotatedUnequalChain()
{
    // 任意軸のroot回転、平行移動、不均衡リンクを持つsource姿勢。
    FModelSkeleton skeleton;
    FModelPose source;
    FModelPose solved;
    gk::String error;
    const int32_t parents[3] = { -1, 0, 1 };
    FModelBoneTransform transforms[3]{};
    const double axisLength = sqrt(14.0);
    const double rootSine = sin(0.35) / axisLength;
    transforms[0].position[0] = 2.0f;
    transforms[0].position[1] = -3.0f;
    transforms[0].position[2] = 4.0f;
    transforms[0].rotation[0] = static_cast<float>(rootSine);
    transforms[0].rotation[1] = static_cast<float>(2.0 * rootSine);
    transforms[0].rotation[2] = static_cast<float>(3.0 * rootSine);
    transforms[0].rotation[3] = static_cast<float>(cos(0.35));
    transforms[1].position[0] = 1.5f;
    transforms[1].rotation[2] = sinf(0.65f * 0.5f);
    transforms[1].rotation[3] = cosf(0.65f * 0.5f);
    transforms[2].position[0] = 0.7f;
    transforms[2].rotation[0] = sinf(0.22f * 0.5f);
    transforms[2].rotation[3] = cosf(0.22f * 0.5f);
    if (!skeleton.parents.AppendRange(parents, 3) || !skeleton.restLocalTransforms.AppendRange(transforms, 3) || !source.localTransforms.AppendRange(transforms, 3))
    {
        return Fail("rotated unequal-chain fixture allocation failed");
    }

    // 元の3点位置と曲げ面からpoleを作る。
    gk::Array<float> sourceMatrices;
    if (!EvaluateModelPose(skeleton, source, sourceMatrices, error) || sourceMatrices.Count() != 48)
    {
        return Fail(error.CStr());
    }
    const double root[3] = { sourceMatrices.At(12), sourceMatrices.At(13), sourceMatrices.At(14) };
    const double middle[3] = { sourceMatrices.At(28), sourceMatrices.At(29), sourceMatrices.At(30) };
    const double end[3] = { sourceMatrices.At(44), sourceMatrices.At(45), sourceMatrices.At(46) };
    const double first[3] = { middle[0] - root[0], middle[1] - root[1], middle[2] - root[2] };
    const double second[3] = { end[0] - middle[0], end[1] - middle[1], end[2] - middle[2] };
    const double normal[3] = { first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2], first[0] * second[1] - first[1] * second[0] };
    const double targetDirection[3] = { end[0] - root[0], end[1] - root[1], end[2] - root[2] };
    const double targetLength = sqrt(targetDirection[0] * targetDirection[0] + targetDirection[1] * targetDirection[1] + targetDirection[2] * targetDirection[2]);
    double direction[3] = { targetDirection[0] / targetLength, targetDirection[1] / targetLength, targetDirection[2] / targetLength };
    const double normalProjection = normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2];
    double planeNormal[3] = { normal[0] - normalProjection * direction[0], normal[1] - normalProjection * direction[1], normal[2] - normalProjection * direction[2] };
    const double planeLength = sqrt(planeNormal[0] * planeNormal[0] + planeNormal[1] * planeNormal[1] + planeNormal[2] * planeNormal[2]);
    if (!(planeLength > 0.0))
    {
        return Fail("rotated unequal-chain bend plane was degenerate");
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        planeNormal[axis] /= planeLength;
    }
    const double bend[3] = { direction[1] * planeNormal[2] - direction[2] * planeNormal[1], direction[2] * planeNormal[0] - direction[0] * planeNormal[2], direction[0] * planeNormal[1] - direction[1] * planeNormal[0] };
    const float target[3] = { static_cast<float>(end[0]), static_cast<float>(end[1]), static_cast<float>(end[2]) };
    const float pole[3] = { static_cast<float>(root[0] + 2.2 * bend[0]), static_cast<float>(root[1] + 2.2 * bend[1]), static_cast<float>(root[2] + 2.2 * bend[2]) };
    if (!SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 1.0f, solved, error))
    {
        return Fail(error.CStr());
    }

    // root、hinge、endすべての回転を符号違いを許して照合する。
    for (uint32_t bone = 0; bone < 3; ++bone)
    {
        if (!SameRotation(source.localTransforms.At(bone).rotation, solved.localTransforms.At(bone).rotation, 2.0e-5))
        {
            return Fail("unchanged target or source pole altered a rotated unequal-chain quaternion");
        }
    }
    return true;
}

/**
 * poleで曲げる2-bone solverとweight 0の不変動作を確認する。
 */
bool TestTwoBonePoleAndWeight()
{
    FModelSkeleton skeleton;
    FModelPose source;
    FModelPose output;
    gk::String error;
    const float target[3] = { 1.0f, 1.0f, 0.0f };
    const float pole[3] = { 0.0f, 1.0f, 0.0f };
    if (!MakeChain(skeleton) || !MakePose(source))
    {
        return Fail("two-bone fixture allocation failed");
    }
    if (!SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 1.0f, output, error))
    {
        return Fail(error.CStr());
    }
    if (!CheckSolvedChain(skeleton, output, target, 0.002f))
    {
        return Fail("two-bone IK missed a reachable target or changed bone lengths");
    }
    FModelPose unchanged;
    if (!SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 0.0f, unchanged, error))
    {
        return Fail(error.CStr());
    }
    if (unchanged.localTransforms.Count() != source.localTransforms.Count() || unchanged.localTransforms.At(0).rotation[3] != source.localTransforms.At(0).rotation[3] || unchanged.localTransforms.At(1).rotation[3] != source.localTransforms.At(1).rotation[3])
    {
        return Fail("zero IK weight changed the source pose");
    }
    return true;
}

/**
 * 到達不能targetを安全に伸ばし、共通solverで解けるchainを確認する。
 */
bool TestUnreachableTargetAndChainSolvers()
{
    FModelSkeleton skeleton;
    FModelPose source;
    FModelPose twoBone;
    FModelPose fabrik;
    FModelPose ccd;
    gk::String error;
    const float target[3] = { 4.0f, 0.0f, 0.0f };
    const float pole[3] = { 0.0f, 1.0f, 0.0f };
    const uint32_t chain[3] = { 0, 1, 2 };
    if (!MakeChain(skeleton) || !MakePose(source))
    {
        return Fail("chain IK fixture allocation failed");
    }
    if (!SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 1.0f, twoBone, error) || !SolveFabrikIk(skeleton, source, chain, 3, target, 1.0f, 0.0001f, 16, fabrik, error) || !SolveCcdIk(skeleton, source, chain, 3, target, 1.0f, 0.0001f, 16, ccd, error))
    {
        return Fail(error.CStr());
    }
    float point[3]{};
    if (!EndPosition(skeleton, twoBone, point) || fabsf(point[0] - 2.0f) > 0.002f || fabsf(point[1]) > 0.002f || !EndPosition(skeleton, fabrik, point) || fabsf(point[0] - 2.0f) > 0.002f || fabsf(point[1]) > 0.002f || !EndPosition(skeleton, ccd, point) || fabsf(point[0] - 2.0f) > 0.002f || fabsf(point[1]) > 0.002f)
    {
        return Fail("unreachable IK target did not clamp to maximum chain reach");
    }
    return true;
}

/**
 * 零長、非有限target、異常scaleとchainを拒否して出力を保つ。
 */
bool TestInvalidIkInputsAreAtomic()
{
    FModelSkeleton skeleton;
    FModelPose source;
    FModelPose output;
    gk::String error;
    const float target[3] = { 0.0f, 1.0f, 0.0f };
    const float pole[3] = { 0.0f, 0.0f, 1.0f };
    const uint32_t chain[3] = { 0, 1, 2 };
    if (!MakeChain(skeleton) || !MakePose(source) || !MakePose(output))
    {
        return Fail("invalid IK fixture allocation failed");
    }
    output.localTransforms.At(0).position[0] = 9.0f;
    FModelPose zeroLength;
    FModelPose singularScale;
    if (!MakePose(zeroLength) || !MakePose(singularScale))
    {
        return Fail("invalid IK variants allocation failed");
    }
    zeroLength.localTransforms.At(1).position[0] = 0.0f;
    if (SolveTwoBoneIk(skeleton, zeroLength, 0, 1, 2, target, pole, 1.0f, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
    {
        return Fail("zero-length IK bone was accepted or changed output");
    }
    singularScale.localTransforms.At(0).scale[0] = 0.0f;
    error.Clear();
    if (SolveFabrikIk(skeleton, singularScale, chain, 3, target, 1.0f, 0.001f, 8, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
    {
        return Fail("singular IK scale changed output or omitted its diagnostic");
    }

    const float nonFiniteTarget[3] = { NAN, 1.0f, 0.0f };
    error.Clear();
    if (SolveTwoBoneIk(skeleton, source, 0, 1, 2, nonFiniteTarget, pole, 1.0f, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
    {
        return Fail("non-finite IK target changed output or omitted its diagnostic");
    }

    const uint32_t disconnectedChain[3] = { 0, 2, 1 };
    error.Clear();
    if (SolveFabrikIk(skeleton, source, disconnectedChain, 3, target, 1.0f, 0.001f, 8, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
    {
        return Fail("disconnected IK chain changed output or omitted its diagnostic");
    }

    FModelPose negativeScale;
    FModelPose nonUniformScale;
    FModelPose outsideRoundingBudget;
    if (!MakePose(negativeScale) || !MakePose(nonUniformScale) || !MakePose(outsideRoundingBudget))
    {
        return Fail("scale validation fixture allocation failed");
    }
    negativeScale.localTransforms.At(0).scale[0] = -1.0f;
    nonUniformScale.localTransforms.At(0).scale[1] = 2.0f;
    outsideRoundingBudget.localTransforms.At(1).scale[0] = 0.999992f;
    error.Clear();
    if (SolveCcdIk(skeleton, negativeScale, chain, 3, target, 1.0f, 0.001f, 8, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
    {
        return Fail("negative IK scale changed output or omitted its diagnostic");
    }
    error.Clear();
    if (SolveCcdIk(skeleton, nonUniformScale, chain, 3, target, 1.0f, 0.001f, 8, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
    {
        return Fail("non-uniform IK scale changed output or omitted its diagnostic");
    }
    error.Clear();
    if (SolveTwoBoneIk(skeleton, outsideRoundingBudget, 0, 1, 2, target, pole, 1.0f, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
    {
        return Fail("scale outside the float-rounding budget changed output or was accepted");
    }
    error.Clear();
    if (SolveFabrikIk(skeleton, outsideRoundingBudget, chain, 3, target, 1.0f, 0.001f, 8, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
    {
        return Fail("FABRIK accepted scale outside the float-rounding budget");
    }
    error.Clear();
    if (SolveCcdIk(skeleton, outsideRoundingBudget, chain, 3, target, 1.0f, 0.001f, 8, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
    {
        return Fail("CCD accepted scale outside the float-rounding budget");
    }
    return true;
}

/**
 * 非自明な親回転と分岐を持つ、指定骨数の2-bone fixtureを作る。
 */
bool MakeWideFixture(uint32_t boneCount, bool reverseChainSigns, FModelSkeleton& skeleton, FModelPose& pose)
{
    for (uint32_t bone = 0; bone < boneCount; ++bone)
    {
        // root、middle、end以外はroot直下の分岐として配置する。
        const int32_t parent = bone == 0 ? -1 : bone == 1 ? 0 : bone == 2 ? 1 : 0;
        FModelBoneTransform transform{};
        transform.position[0] = bone == 0 ? 1.25f : bone == 1 ? 0.9f : bone == 2 ? 0.75f : 0.001f * static_cast<float>(bone % 17u);
        transform.position[1] = bone == 0 ? -0.75f : bone == 1 ? 0.16f : bone == 2 ? -0.11f : 0.002f * static_cast<float>(bone % 13u);
        transform.position[2] = bone == 0 ? 0.4f : bone == 1 ? 0.08f : bone == 2 ? 0.13f : -0.001f * static_cast<float>(bone % 11u);
        if (bone == 0)
        {
            transform.rotation[0] = 0.13f;
            transform.rotation[1] = -0.18f;
            transform.rotation[2] = 0.09f;
            transform.rotation[3] = 0.97f;
        }
        else if (bone == 1)
        {
            transform.rotation[0] = 0.05f;
            transform.rotation[1] = 0.12f;
            transform.rotation[2] = -0.08f;
            transform.rotation[3] = 0.98f;
        }
        else if (bone == 2)
        {
            transform.rotation[0] = -0.07f;
            transform.rotation[1] = 0.03f;
            transform.rotation[2] = 0.15f;
            transform.rotation[3] = 0.98f;
        }
        else
        {
            transform.rotation[0] = 0.01f * static_cast<float>(bone % 7u + 1u);
            transform.rotation[1] = -0.012f * static_cast<float>(bone % 5u + 1u);
            transform.rotation[2] = 0.008f * static_cast<float>(bone % 9u + 1u);
            transform.rotation[3] = 1.0f;
        }
        const double length = sqrt(static_cast<double>(transform.rotation[0]) * transform.rotation[0] + static_cast<double>(transform.rotation[1]) * transform.rotation[1] + static_cast<double>(transform.rotation[2]) * transform.rotation[2] + static_cast<double>(transform.rotation[3]) * transform.rotation[3]);
        for (uint32_t component = 0; component < 4; ++component)
        {
            transform.rotation[component] = static_cast<float>(transform.rotation[component] / length);
            if (reverseChainSigns && bone < 3)
                transform.rotation[component] = -transform.rotation[component];
        }
        if (!skeleton.parents.Append(parent) || !skeleton.restLocalTransforms.Append(transform) || !pose.localTransforms.Append(transform))
            return false;
    }
    return true;
}

/**
 * poseの全変換とmorph値を別配列へ複製する。
 */
bool ClonePose(const FModelPose& source, FModelPose& output)
{
    return output.localTransforms.AppendRange(source.localTransforms.Data(), source.localTransforms.Count()) && output.morphWeights.AppendRange(source.morphWeights.Data(), source.morphWeights.Count());
}

/**
 * すべてのpose値が成分ごとに一致するか調べる。
 */
bool SamePose(const FModelPose& first, const FModelPose& second)
{
    if (first.localTransforms.Count() != second.localTransforms.Count() || first.morphWeights.Count() != second.morphWeights.Count())
        return false;
    for (uint32_t bone = 0; bone < first.localTransforms.Count(); ++bone)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
            if (first.localTransforms.At(bone).position[axis] != second.localTransforms.At(bone).position[axis] || first.localTransforms.At(bone).scale[axis] != second.localTransforms.At(bone).scale[axis])
                return false;
        for (uint32_t component = 0; component < 4; ++component)
            if (first.localTransforms.At(bone).rotation[component] != second.localTransforms.At(bone).rotation[component])
                return false;
    }
    for (uint32_t morph = 0; morph < first.morphWeights.Count(); ++morph)
        if (first.morphWeights.At(morph) != second.morphWeights.At(morph))
            return false;
    return true;
}

/**
 * 同じ3-bone鎖を持つ縮約版と339骨版でsigned quaternion結果を比較する。
 */
bool TestWideTwoBoneResultsMatchReducedFixture()
{
    const float weights[3] = { 0.0f, 1.0f, 0.37f };
    for (uint32_t signCase = 0; signCase < 2; ++signCase)
    {
        for (float weight : weights)
        {
            FModelSkeleton reducedSkeleton, wideSkeleton;
            FModelPose reducedSource, wideSource, reducedOutput, wideOutput;
            gk::String error;
            if (!MakeWideFixture(3, signCase != 0, reducedSkeleton, reducedSource) || !MakeWideFixture(339, signCase != 0, wideSkeleton, wideSource))
                return Fail("wide IK fixture allocation failed");
            const float target[3] = { 2.05f, 0.0f, 0.55f };
            const float pole[3] = { 1.25f, 0.1f, 1.4f };
            if (!SolveTwoBoneIk(reducedSkeleton, reducedSource, 0, 1, 2, target, pole, weight, reducedOutput, error))
                return Fail(error.CStr());
            ResetModelIkWorkForTesting();
            if (!SolveTwoBoneIk(wideSkeleton, wideSource, 0, 1, 2, target, pole, weight, wideOutput, error))
                return Fail(error.CStr());
            if (GetModelIkWorldRotationCountForTesting() > 12u)
                return Fail("two-bone IK accumulated world rotations outside the requested ancestor paths");
            for (uint32_t bone = 0; bone < 3; ++bone)
            {
                for (uint32_t component = 0; component < 4; ++component)
                {
                    if (reducedOutput.localTransforms.At(bone).rotation[component] != wideOutput.localTransforms.At(bone).rotation[component])
                        return Fail("wide two-bone IK changed a signed chain quaternion component");
                }
            }
        }
    }
    return true;
}

/**
 * 全骨入力検証の失敗とsource/output alias時のpose原子性を保つ。
 */
bool TestWideTwoBoneValidationAndAliasing()
{
    FModelSkeleton skeleton;
    FModelPose source, output, expectedOutput;
    gk::String error;
    if (!MakeWideFixture(339, false, skeleton, source) || !ClonePose(source, output))
        return Fail("wide IK validation fixture allocation failed");
    output.localTransforms.At(338).position[0] = 123.0f;
    if (!ClonePose(output, expectedOutput))
        return Fail("wide IK output snapshot allocation failed");
    const float target[3] = { 2.05f, 0.0f, 0.55f };
    const float pole[3] = { 1.25f, 0.1f, 1.4f };
    FModelPose invalidSource;
    if (!ClonePose(source, invalidSource))
        return Fail("wide IK invalid-pose allocation failed");
    invalidSource.localTransforms.At(338).rotation[0] = NAN;
    if (SolveTwoBoneIk(skeleton, invalidSource, 0, 1, 2, target, pole, 0.6f, output, error) || error.Empty() || !SamePose(output, expectedOutput))
        return Fail("non-finite unrelated bone was accepted or changed the output pose");

    error.Clear();
    skeleton.parents.At(338) = 338;
    if (SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 0.6f, output, error) || error.Empty() || !SamePose(output, expectedOutput))
        return Fail("invalid unrelated parent was accepted or changed the output pose");
    skeleton.parents.At(338) = 0;

    FModelPose separateOutput, aliasedPose;
    if (!SolveTwoBoneIk(skeleton, source, 0, 1, 2, target, pole, 0.6f, separateOutput, error) || !ClonePose(source, aliasedPose))
        return Fail(error.Empty() ? "wide IK alias fixture allocation failed" : error.CStr());
    if (!SolveTwoBoneIk(skeleton, aliasedPose, 0, 1, 2, target, pole, 0.6f, aliasedPose, error) || !SamePose(separateOutput, aliasedPose))
        return Fail("source/output alias changed the two-bone IK result");
    return true;
}

/**
 * 揺れた節の位置へ回転だけで合わせ、末端の先と失敗時の出力を確認する。
 */
bool TestSecondaryChainOrientation()
{
    FModelSkeleton skeleton;
    FModelPose source, output, saved;
    gk::String error;
    const uint32_t bones[3] = { 0, 1, 2 };
    const gk::Vec3 targets[4] = { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 2, 0 } };
    ResetModelIkWorkForTesting();
    if (!MakeChain(skeleton) || !MakePose(source) || !OrientModelPoseChain(skeleton, source, bones, 3, { 1, 0, 0 }, targets, output, error))
    {
        return Fail(error.CStr());
    }
    if (GetModelIkWorldRotationCountForTesting() > 2)
    {
        return Fail("secondary orientation recomputed the same chain ancestors");
    }
    gk::Array<float> matrices;
    if (!EvaluateModelPose(skeleton, output, matrices, error))
    {
        return Fail(error.CStr());
    }
    for (uint32_t bone = 0; bone < 3; ++bone)
    {
        const uint32_t offset = bone * 16u;
        if (fabsf(matrices.At(offset + 12) - targets[bone].x) > 0.0001f || fabsf(matrices.At(offset + 13) - targets[bone].y) > 0.0001f || fabsf(matrices.At(offset + 14) - targets[bone].z) > 0.0001f)
        {
            return Fail("secondary chain did not reach a simulated joint");
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (output.localTransforms.At(bone).position[axis] != source.localTransforms.At(bone).position[axis] || output.localTransforms.At(bone).scale[axis] != source.localTransforms.At(bone).scale[axis])
            {
                return Fail("secondary orientation changed local position or scale");
            }
        }
    }
    if (fabsf(matrices.At(32) + matrices.At(44) - targets[3].x) > 0.0001f || fabsf(matrices.At(33) + matrices.At(45) - targets[3].y) > 0.0001f || !ClonePose(output, saved))
    {
        return Fail("secondary dummy tip did not follow the last bone");
    }
    // 実データの小さなscale軸差でも、節位置の誤差が鎖長の0.01%以内に収まる。
    source.localTransforms.At(0).scale[0] = 0.999999881f;
    source.localTransforms.At(0).scale[1] = 0.99998951f;
    source.localTransforms.At(0).scale[2] = 0.99998951f;
    if (!OrientModelPoseChain(skeleton, source, bones, 3, { 1, 0, 0 }, targets, output, error) || !EvaluateModelPose(skeleton, output, matrices, error))
    {
        return Fail("secondary motion rejected a small authored scale-axis difference");
    }
    for (uint32_t bone = 0; bone < 3; ++bone)
    {
        if (fabsf(matrices.At(bone * 16u + 12u) - targets[bone].x) > 0.0001f || fabsf(matrices.At(bone * 16u + 13u) - targets[bone].y) > 0.0001f)
        {
            return Fail("secondary motion exceeded the small-scale position tolerance");
        }
    }
    saved.localTransforms.Clear();
    saved.morphWeights.Clear();
    if (!ClonePose(output, saved))
    {
        return Fail("secondary scale output snapshot allocation failed");
    }
    source.localTransforms.At(0).scale[1] = 0.95f;
    if (OrientModelPoseChain(skeleton, source, bones, 3, { 1, 0, 0 }, targets, output, error) || !SamePose(output, saved))
    {
        return Fail("secondary motion accepted an unsupported axis scale or changed output");
    }
    source.localTransforms.At(0).scale[1] = 0.99998951f;
    const uint32_t invalid[2] = { 0, 2 };
    if (OrientModelPoseChain(skeleton, source, invalid, 2, { 1, 0, 0 }, targets, output, error) || !SamePose(output, saved))
    {
        return Fail("invalid secondary chain changed the output pose");
    }
    return true;
}

}

int main()
{
    return TestSecondaryChainOrientation() && TestTwoBonePoleAndWeight() && TestUnreachableTargetAndChainSolvers() && TestInvalidIkInputsAreAtomic() && TestNearlyUniformScaleRounding() && TestScaledAndRotatedChains() && TestCollinearPoleFallback() && TestTranslatedRootTarget() && TestUnchangedTargetPreservesRotatedUnequalChain() && TestWideTwoBoneResultsMatchReducedFixture() && TestWideTwoBoneValidationAndAliasing() ? 0 : 1;
}
