// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_TESTS_SUPPORT_MODEL_SKIN_POINTS_CAPTURE_H
#define GKCORE_TESTS_SUPPORT_MODEL_SKIN_POINTS_CAPTURE_H

#include <gkcore.h>

/**
 * 指定bone prefixのskin位置を参照と比較し、身体形状への侵入を報告する。
 * skin位置と身体形状の距離はmodel spaceで測り、instance共通SRT適用前の値を扱う。
 * 参照一致は1、無効な入力や実行失敗は0、未対応条件や参照不一致は-1を返す。
 * 侵入数は診断のみで、参照一致の戻り値には影響しない。
 */
extern "C" GKCORE_API int VerifyModelSkinPointsForTesting(gk::ModelHandle model, uint32_t frame, const char* bonePrefix);

#endif
