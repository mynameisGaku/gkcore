#include "model/FbxMaterial.h"
#include "image/ImageLoader.h"
#include "resources/ResourceIO.h"
#include <ufbx/ufbx.h>
#include "foundation/Memory.h"

#include <math.h>
#include <string.h>

/**
 * Converts supported FBX material properties and images to owned model payloads.
 */
namespace gk::detail
{
/**
 * Keeps FBX-specific validation and color conversion helpers private to this module.
 */
namespace
{
const uint32_t maxEmbeddedImageBytes = 64u * 1024u * 1024u;
const uint32_t maxResourcePathBytes = 32768u;
const uint32_t noSourceFileIndex = UINT32_MAX;
const uint32_t maxModelTextures = 128u;
const uint64_t maxModelTextureBytes = 256u * 1024u * 1024u;

/**
 * Writes a useful diagnostic and marks the conversion as failed.
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * Appends a default white material if the first source material replaced it.
 */
bool EnsureDefaultMaterial(ModelResource& model, FbxMaterialContext& context, int32_t& outputIndex, String& error)
{
    if (context.defaultMaterialIndex >= 0 && static_cast<uint32_t>(context.defaultMaterialIndex) < model.materials.Count())
    {
        outputIndex = context.defaultMaterialIndex;
        context.defaultMaterialUsed = true;
        return true;
    }
    ModelMaterial material{};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.metallicFactor = 1.0f;
    material.roughnessFactor = 1.0f;
    material.baseColorTextureIndex = -1;
    if (model.materials.Count() >= static_cast<uint32_t>(INT32_MAX) || !model.materials.Append(material))
        return Fail(error, "FBX default material allocation failed");
    context.defaultMaterialIndex = static_cast<int32_t>(model.materials.Count() - 1);
    context.defaultMaterialUsed = true;
    outputIndex = context.defaultMaterialIndex;
    return true;
}

/**
 * Checks that an FBX numeric material value is finite and normalized.
 */
bool IsUnit(float value)
{
    return isfinite(value) != 0 && value >= 0.0f && value <= 1.0f;
}

/**
 * Rejects non-identity legacy UV values retained only in the bounded FBX DOM.
 */
bool HasLegacyUvTransform(const ufbx_texture& texture)
{
    const ufbx_dom_node* dom = texture.element.dom_node;
    if (!dom)
        return false;
    const ufbx_dom_node* translation = ufbx_dom_find(dom, "ModelUVTranslation");
    if (translation)
    {
        const ufbx_real_list values = ufbx_dom_as_real_list(translation);
        if (values.count < 2 || values.count > 3 || !values.data)
            return true;
        for (size_t i = 0; i < values.count; ++i)
        {
            if (!isfinite(static_cast<double>(values.data[i])) || fabs(static_cast<double>(values.data[i])) > 1.0e-6)
                return true;
        }
    }
    const ufbx_dom_node* scaling = ufbx_dom_find(dom, "ModelUVScaling");
    if (scaling)
    {
        const ufbx_real_list values = ufbx_dom_as_real_list(scaling);
        if (values.count < 2 || values.count > 3 || !values.data)
            return true;
        for (size_t i = 0; i < values.count; ++i)
        {
            if (!isfinite(static_cast<double>(values.data[i])) || fabs(static_cast<double>(values.data[i]) - 1.0) > 1.0e-6)
                return true;
        }
    }
    return false;
}

/**
 * Selects the PBR maps only when the material contains or enables PBR base color.
 */
bool HasPbrBaseColor(const ufbx_material& material)
{
    return material.features.pbr.enabled || material.pbr.base_color.has_value || material.pbr.base_color.texture != nullptr || material.pbr.base_factor.has_value || material.pbr.base_factor.texture != nullptr;
}

/**
 * Reads a source texture once and stores it in the model's retained image array.
 */
bool LoadTexture(const ufbx_texture* texture, const char* modelPath, ModelResource& model, FbxMaterialContext& context, int32_t& outputIndex, String& error)
{
    if (texture->type != UFBX_TEXTURE_FILE)
        return Fail(error, "FBX base-color image uses an unsupported layered or procedural texture");
    if (texture->has_uv_transform)
        return Fail(error, "FBX base-color image uses an unsupported UV transform");
    if (HasLegacyUvTransform(*texture))
        return Fail(error, "FBX base-color image uses an unsupported legacy UV transform");
    for (uint32_t i = 0; i < context.textures.Count(); ++i)
    {
        const FbxMaterialContext::TextureEntry& entry = context.textures.At(i);
        if (entry.source == texture || (texture->has_file && texture->file_index != noSourceFileIndex && entry.sourceFileIndex == texture->file_index))
        {
            outputIndex = entry.modelIndex;
            return true;
        }
    }
    if (model.textures.Count() >= maxModelTextures)
        return Fail(error, "FBX model exceeds the base-color texture-count limit");
    ImageResource* image = nullptr;
    if (texture->content.data && texture->content.size)
    {
        if (texture->content.size > maxEmbeddedImageBytes || texture->content.size > UINT32_MAX)
            return Fail(error, "FBX embedded base-color image exceeds the size limit");
        image = DecodeImagePayload(static_cast<const uint8_t*>(texture->content.data), static_cast<uint32_t>(texture->content.size), error);
        if (!image)
            return false;
    }
    else
    {
        const ufbx_string& sourcePath = texture->relative_filename.length ? texture->relative_filename : texture->filename;
        const char* relative = sourcePath.data;
        const size_t relativeLength = sourcePath.length;
        if (!relative || !relativeLength || !modelPath)
            return Fail(error, "FBX base-color image has no embedded data or relative file path");
        if (relativeLength > maxResourcePathBytes || relative[0] == '/' || relative[0] == '\\' || (relativeLength > 1 && relative[1] == ':') || memchr(relative, '\0', relativeLength) != nullptr)
            return Fail(error, "FBX base-color image path is invalid or absolute");
        const char* slash = strrchr(modelPath, '/');
        const char* backslash = strrchr(modelPath, '\\');
        if (backslash && (!slash || backslash > slash))
            slash = backslash;
        String path;
        if (slash)
        {
            const uint32_t directoryLength = static_cast<uint32_t>(slash - modelPath + 1);
            if (!path.Append(modelPath, directoryLength))
                return Fail(error, "FBX image path allocation failed");
        }
        if (path.Length() > maxResourcePathBytes - static_cast<uint32_t>(relativeLength) || !path.Append(relative, static_cast<uint32_t>(relativeLength)))
            return Fail(error, "FBX image path exceeds the size limit");
        String normalizedPath;
        for (uint32_t i = 0; i < path.Length(); ++i)
        {
            const char value = path.CStr()[i] == '\\' ? '/' : path.CStr()[i];
            if (!normalizedPath.Append(&value, 1))
                return Fail(error, "FBX image path allocation failed");
        }
        image = LoadImagePayload(normalizedPath.CStr(), error);
        if (!image)
        {
            String diagnostic;
            diagnostic.Assign("FBX base-color image could not be loaded: ");
            diagnostic.Append(normalizedPath.CStr());
            diagnostic.Append(" (");
            diagnostic.Append(error.CStr());
            diagnostic.Append(")");
            error.MoveFrom(diagnostic);
            return false;
        }
    }

    uint64_t existingTextureBytes = 0;
    for (uint32_t i = 0; i < model.textures.Count(); ++i)
    {
        const ImageResource* existing = model.textures.At(i);
        if (existing)
            existingTextureBytes += existing->rgba.Count();
    }
    if (existingTextureBytes > maxModelTextureBytes || image->rgba.Count() > maxModelTextureBytes - existingTextureBytes)
    {
        Release(&image->reference);
        return Fail(error, "FBX model exceeds the decoded base-color image size limit");
    }
    if (model.textures.Count() >= static_cast<uint32_t>(INT32_MAX))
    {
        Release(&image->reference);
        return Fail(error, "FBX model has too many base-color images");
    }
    const int32_t modelIndex = static_cast<int32_t>(model.textures.Count());
    if (!model.textures.Append(image))
    {
        Release(&image->reference);
        return Fail(error, "FBX base-color image allocation failed");
    }
    const uint32_t sourceFileIndex = texture->has_file ? texture->file_index : noSourceFileIndex;
    const FbxMaterialContext::TextureEntry entry = { texture, modelIndex, sourceFileIndex };
    if (!context.textures.Append(entry))
        return Fail(error, "FBX texture map allocation failed");
    outputIndex = modelIndex;
    return true;
}

/**
 * Converts FBX diffuse/PBR colors and maps to the model's linear base-color payload.
 */
bool ConvertMaterial(const ufbx_material& source, const char* modelPath, ModelResource& model, FbxMaterialContext& context, ModelMaterial& output, String& error)
{
    output.baseColorFactor[0] = 1.0f;
    output.baseColorFactor[1] = 1.0f;
    output.baseColorFactor[2] = 1.0f;
    output.baseColorFactor[3] = 1.0f;
    output.metallicFactor = 0.0f;
    output.roughnessFactor = 1.0f;
    output.baseColorTextureIndex = -1;

    const bool hasPbrColor = HasPbrBaseColor(source);
    const ufbx_material_map& colorMap = hasPbrColor ? source.pbr.base_color : source.fbx.diffuse_color;
    const ufbx_material_map& factorMap = hasPbrColor ? source.pbr.base_factor : source.fbx.diffuse_factor;
    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (colorMap.has_value)
    {
        color[0] = static_cast<float>(colorMap.value_vec4.x);
        color[1] = static_cast<float>(colorMap.value_vec4.y);
        color[2] = static_cast<float>(colorMap.value_vec4.z);
        if (colorMap.value_components >= 4)
            color[3] = static_cast<float>(colorMap.value_vec4.w);
    }
    const float factor = factorMap.has_value ? static_cast<float>(factorMap.value_real) : 1.0f;
    if (!IsUnit(color[0]) || !IsUnit(color[1]) || !IsUnit(color[2]) || !IsUnit(color[3]) || !IsUnit(factor))
        return Fail(error, "FBX base-color material contains a non-finite or out-of-range value");
    output.baseColorFactor[0] = color[0] * factor;
    output.baseColorFactor[1] = color[1] * factor;
    output.baseColorFactor[2] = color[2] * factor;
    float opacity = color[3];
    if (hasPbrColor && source.pbr.opacity.has_value)
    {
        const float pbrOpacity = static_cast<float>(source.pbr.opacity.value_real);
        if (!IsUnit(pbrOpacity))
            return Fail(error, "FBX PBR opacity is non-finite or out of range");
        opacity *= pbrOpacity;
    }
    else if (source.fbx.transparency_factor.has_value)
    {
        const float transparency = static_cast<float>(source.fbx.transparency_factor.value_real);
        if (!IsUnit(transparency))
            return Fail(error, "FBX transparency factor is non-finite or out of range");
        opacity *= 1.0f - transparency;
    }
    output.baseColorFactor[3] = opacity;

    const ufbx_material_map& alternateMap = hasPbrColor ? source.fbx.diffuse_color : source.pbr.base_color;
    if (alternateMap.texture_enabled && alternateMap.texture && alternateMap.texture != colorMap.texture)
        return Fail(error, "FBX material has conflicting diffuse and PBR base-color images");
    if (factorMap.texture_enabled && factorMap.texture)
        return Fail(error, "FBX base-color factor textures are unsupported");
    if (source.pbr.opacity.texture_enabled && source.pbr.opacity.texture)
        return Fail(error, "FBX base-color opacity textures are unsupported");
    if (source.fbx.transparency_factor.texture_enabled && source.fbx.transparency_factor.texture)
        return Fail(error, "FBX transparency factor textures are unsupported");
    if (source.fbx.transparency_color.texture_enabled && source.fbx.transparency_color.texture)
        return Fail(error, "FBX transparency color textures are unsupported");
    if (colorMap.texture_enabled && colorMap.texture)
    {
        if (!LoadTexture(colorMap.texture, modelPath, model, context, output.baseColorTextureIndex, error))
            return false;
    }
    return true;
}
}

/**
 * Rejects a base-color UV set the geometry importer cannot select for this mesh.
 */
bool ValidateFbxMaterialUvSet(const ufbx_material* material, const ufbx_mesh* mesh, String& error)
{
    error.Clear();
    if (!material)
        return true;
    const bool pbr = HasPbrBaseColor(*material);
    const ufbx_material_map& map = pbr ? material->pbr.base_color : material->fbx.diffuse_color;
    if (!map.texture_enabled || !map.texture)
        return true;
    if (!mesh || !mesh->vertex_uv.exists)
        return Fail(error, "FBX base-color image requires mesh texture coordinates");
    if (!map.texture->uv_set.length)
        return true;
    if (!mesh->uv_sets.count || !mesh->uv_sets.data[0].name.data || mesh->uv_sets.data[0].name.length != map.texture->uv_set.length || memcmp(mesh->uv_sets.data[0].name.data, map.texture->uv_set.data, map.texture->uv_set.length) != 0)
    {
        return Fail(error, "FBX base-color image selects a UV set unsupported by this mesh importer");
    }
    return true;
}

/**
 * Appends or reuses a source material and resolves its optional base-color image.
 */
bool LoadFbxMaterial(const ufbx_material* source, const char* utf8ModelPath, ModelResource& model, FbxMaterialContext& context, int32_t& outputIndex, String& error)
{
    error.Clear();
    if (!source)
    {
        return EnsureDefaultMaterial(model, context, outputIndex, error);
    }
    for (uint32_t i = 0; i < context.materials.Count(); ++i)
    {
        if (context.materials.At(i).source == source)
        {
            outputIndex = context.materials.At(i).modelIndex;
            return true;
        }
    }

    ModelMaterial material{};
    if (!ConvertMaterial(*source, utf8ModelPath, model, context, material, error))
        return false;
    const bool replaceSeededDefault = context.materials.Count() == 0 && context.defaultMaterialIndex == 0 && !context.defaultMaterialUsed && model.materials.Count() == 1 && model.primitives.Count() == 0;
    if (replaceSeededDefault)
    {
        model.materials.At(0) = material;
        context.defaultMaterialIndex = -1;
        outputIndex = 0;
    }
    else
    {
        if (model.materials.Count() >= static_cast<uint32_t>(INT32_MAX) || !model.materials.Append(material))
            return Fail(error, "FBX material allocation failed");
        outputIndex = static_cast<int32_t>(model.materials.Count() - 1);
    }
    const FbxMaterialContext::MaterialEntry entry = { source, outputIndex };
    if (!context.materials.Append(entry))
    {
        model.materials.RemoveAt(model.materials.Count() - 1);
        return Fail(error, "FBX material map allocation failed");
    }
    return true;
}

} // namespace gk::detail
