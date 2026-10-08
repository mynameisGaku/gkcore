// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_EMODELANIMATIONFORMAT_H
#define GKCORE_MODEL_ANIMATION_EMODELANIMATIONFORMAT_H

/**
 * 対応するanimation sourceの形式を区別する型。
 */
namespace gk::model
{
/**
 * モデルと外部アニメーションの互換性を確認する入力形式。
 */
enum class EModelAnimationFormat
{
    // 連番OBJによる頂点アニメーション。
    ObjSequence,
    // GLBの階層、骨、変形アニメーション。
    Glb,
    // FBXの階層、骨、変形アニメーション。
    Fbx
};
}

#endif
