// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTEPSHAPECACHE_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTEPSHAPECACHE_H

#include "foundation/Array.h"
#include "foundation/String.h"
#include "model/animation/FModelSecondaryMotionStepShapes.h"

/**
 * 1回のmodel更新中に鎖ごとの接触半径別shapeを共有する。
 */
namespace gk::model::animation
{

/**
 * 同じ入力shapeと節半径から拡張・guard・準備形状を一度だけ生成する。
 */
class FModelSecondaryMotionStepShapeCache
{
  public:
    /**
     * 呼び出し元が保持する不変shape配列を借りてcacheを作る。
     */
    FModelSecondaryMotionStepShapeCache(const FModelSecondaryMotionCollisionShape* shapes, uint32_t count);

    /**
     * 節半径に対応するshape群を返す。失敗時はoutputと登録済みの形状内容を保つ。
     * 返す配列参照は次のGetまでに使い切る。次の確保で配列の位置が変わり得る。
     */
    bool Get(float pointRadius, FModelSecondaryMotionStepShapes& output, gk::String& error);

  private:
    /**
     * 同じ節半径の形状群が、所有配列のどこにあるかを保持する。
     */
    struct FGroup
    {
        // このgroupを作った節半径。
        float pointRadius = 0.0f;
        // flat shape配列におけるgroup先頭位置。
        uint32_t offset = 0;
    };

    // 呼び出し元がcache寿命中に保持する入力shape配列。
    const FModelSecondaryMotionCollisionShape* sourceShapes = nullptr;
    // 入力shape数。0以上64以下をGetで検証する。
    uint32_t sourceShapeCount = 0;
    // 半径ごとの配列範囲を保持するgroup一覧。
    gk::Array<FGroup> groups;
    // 節半径を加えたshape群を連結して所有する。
    gk::Array<FModelSecondaryMotionCollisionShape> expandedShapes;
    // 1.001 guard済みshape群を連結して所有する。
    gk::Array<FModelSecondaryMotionCollisionShape> guardedShapes;
    // guard済みshapeから作ったprepared群を連結して所有する。
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedShapes;
};

}

#endif
