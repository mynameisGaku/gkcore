// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/AModelAnimationSource.h"

/**
 * animation sourceの共有所有権を管理する処理。
 */
namespace gk::model
{
/**
 * 最終参照を失った元データと所有者を破棄する。
 */
static void DestroyAnimationAsset(RefCounted* reference)
{
    FModelAnimationAsset* asset = reinterpret_cast<FModelAnimationAsset*>(reference);
    delete asset->source;
    delete asset;
}

FModelAnimationAsset* CreateModelAnimationAsset(AModelAnimationSource* source, String& error)
{
    if (!source)
    {
        error.Assign("animation source is missing");
        return nullptr;
    }
    FModelAnimationAsset* asset = nullptr;
    try
    {
        asset = new FModelAnimationAsset;
    }
    catch (...)
    {
        delete source;
        error.Assign("animation asset allocation failed");
        return nullptr;
    }
    asset->reference = { 1, DestroyAnimationAsset };
    asset->source = source;
    error.Clear();
    return asset;
}
}
