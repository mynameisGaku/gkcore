#include "model/Model.h"
#include "model/animation/FModelAnimationAsset.h"

/**
 * model resourceの生成と破棄を管理する処理。
 */
namespace gk::detail
{
ModelResource* CreateModelResource()
{
    ModelResource* model = nullptr;
    try
    {
        model = new ModelResource;
    }
    catch (...)
    {
        return nullptr;
    }
    model->reference.references = 1;
    model->reference.destroy = DestroyModelResource;
    ModelMaterial material{};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.metallicFactor = 1.0f;
    material.roughnessFactor = 1.0f;
    material.baseColorTextureIndex = -1;
    if (!model->materials.Append(material))
    {
        Release(&model->reference);
        return nullptr;
    }
    return model;
}

void DestroyModelResource(RefCounted* object)
{
    ModelResource* model = reinterpret_cast<ModelResource*>(object);
    if (model->animation)
        Release(&model->animation->reference);
    if (model->geometrySource)
        Release(&model->geometrySource->reference);
    for (uint32_t i = 0; i < model->textures.Count(); ++i)
    {
        if (model->textures.At(i))
            Release(&model->textures.At(i)->reference);
    }
    delete model;
}
}
