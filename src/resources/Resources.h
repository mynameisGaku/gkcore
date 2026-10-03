#pragma once

#include "../../include/gkcore/Handle.h"
#include "../foundation/Array.h"
#include "../foundation/RefCount.h"
#include "../foundation/String.h"
#include <stdint.h>

/**
 * Resource payloads and operations used by the gkcore runtime.
 */
namespace gk::detail {

/**
 * Decoded image pixels stored as tightly packed top-left RGBA bytes.
 */
struct ImageResource {
    RefCounted reference;
    uint32_t width;
    uint32_t height;
    Array<uint8_t> rgba;
};

/**
 * One model vertex with position, normal, and texture coordinates.
 */
struct ModelVertex {
    float position[3];
    float normal[3];
    float uv[2];
};

/**
 * Material factors and optional base-color image index for a mesh primitive.
 */
struct ModelMaterial {
    float baseColorFactor[4];
    float metallicFactor;
    float roughnessFactor;
    int32_t baseColorTextureIndex;
};

/**
 * Contiguous index range associated with one material.
 */
struct ModelPrimitive {
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t materialIndex;
};

/**
 * Static indexed geometry, primitive materials, and retained texture images.
 */
struct ModelResource {
    RefCounted reference;
    Array<ModelVertex> vertices;
    Array<uint32_t> indices;
    Array<ModelPrimitive> primitives;
    Array<ModelMaterial> materials;
    Array<ImageResource*> textures;
};

/**
 * Loads a supported image file and returns its typed resource handle.
 */
ImageHandle LoadImage(const char* path, String& error);
/**
 * Removes a registered image handle while retained draw snapshots stay valid.
 */
bool DeleteImage(ImageHandle handle, String& error);
/**
 * Returns a borrowed image payload for a currently registered handle.
 */
ImageResource* FindImage(ImageHandle handle);
/**
 * Loads static geometry and materials from a supported model file.
 */
ModelHandle LoadModel(const char* path, String& error);
/**
 * Removes a registered model handle while retained draw snapshots stay valid.
 */
bool DeleteModel(ModelHandle handle, String& error);
/**
 * Returns a borrowed model payload for a currently registered handle.
 */
ModelResource* FindModel(ModelHandle handle);
/**
 * Releases registry ownership of every loaded resource.
 */
void ClearResources();

} // namespace gk::detail
