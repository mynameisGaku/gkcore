// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_MODELTRANSPARENCY_H
#define GKCORE_RENDER_MODELTRANSPARENCY_H

#include "render/FModelTransparencyDraw.h"
#include "render/ModelDrawPlan.h"
#include "internal/Backend.hpp"

namespace gk::render
{
/**
 * 同じcameraの連続したSceneモデル群ごとに、透明三角形を奥から並べる。
 * 他の描画・custom shader・camera変更を越えず、UIは元の命令順を保つ。
 * triangleLimit超過・不正入力・確保失敗ではoutputを変更しない。
 */
bool BuildModelTransparencyPlan(const detail::FramePacket& frame, uint32_t triangleLimit, Array<FModelTransparencyDraw>& output, String& error, const ModelDrawPlan::FRange* drawPlanRanges = nullptr, uint32_t drawPlanRangeCount = 0, const ModelPartPlan* drawPlanParts = nullptr, uint32_t drawPlanPartCount = 0);
}

#endif
