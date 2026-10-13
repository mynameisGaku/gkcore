// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_EXAMPLES_MODELPINGPONGMOTION_H
#define GKCORE_EXAMPLES_MODELPINGPONGMOTION_H

/**
 * viewer用の折り返し再生補助。
 */
namespace gk::examples
{
/**
 * 再生位相をclip範囲内の折り返し時刻へ変換する。入力不正時はoutputを保ちfalse。
 */
bool EvaluatePingPongMotionTime(double phaseSeconds, double durationSeconds, double& outputSeconds);

/**
 * --ping-pong-motionを位置引数から除く。重複・null引数では引数とoutputを保ちfalse。
 */
bool ParsePingPongMotionOptions(int& argc, char** argv, bool& enabled);
}

#endif
