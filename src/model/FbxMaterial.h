#pragma once

#include "Model.h"
#include "../foundation/String.h"
#include "../foundation/Array.h"
#include <stdint.h>

struct ufbx_material;
struct ufbx_mesh;
struct ufbx_texture;

/**
 * Converts one FBX material and its supported base-color image into model data.
 */
namespace gk::detail {

/**
 * Import-local source maps prevent repeated nodes from duplicating decoded assets.
 */
struct FbxMaterialContext {
    /**
     * Maps one ufbx texture or shared source image file to a model-local image slot.
     */
    struct TextureEntry {
        const ufbx_texture* source;
        int32_t modelIndex;
        uint32_t sourceFileIndex;
    };
    /**
     * Maps a parsed source material to its model-local slot.
     */
    struct MaterialEntry {
        const ufbx_material* source;
        int32_t modelIndex;
    };

    Array<TextureEntry> textures;
    Array<MaterialEntry> materials;
    int32_t defaultMaterialIndex;
    bool defaultMaterialUsed;

    /**
     * Starts with the default material seeded by CreateModelResource().
     */
    FbxMaterialContext() : defaultMaterialIndex(0), defaultMaterialUsed(false) {}
};

/**
 * Adds a supported FBX material and optional image, returning its model-local index.
 *
 * Material coefficients are preserved as linear values. A null source returns the
 * existing opaque-white default material. External images are resolved relative to
 * the model path; unsupported image graphs fail explicitly.
 */
bool LoadFbxMaterial(const ufbx_material* source, const char* utf8ModelPath,
                     ModelResource& model, FbxMaterialContext& context,
                     int32_t& outputIndex, String& error);

/**
 * Rejects a base-color UV set the geometry importer cannot select for this mesh.
 */
bool ValidateFbxMaterialUvSet(const ufbx_material* material, const ufbx_mesh* mesh,
                              String& error);

} // namespace gk::detail
