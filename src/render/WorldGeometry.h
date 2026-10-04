#pragma once

#include "Geometry.h"

/**
 * World-space attributes carried through model transforms and camera clipping.
 */
namespace gk::render {

/**
 * One source triangle corner before model and camera transforms.
 */
struct WorldVertex {
    Vec3 position;
    Vec3 normal;
    float uv[2];
};

/**
 * Projected surface and lighting attributes for one clipped world vertex.
 */
struct ProjectedWorldVertex {
    Vertex surface;
    float worldNormal[3];
    float viewDirection[3];
};

/**
 * Clips and projects one world-space triangle, preserving its UV and lighting attributes.
 */
bool ProjectWorldTriangle(const detail::FramePacket& frame, const detail::DrawPacket& draw,
                          const WorldVertex points[3], bool applyModelTransform,
                          bool includeLighting, const float* linearColor,
                          ProjectedWorldVertex output[18], uint32_t& outputCount,
                          String& error);

} // namespace gk::render
