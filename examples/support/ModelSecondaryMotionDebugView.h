// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_EXAMPLES_MODELSECONDARYMOTIONDEBUGVIEW_H
#define GKCORE_EXAMPLES_MODELSECONDARYMOTIONDEBUGVIEW_H

#include <gkcore.h>

/**
 * 揺れものの身体形状と骨の接触範囲を表示する補助機能。
 */
namespace gk::examples
{

/**
 * モデル空間の揺れものdebug形状を表示座標へ変換して描く。
 * 呼び出し側は事前にUIレイヤーを選ぶ。形状数が4096を超える、入力が無効、または描画APIが失敗した場合はfalseを返す。
 */
bool DrawModelSecondaryMotionDebug(gk::ModelHandle model, float scale, gk::Vec3 displayCenter, float rotationY);
/**
 * --collision-debugを取り出す。重複時は引数列とenabledを保つ。
 */
bool ParseSecondaryMotionDebugOptions(int& argc, char** argv, bool& enabled);

}

#endif
