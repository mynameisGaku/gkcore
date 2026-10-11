// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FPREPAREDSECONDARYMOTIONCOLLISIONSHAPE_H
#define GKCORE_MODEL_ANIMATION_FPREPAREDSECONDARYMOTIONCOLLISIONSHAPE_H

#include "foundation/FVector3d.h"

/**
 * 揺れもの接触計算で再利用する準備済み形状と、その生成契約を定義する。
 */
namespace gk::model::animation
{

/**
 * 接触計算向けに準備した身体形状。接触形状の準備関数が生成した値だけを使う。
 */
struct FPreparedSecondaryMotionCollisionShape
{
    gk::FVector3d start;      // 形状軸の始点。
    gk::FVector3d end;        // 形状軸の終点。
    gk::FVector3d axis;       // 始点から終点へ向かう軸。
    double axisLengthSquared; // 最近点計算に使う軸長の二乗。
    double radius;            // 接触判定に使う半径。
    double tolerance;         // 接触判定に使う許容幅。
    double boundsLow[3];      // 半径分広げたAABBの下限。
    double boundsHigh[3];     // 半径分広げたAABBの上限。
};

}

#endif
