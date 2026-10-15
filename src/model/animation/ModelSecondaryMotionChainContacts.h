// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCHAINCONTACTS_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCHAINCONTACTS_H

#include <stdint.h>
#include "foundation/Array.h"
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/FPreparedSecondaryMotionCollisionShape.h"
#include "model/animation/FSecondaryMotionChainAttachmentContact.h"
#include "model/animation/FSecondaryMotionReferenceSegment.h"
#include "model/animation/FSecondaryMotionContactLinearSystem.h"

/**
 * 鎖全体の骨長・曲げ角を保つ身体接触計算。
 */
namespace gk::model::animation
{

/**
 * 鎖の全節方向を同時に調整して身体接触を解く。有限探索で候補を見つけられない場合はpositionsを保ち、errorは空のまま返す。不正入力や計算失敗ではerrorを設定する。
 */
bool ResolvePreparedSecondaryMotionChainContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionReferenceSegment* referenceSegments, uint32_t segmentCount, double cosineLimit, gk::Array<gk::FVector3d>& positions, gk::String& error, double toleranceFraction = 1.0);

/**
 * 鎖の身体接触と添付線分を同時に解く。各添付定義はsegmentの基準方向と鎖の角度制約に一致させる。候補が見つからない場合はpositionsを保ち、errorを空のまま返す。
 */
bool ResolvePreparedSecondaryMotionChainContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionReferenceSegment* referenceSegments, uint32_t segmentCount, double cosineLimit, gk::Array<gk::FVector3d>& positions, gk::String& error, double toleranceFraction, const FSecondaryMotionChainAttachmentContact* contacts, uint32_t contactCount);

/**
 * 既存の鎖身体接触・bone-local接触のmeritと方向勾配を、全身solver向けの配列範囲で評価する。linearSystem指定時はfirstSegmentから接触行も集める。
 * directionGradients=nullptrとgradientCount=0を指定すると位置とmeritだけを評価する。この場合linearSystemは指定できない。
 */
bool EvaluatePreparedSecondaryMotionChainContactsMerit(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionReferenceSegment* referenceSegments, uint32_t segmentCount, double cosineLimit, double toleranceFraction, const FSecondaryMotionChainAttachmentContact* contacts, uint32_t contactCount, const gk::FVector3d* positions, uint32_t positionCount, double& merit, gk::FVector3d* directionGradients, uint32_t gradientCount, gk::String& error, FSecondaryMotionContactLinearSystem* linearSystem = nullptr, uint32_t firstSegment = 0, bool useClearanceMargin = false);
}

#endif
