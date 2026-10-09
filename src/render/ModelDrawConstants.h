// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_MODELDRAWCONSTANTS_H
#define GKCORE_RENDER_MODELDRAWCONSTANTS_H
#include "render/FModelDrawConstants.h"
#include "internal/Backend.hpp"
/**
 * CPUの描画入力をGPUの変換定数へまとめる。
 */
namespace gk::render
{
/**
 * cameraとmodelの変換を検証して格納する。GPUの安全範囲外では出力を保ちfalse。
 */
bool PackModelDrawConstants(const detail::FramePacket& frame, const detail::DrawPacket& draw, FModelDrawConstants& output, String& error);
}
#endif
