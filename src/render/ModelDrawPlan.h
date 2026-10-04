#pragma once

#include "../resources/Resources.h"

/**
 * CPU validation and material mapping for static model draws.
 */
namespace gk::render {

/**
 * One validated primitive index range with its linear base-color and texture selection.
 */
struct ModelPartPlan {
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t materialIndex;
    int32_t textureIndex;
    float baseColorFactor[4];
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
};

/**
 * Ordered primitive plan used to prepare model vertices and texture runs.
 */
struct ModelDrawPlan {
    Array<ModelPartPlan> parts;

    /**
     * Clears the current plan while retaining its backing capacity.
     */
    void Reset();
};

/**
 * Validates primitive bounds and factors before replacing output with ordered draw metadata.
 */
bool BuildModelDrawPlan(const detail::ModelResource& model, ModelDrawPlan& output,
                        String& error);

} // namespace gk::render
