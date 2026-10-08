// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODEL_BONE_TRANSFORM_H
#define GKCORE_MODEL_ANIMATION_FMODEL_BONE_TRANSFORM_H

namespace gk::model::animation
{

/**
 * ボーンの親基準位置、回転、拡大率を表す値。
 */
struct FModelBoneTransform
{
    // 親ボーン基準の位置。
    float position[3]{};
    // 親ボーン基準の単位回転quaternion。要素順はX、Y、Z、W。
    float rotation[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
    // 各軸の拡大率。姿勢評価では有限なゼロ値も保持する。
    float scale[3]{ 1.0f, 1.0f, 1.0f };
};

}

#endif
