// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FMODELTRANSPARENCYDRAW_H
#define GKCORE_RENDER_FMODELTRANSPARENCYDRAW_H

#include <stdint.h>

namespace gk::render
{
/**
 * 奥行き順へ並べる透明モデルの1三角形。
 */
struct FModelTransparencyDraw
{
    // frame内で姿勢とcameraを保持する描画の番号。
    uint32_t drawIndex;
    // 元モデルの描画計画におけるprimitive番号。
    uint32_t partIndex;
    // 元モデルのindex列における三角形の開始位置。
    uint32_t firstIndex;
    // この三角形群を描き終えてから処理する次のScene命令。末尾はdraw数。
    uint32_t barrierIndex;
    // 同じ奥行きの場合に維持する描画予約順。
    uint64_t sequence;
    // cameraから三角形重心までのview方向の距離。
    double depth;
};
}

#endif
