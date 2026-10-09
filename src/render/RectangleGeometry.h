#pragma once

#include "render/Geometry.h"

/**
 * Converts screen-space rectangle draw packets into bounded clip-space vertices.
 */
namespace gk::render
{

/**
 * Appends a filled rectangle or a one-pixel-default outline without partial output.
 */
bool AppendRectangle(const detail::FramePacket& frame, const detail::DrawPacket& draw, Array<Vertex>& vertices, uint32_t vertexLimit, String& error);

} // namespace gk::render
