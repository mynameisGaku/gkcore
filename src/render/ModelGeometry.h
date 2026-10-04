#pragma once

#include "WorldGeometry.h"

/**
 * Lit static-model vertex expansion for the built-in PBR pipeline.
 */
namespace gk::render {

/**
 * Surface data plus world-space lighting inputs and material scalar factors.
 */
struct ModelRenderVertex {
    Vertex surface;
    float worldNormal[3];
    float viewDirection[3];
    float metallicRoughness[2];
};
static_assert(sizeof(ModelRenderVertex) == 72, "lit model vertex ABI must remain 72 bytes");

/**
 * Expands one validated model material range into lit vertices atomically.
 */
bool AppendLitModelPart(const detail::FramePacket& frame, const detail::DrawPacket& draw,
                        const ModelPartPlan& part, Array<ModelRenderVertex>& vertices,
                        uint32_t vertexLimit, String& error);

} // namespace gk::render
