// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelAnimationResources.h"
#include "foundation/HandleTable.h"
#include "foundation/Array.h"

/**
 * animation handleとasset所有権を管理するregistry。
 */
namespace gk::model
{
namespace
{
// clip handleとその所有者の対応。
HandleTable<ModelAnimationTag, FModelAnimationAsset> handles;
// shutdownで解放する登録参照。
Array<FModelAnimationAsset*> objects;
// 解放後も再利用しないhandle番号。
uint32_t nextHandle = 1;
}

ModelAnimationHandle RegisterAnimation(FModelAnimationAsset* asset, String& error)
{
    if (!asset)
        return {};
    const ModelAnimationHandle handle(nextHandle == 0 ? 0 : nextHandle++);
    if (!handle.IsValid() || !handles.Insert(handle, asset) || !objects.Append(asset))
    {
        FModelAnimationAsset* removed = nullptr;
        handles.Remove(handle, removed);
        Release(&asset->reference);
        error.Assign("animation handle allocation failed");
        return {};
    }
    error.Clear();
    return handle;
}

FModelAnimationAsset* FindAnimation(ModelAnimationHandle handle)
{
    return handles.Find(handle);
}

bool DeleteAnimation(ModelAnimationHandle handle, String& error)
{
    FModelAnimationAsset* asset = nullptr;
    if (!handles.Remove(handle, asset))
    {
        error.Assign("invalid animation handle");
        return false;
    }
    for (uint32_t i = 0; i < objects.Count(); ++i)
    {
        if (objects.At(i) == asset)
        {
            objects.RemoveAt(i);
            break;
        }
    }
    Release(&asset->reference);
    error.Clear();
    return true;
}

void ClearAnimations()
{
    handles.Clear();
    for (uint32_t i = 0; i < objects.Count(); ++i)
        Release(&objects.At(i)->reference);
    objects.Clear();
}
}
