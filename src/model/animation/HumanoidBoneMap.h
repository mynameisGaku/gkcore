// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_HUMANOIDBONEMAP_H
#define GKCORE_MODEL_ANIMATION_HUMANOIDBONEMAP_H

#include "model/animation/AModelAnimationSource.h"
#include "foundation/Array.h"

/**
 * 骨格へ保存済みの人型役割を対応付ける処理。
 */
namespace gk::model
{
/**
 * UTF-8の役割対応表を読み、成功時に全ボーン分の役割配列へ置き換える。
 * pathは対応表のファイル名、sourceは骨名と書き換え可否を返す。
 * 不正な行、重複、未解決の骨名、読み込み失敗時はfalseを返し、outputを保つ。
 */
bool LoadHumanoidBoneMap(const char* path, const AModelAnimationSource& source, Array<uint16_t>& output, String& error);
}

#endif
