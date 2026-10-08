// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELANIMATIONASSET_H
#define GKCORE_MODEL_ANIMATION_FMODELANIMATIONASSET_H

#include "../../foundation/RefCount.h"
#include "../../foundation/String.h"
#include "../../foundation/Array.h"

/**
 * animation sourceの共有所有権を扱う内部型。
 */
namespace gk::model
{
class AModelAnimationSource;
/**
 * モデルや再生状態から共有される、元アニメーション情報の所有者。
 */
struct FModelAnimationAsset
{
    // 元データを必要とするモデル・再生状態の保持数。
    RefCounted reference;
    // 形式固有の元データ。最終参照の解放時に破棄する。
    AModelAnimationSource* source = nullptr;
    // 外部clipのボーンの役割。対応表作成後の再生状態には影響しない。
    Array<uint16_t> roles;
};
/**
 * sourceの所有権を受け取り共有所有者を作る。失敗時もsourceを破棄する。
 */
FModelAnimationAsset* CreateModelAnimationAsset(AModelAnimationSource* source, String& error);
}

#endif
