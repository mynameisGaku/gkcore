#include "Resources.h"
#include "../foundation/HandleTable.h"
#include "../foundation/Memory.h"
#include "../image/ImageLoader.h"
#include "../model/ModelLoader.h"

namespace gk::detail {
namespace {
HandleTable<ImageTag, ImageResource> imageHandles;
HandleTable<ModelTag, ModelResource> modelHandles;
Array<ImageResource*> imageObjects;
Array<ModelResource*> modelObjects;
uint32_t nextResourceHandle = 1;

bool AllocateHandle(uint32_t& value) {
    if (nextResourceHandle == 0) return false;
    value = nextResourceHandle++;
    return true;
}
void RemoveImageObject(ImageResource* image) {
    for (uint32_t i = 0; i < imageObjects.Count(); ++i) {
        if (imageObjects.At(i) == image) {
            imageObjects.RemoveAt(i);
            return;
        }
    }
}
void RemoveModelObject(ModelResource* model) {
    for (uint32_t i = 0; i < modelObjects.Count(); ++i) {
        if (modelObjects.At(i) == model) {
            modelObjects.RemoveAt(i);
            return;
        }
    }
}
}

ImageHandle LoadImage(const char* path, String& error) {
    ImageResource* image = LoadImagePayload(path, error);
    if (!image) return ImageHandle();
    uint32_t value = 0;
    const ImageHandle handle(AllocateHandle(value) ? value : 0);
    if (!handle.IsValid() || !imageHandles.Insert(handle, image) || !imageObjects.Append(image)) {
        ImageResource* removed = nullptr;
        if (handle.IsValid()) imageHandles.Remove(handle, removed);
        Release(&image->reference);
        error.Assign("image resource handle allocation failed");
        return ImageHandle();
    }
    error.Clear();
    return handle;
}

bool DeleteImage(ImageHandle handle, String& error) {
    ImageResource* image = nullptr;
    if (!imageHandles.Remove(handle, image) || !image) {
        error.Assign("invalid or stale image handle");
        return false;
    }
    RemoveImageObject(image);
    Release(&image->reference);
    error.Clear();
    return true;
}

ImageResource* FindImage(ImageHandle handle) {
    return imageHandles.Find(handle);
}

ModelHandle LoadModel(const char* path, String& error) {
    ModelResource* model = LoadModelPayload(path, error);
    if (!model) return ModelHandle();
    uint32_t value = 0;
    const ModelHandle handle(AllocateHandle(value) ? value : 0);
    if (!handle.IsValid() || !modelHandles.Insert(handle, model) || !modelObjects.Append(model)) {
        ModelResource* removed = nullptr;
        if (handle.IsValid()) modelHandles.Remove(handle, removed);
        Release(&model->reference);
        error.Assign("model resource handle allocation failed");
        return ModelHandle();
    }
    error.Clear();
    return handle;
}

bool DeleteModel(ModelHandle handle, String& error) {
    ModelResource* model = nullptr;
    if (!modelHandles.Remove(handle, model) || !model) {
        error.Assign("invalid or stale model handle");
        return false;
    }
    RemoveModelObject(model);
    Release(&model->reference);
    error.Clear();
    return true;
}

ModelResource* FindModel(ModelHandle handle) {
    return modelHandles.Find(handle);
}

void ClearResources() {
    imageHandles.Clear();
    modelHandles.Clear();
    for (uint32_t i = 0; i < imageObjects.Count(); ++i) Release(&imageObjects.At(i)->reference);
    for (uint32_t i = 0; i < modelObjects.Count(); ++i) Release(&modelObjects.At(i)->reference);
    imageObjects.Clear();
    modelObjects.Clear();
}
}
