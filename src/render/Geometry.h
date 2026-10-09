#pragma once

#include "internal/Backend.hpp"
#include "render/ModelDrawPlan.h"

/**
 * CPU-side draw conversion and geometry preparation.
 */
namespace gk::render
{

/**
 * A clip-space color or textured vertex consumed by built-in Forge pipelines.
 * Model coordinates retain perspective-correct UVs through homogeneous clipping.
 */
struct Vertex
{
    float position[4];
    float color[4];
    float uv[2];
};

/**
 * A contiguous set of triangles sharing the same depth state.
 */
struct DrawRun
{
    uint32_t first;
    uint32_t count;
    bool depthTest;
};

/**
 * Converts one API draw packet to bounded clip-space vertices.
 */
bool AppendDraw(const detail::FramePacket& frame, const detail::DrawPacket& draw, Array<Vertex>& vertices, uint32_t vertexLimit, String& error);

/**
 * Expands one image snapshot into a scaled and rotated textured quad.
 */
bool AppendSprite(const detail::FramePacket& frame, const detail::DrawPacket& draw, Array<Vertex>& vertices, uint32_t vertexLimit, String& error);

/**
 * Expands one validated model primitive using its linear base-color factor and index range.
 */
bool AppendModelPart(const detail::FramePacket& frame, const detail::DrawPacket& draw, const ModelPartPlan& part, Array<Vertex>& vertices, uint32_t vertexLimit, String& error);

}
