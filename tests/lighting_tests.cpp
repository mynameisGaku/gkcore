#include "effects/Lighting.h"
#include "render/LightingAbi.h"

#include <math.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

namespace
{
bool Require(bool condition, const char* message)
{
    if (condition)
        return true;
    fprintf(stderr, "%s\n", message);
    return false;
}
}

int main()
{
    using namespace gk;
    gk::effects::ResetLighting();
    const gk::effects::LightingSettings valueInitialized{};
    if (!Require(valueInitialized.ambientIntensity == 0.2f && valueInitialized.directionalIntensity == 3.0f && valueInitialized.direction.x == -0.4082483f, "value-initialized lighting defaults are incorrect"))
        return 1;
    gk::effects::LightingSettings value = gk::effects::CurrentLighting();
    if (!Require(value.ambientIntensity == 0.2f && value.direction.x == -0.4082483f && value.direction.y == -0.8164966f && value.direction.z == 0.4082483f && value.directionalIntensity == 3.0f, "lighting defaults are incorrect"))
        return 1;
    if (!Require(gk::effects::SetAmbientLight(0.0f) && gk::effects::SetAmbientLight(4.0f) && gk::effects::SetAmbientLight(1.25f), "valid ambient values were rejected"))
        return 1;
    if (!Require(!gk::effects::SetAmbientLight(-0.01f) && !gk::effects::SetAmbientLight(4.01f) && !gk::effects::SetAmbientLight(NAN) && !gk::effects::SetAmbientLight(INFINITY) && gk::effects::CurrentLighting().ambientIntensity == 1.25f, "invalid ambient values changed settings"))
        return 1;

    if (!Require(gk::effects::SetDirectionalLight({ 3.0f, 4.0f, 0.0f }, 16.0f), "valid directional light was rejected"))
        return 1;
    value = gk::effects::CurrentLighting();
    if (!Require(fabsf(value.direction.x - 0.6f) < 1e-6f && fabsf(value.direction.y - 0.8f) < 1e-6f && value.direction.z == 0.0f && value.directionalIntensity == 16.0f, "direction was not normalized"))
        return 1;
    if (!Require(gk::effects::SetDirectionalLight({ FLT_MAX, FLT_MAX, 0.0f }, 0.0f), "large finite direction was rejected"))
        return 1;
    value = gk::effects::CurrentLighting();
    if (!Require(fabsf(value.direction.x - 0.70710678f) < 1e-6f && fabsf(value.direction.y - 0.70710678f) < 1e-6f, "large direction normalization failed"))
        return 1;
    if (!Require(gk::effects::SetDirectionalLight({ FLT_MIN, 0.0f, 0.0f }, 2.0f) && gk::effects::CurrentLighting().direction.x == 1.0f, "small finite direction was rejected"))
        return 1;
    const gk::effects::LightingSettings preserved = gk::effects::CurrentLighting();
    if (!Require(!gk::effects::SetDirectionalLight({ 0.0f, 0.0f, 0.0f }, 2.0f) && !gk::effects::SetDirectionalLight({ NAN, 0.0f, 1.0f }, 2.0f) && !gk::effects::SetDirectionalLight({ 1.0f, 0.0f, 0.0f }, -0.1f) && !gk::effects::SetDirectionalLight({ 1.0f, 0.0f, 0.0f }, 16.01f) && !gk::effects::SetDirectionalLight({ 1.0f, 0.0f, 0.0f }, INFINITY) && gk::effects::CurrentLighting().direction.x == preserved.direction.x && gk::effects::CurrentLighting().direction.y == preserved.direction.y && gk::effects::CurrentLighting().directionalIntensity == preserved.directionalIntensity, "invalid directional light changed settings"))
        return 1;

    String error;
    render::LightingConstants packed{};
    if (!Require(render::PackLightingConstants(gk::effects::CurrentLighting(), packed, error) && error.Empty(), "valid lighting settings did not pack"))
        return 1;
    if (!Require(packed.directionIntensity[0] == gk::effects::CurrentLighting().direction.x && packed.directionIntensity[1] == gk::effects::CurrentLighting().direction.y && packed.directionIntensity[2] == gk::effects::CurrentLighting().direction.z && packed.directionIntensity[3] == gk::effects::CurrentLighting().directionalIntensity && packed.ambient[0] == gk::effects::CurrentLighting().ambientIntensity && packed.ambient[1] == 0.0f && packed.ambient[2] == 0.0f && packed.ambient[3] == 0.0f, "lighting constants layout or padding is incorrect"))
        return 1;
    const gk::effects::LightingSettings largeDirection = { 0.5f, { FLT_MAX, FLT_MAX, 0.0f }, 2.0f };
    if (!Require(render::PackLightingConstants(largeDirection, packed, error) && fabsf(packed.directionIntensity[0] - 0.70710678f) < 1e-6f && fabsf(packed.directionIntensity[1] - 0.70710678f) < 1e-6f, "packer did not normalize a very large direction safely"))
        return 1;
    const gk::effects::LightingSettings smallDirection = { 0.5f, { FLT_MIN, 0.0f, 0.0f }, 2.0f };
    if (!Require(render::PackLightingConstants(smallDirection, packed, error) && packed.directionIntensity[0] == 1.0f && packed.directionIntensity[1] == 0.0f, "packer did not normalize a very small direction safely"))
        return 1;
    const gk::effects::LightingSettings invalidCases[] = { { 0.5f, { 0.0f, 0.0f, 0.0f }, 2.0f }, { 0.5f, { NAN, 0.0f, 1.0f }, 2.0f }, { 0.5f, { INFINITY, 0.0f, 1.0f }, 2.0f }, { -0.01f, { 1.0f, 0.0f, 0.0f }, 2.0f }, { 4.01f, { 1.0f, 0.0f, 0.0f }, 2.0f }, { NAN, { 1.0f, 0.0f, 0.0f }, 2.0f }, { INFINITY, { 1.0f, 0.0f, 0.0f }, 2.0f }, { 0.5f, { 1.0f, 0.0f, 0.0f }, -0.01f }, { 0.5f, { 1.0f, 0.0f, 0.0f }, 16.01f }, { 0.5f, { 1.0f, 0.0f, 0.0f }, NAN }, { 0.5f, { 1.0f, 0.0f, 0.0f }, INFINITY } };
    for (uint32_t index = 0; index < sizeof(invalidCases) / sizeof(invalidCases[0]); ++index)
    {
        unsigned char* bytes = reinterpret_cast<unsigned char*>(&packed);
        for (uint32_t byte = 0; byte < sizeof(packed); ++byte)
            bytes[byte] = static_cast<unsigned char>(byte * 13u + 7u);
        render::LightingConstants before = packed;
        if (!Require(!render::PackLightingConstants(invalidCases[index], packed, error) && !error.Empty() && memcmp(&packed, &before, sizeof(packed)) == 0, "invalid settings changed packed output"))
            return 1;
    }
    gk::effects::ResetLighting();
    if (!Require(gk::effects::CurrentLighting().ambientIntensity == 0.2f && gk::effects::CurrentLighting().directionalIntensity == 3.0f, "lighting reset failed"))
        return 1;
    return 0;
}
