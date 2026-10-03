#pragma once

#include <gkcore.h>

/**
 * Camera, model transforms, and world-space geometry commands.
 */
namespace gk {

int DrawTriangle3D(Vec3 a, Vec3 b, Vec3 c, uint32_t color, bool filled);
int SetCamera(Vec3 position, Vec3 target);
int DrawModel(ModelHandle model);
int SetModelPosition(ModelHandle model, Vec3 position);
int SetModelRotation(ModelHandle model, Vec3 rotationRadians);
int SetModelScale(ModelHandle model, Vec3 scale);

}
