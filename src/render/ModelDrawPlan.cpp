#include "ModelDrawPlan.h"

#include <float.h>

/**
 * Validates static-model primitives and converts material data to draw-ready records.
 */
namespace gk::render {
/**
 * Keeps finite-factor checks local to model draw-plan construction.
 */
namespace {

/**
 * Replaces the validation diagnostic and returns failure.
 */
bool Fail(String& error, const char* message) {
    error.Assign(message);
    return false;
}

/**
 * Checks a finite normalized material factor.
 */
bool IsUnitFactor(float value) {
    return value == value && value >= 0.0f && value <= 1.0f && value <= FLT_MAX;
}

/**
 * Validates every scalar consumed or retained by one material.
 */
bool IsMaterialValid(const detail::ModelMaterial& material) {
    for (uint32_t i = 0; i < 4; ++i) {
        if (!IsUnitFactor(material.baseColorFactor[i])) return false;
    }
    return IsUnitFactor(material.metallicFactor) && IsUnitFactor(material.roughnessFactor);
}

} // namespace

/**
 * Clears the ordered draw records while keeping reusable storage.
 */
void ModelDrawPlan::Reset() {
    parts.Clear();
}

/**
 * Builds one validated ordered draw record per model primitive.
 */
bool BuildModelDrawPlan(const detail::ModelResource& model, ModelDrawPlan& output,
                        String& error) {
    error.Clear();
    ModelDrawPlan candidate;
    for (uint32_t primitiveIndex = 0; primitiveIndex < model.primitives.Count(); ++primitiveIndex) {
        const detail::ModelPrimitive& primitive = model.primitives.At(primitiveIndex);
        if (primitive.indexCount == 0 || primitive.indexCount % 3 != 0 ||
            primitive.firstIndex > model.indices.Count() ||
            primitive.indexCount > model.indices.Count() - primitive.firstIndex)
            return Fail(error, "The model primitive index range is invalid");
        for (uint32_t i = 0; i < primitive.indexCount; ++i) {
            if (model.indices.At(primitive.firstIndex + i) >= model.vertices.Count())
                return Fail(error, "The model primitive contains an invalid vertex index");
        }

        ModelPartPlan part{};
        part.firstIndex = primitive.firstIndex;
        part.indexCount = primitive.indexCount;
        part.materialIndex = primitive.materialIndex;
        part.textureIndex = -1;
        part.baseColorFactor[0] = 1.0f;
        part.baseColorFactor[1] = 1.0f;
        part.baseColorFactor[2] = 1.0f;
        part.baseColorFactor[3] = 1.0f;

        if (primitive.materialIndex != -1) {
            if (primitive.materialIndex < 0 ||
                static_cast<uint32_t>(primitive.materialIndex) >= model.materials.Count())
                return Fail(error, "The model primitive material index is invalid");
            const detail::ModelMaterial& material =
                model.materials.At(static_cast<uint32_t>(primitive.materialIndex));
            if (!IsMaterialValid(material))
                return Fail(error, "The model material contains an invalid factor");
            for (uint32_t component = 0; component < 4; ++component)
                part.baseColorFactor[component] = material.baseColorFactor[component];
            if (material.baseColorTextureIndex != -1) {
                if (material.baseColorTextureIndex < 0 ||
                    static_cast<uint32_t>(material.baseColorTextureIndex) >= model.textures.Count() ||
                    !model.textures.At(static_cast<uint32_t>(material.baseColorTextureIndex)))
                    return Fail(error, "The model material texture index is invalid");
                part.textureIndex = material.baseColorTextureIndex;
            }
        }
        if (!candidate.parts.Append(part))
            return Fail(error, "The model draw plan allocation failed");
    }
    output.parts.MoveFrom(candidate.parts);
    error.Clear();
    return true;
}

} // namespace gk::render
