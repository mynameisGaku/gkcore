#include "model/ModelLoader.h"
#include "model/FbxMaterial.h"

#include <ufbx.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef GKCORE_TEST_SOURCE_DIR
#define GKCORE_TEST_SOURCE_DIR "."
#endif

namespace gk::tests
{
/**
 * File fixtures and material-policy assertions for static FBX ingestion.
 */
namespace
{

/**
 * Saves one test failure message and returns false for direct assertion chaining.
 */
bool Fail(String& failure, const char* message)
{
    failure.Assign(message);
    return false;
}

/**
 * Compares parsed scalar values with a small importer tolerance.
 */
bool Near(float left, float right, float tolerance = 0.0002f)
{
    return fabsf(left - right) <= tolerance;
}

/**
 * Loads one source fixture and checks geometry, transforms, materials, and its image.
 */
bool CheckLoadedModel(const char* relativePath, bool expectEmbedded, String& failure)
{
    char path[1024];
    const int length = snprintf(path, sizeof(path), "%s/tests/assets/models/%s", GKCORE_TEST_SOURCE_DIR, relativePath);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(path))
        return Fail(failure, "FBX fixture path exceeded the test buffer");

    String error;
    detail::ModelResource* model = detail::LoadModelPayload(path, error);
    if (!model)
    {
        failure.Assign("valid FBX fixture was rejected: ");
        failure.Append(relativePath);
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }
    bool valid = true;
    if (model->vertices.Count() < 4 || model->indices.Count() != 6 || model->primitives.Count() != 2 || model->materials.Count() < 2 || model->textures.Count() != 1)
    {
        failure.Assign("FBX quad geometry, material groups, or deduplicated image payload is incomplete");
        failure.Append(" (vertices=");
        failure.AppendUnsigned(model->vertices.Count());
        failure.Append(", indices=");
        failure.AppendUnsigned(model->indices.Count());
        failure.Append(", primitives=");
        failure.AppendUnsigned(model->primitives.Count());
        failure.Append(", materials=");
        failure.AppendUnsigned(model->materials.Count());
        failure.Append(", textures=");
        failure.AppendUnsigned(model->textures.Count());
        failure.Append(")");
        valid = false;
    }
    if (valid)
    {
        float minimumX = model->vertices.At(0).position[0];
        float maximumX = minimumX;
        float minimumY = model->vertices.At(0).position[1];
        float maximumY = minimumY;
        bool sawFlippedV = false;
        for (uint32_t i = 0; i < model->vertices.Count(); ++i)
        {
            const detail::ModelVertex& vertex = model->vertices.At(i);
            if (!isfinite(vertex.position[0]) || !isfinite(vertex.position[1]) || !isfinite(vertex.position[2]) || !isfinite(vertex.uv[0]) || !isfinite(vertex.uv[1]))
            {
                failure.Assign("FBX emitted non-finite transformed vertex data");
                valid = false;
                break;
            }
            if (vertex.position[0] < minimumX)
                minimumX = vertex.position[0];
            if (vertex.position[0] > maximumX)
                maximumX = vertex.position[0];
            if (vertex.position[1] < minimumY)
                minimumY = vertex.position[1];
            if (vertex.position[1] > maximumY)
                maximumY = vertex.position[1];
            if (Near(vertex.uv[1], 1.0f))
                sawFlippedV = true;
            if (!Near(vertex.normal[0], 0.0f) || !Near(vertex.normal[1], 0.0f) || !Near(vertex.normal[2], 1.0f))
            {
                failure.Assign("FBX normals were not preserved through model transforms");
                valid = false;
                break;
            }
        }
        if (valid && (!Near(minimumX, 0.13f) || !Near(maximumX, 0.15f) || !Near(minimumY, 0.04f) || !Near(maximumY, 0.05f) || !sawFlippedV))
        {
            failure.Assign("FBX hierarchy transforms, scale, or native V coordinate conversion failed");
            char diagnostic[160];
            snprintf(diagnostic, sizeof(diagnostic), " (x=%.6f..%.6f, y=%.6f..%.6f, flippedV=%d)", minimumX, maximumX, minimumY, maximumY, sawFlippedV ? 1 : 0);
            failure.Append(diagnostic);
            valid = false;
        }
    }
    if (valid)
    {
        const detail::ModelPrimitive& first = model->primitives.At(0);
        const detail::ModelPrimitive& second = model->primitives.At(1);
        if (first.indexCount != 3 || second.indexCount != 3 || first.materialIndex < 0 || second.materialIndex < 0 || first.materialIndex == second.materialIndex)
        {
            failure.Assign("FBX polygon material slots were not kept on separate primitives");
            valid = false;
        }
    }
    if (valid)
    {
        const float expectedWarmRed = 0.5f * 0.8f;
        bool foundWarm = false;
        bool foundCool = false;
        for (uint32_t i = 0; i < model->materials.Count(); ++i)
        {
            const detail::ModelMaterial& material = model->materials.At(i);
            if (Near(material.baseColorFactor[0], expectedWarmRed, 0.002f))
            {
                foundWarm = Near(material.baseColorFactor[3], 0.75f) && material.baseColorTextureIndex == 0;
            }
            else if (Near(material.baseColorFactor[0], 0.2f, 0.002f))
            {
                foundCool = material.baseColorTextureIndex == (expectEmbedded ? -1 : 0);
            }
        }
        if (!foundWarm || !foundCool)
        {
            failure.Assign("FBX diffuse factor conversion, opacity, or material texture mapping failed");
            failure.Append(" (warm=");
            failure.Append(foundWarm ? "yes" : "no");
            failure.Append(", cool=");
            failure.Append(foundCool ? "yes" : "no");
            failure.Append(", materials=");
            for (uint32_t i = 0; i < model->materials.Count(); ++i)
            {
                char diagnostic[96];
                const detail::ModelMaterial& material = model->materials.At(i);
                snprintf(diagnostic, sizeof(diagnostic), "%s%.5g/%.5g/%.5g/tex%d", i ? ";" : "", material.baseColorFactor[0], material.baseColorFactor[3], material.roughnessFactor, material.baseColorTextureIndex);
                failure.Append(diagnostic);
            }
            failure.Append(")");
            valid = false;
        }
    }
    if (valid)
    {
        detail::ImageResource* texture = model->textures.At(0);
        if (!texture || texture->width != 1 || texture->height != 1 || texture->rgba.Count() != 4)
        {
            failure.Assign(expectEmbedded ? "embedded FBX PNG was not decoded" : "relative Unicode FBX PNG was not resolved and decoded");
            valid = false;
        }
    }
    Release(&model->reference);
    return valid;
}

/**
 * Confirms malformed or unsupported input fails with a diagnostic.
 */
bool CheckRejected(const char* relativePath, String& failure)
{
    char path[1024];
    const int length = snprintf(path, sizeof(path), "%s/tests/assets/models/%s", GKCORE_TEST_SOURCE_DIR, relativePath);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(path))
        return Fail(failure, "invalid FBX fixture path exceeded the test buffer");
    String error;
    detail::ModelResource* model = detail::LoadModelPayload(path, error);
    if (model)
    {
        Release(&model->reference);
        failure.Assign("invalid or unsupported FBX fixture was accepted: ");
        failure.Append(relativePath);
        return false;
    }
    if (error.Empty())
    {
        failure.Assign("rejected FBX fixture had no diagnostic: ");
        failure.Append(relativePath);
        return false;
    }
    return true;
}

/**
 * Verifies null and explicit materials retain distinct payload slots in either order.
 */
bool CheckDefaultMaterialOrder(String& failure)
{
    ufbx_material explicitMaterial{};
    explicitMaterial.fbx.diffuse_color.has_value = true;
    explicitMaterial.fbx.diffuse_color.value_components = 3;
    explicitMaterial.fbx.diffuse_color.value_vec4.x = 0.5;
    explicitMaterial.fbx.diffuse_color.value_vec4.y = 0.6;
    explicitMaterial.fbx.diffuse_color.value_vec4.z = 0.7;
    explicitMaterial.fbx.diffuse_color.value_vec4.w = 1.0;
    explicitMaterial.fbx.diffuse_factor.has_value = true;
    explicitMaterial.fbx.diffuse_factor.value_real = 0.8;

    for (uint32_t explicitFirst = 0; explicitFirst < 2; ++explicitFirst)
    {
        detail::ModelResource* model = detail::CreateModelResource();
        if (!model)
            return Fail(failure, "could not allocate material-order model payload");
        detail::FbxMaterialContext context;
        int32_t first = -1;
        int32_t second = -1;
        String error;
        const bool firstSucceeded = explicitFirst ? detail::LoadFbxMaterial(&explicitMaterial, nullptr, *model, context, first, error) : detail::LoadFbxMaterial(nullptr, nullptr, *model, context, first, error);
        const bool secondSucceeded = explicitFirst ? detail::LoadFbxMaterial(nullptr, nullptr, *model, context, second, error) : detail::LoadFbxMaterial(&explicitMaterial, nullptr, *model, context, second, error);
        bool valid = firstSucceeded && secondSucceeded && first != second && model->materials.Count() == 2;
        const int32_t whiteIndex = explicitFirst ? second : first;
        const int32_t colorIndex = explicitFirst ? first : second;
        if (valid)
        {
            const detail::ModelMaterial& white = model->materials.At(static_cast<uint32_t>(whiteIndex));
            const detail::ModelMaterial& color = model->materials.At(static_cast<uint32_t>(colorIndex));
            valid = Near(white.baseColorFactor[0], 1.0f) && Near(white.baseColorFactor[1], 1.0f) && Near(white.baseColorFactor[2], 1.0f) && Near(white.baseColorFactor[3], 1.0f) && white.baseColorTextureIndex == -1 && Near(color.baseColorFactor[0], 0.4f) && Near(color.baseColorFactor[3], 1.0f);
        }
        Release(&model->reference);
        if (!valid)
            return Fail(failure, explicitFirst ? "explicit material then null slot did not retain independent white fallback" : "null slot then explicit material overwrote the white fallback");
    }
    return true;
}

}

/**
 * Runs public loader cases and the focused material-slot ordering regression.
 */
bool FbxLoadingContract(String& failure)
{
    if (!CheckLoadedModel("gkcore_ascii.fbx", false, failure) || !CheckLoadedModel("gkcore_binary.fbx", true, failure) || !CheckLoadedModel("gkcore_ascii_upper.FBX", false, failure) || !CheckLoadedModel("gkcore_ascii_magic.bin", false, failure) || !CheckLoadedModel("gkcore_binary_magic.bin", true, failure))
        return false;

    const char* rejected[] = { "gkcore_malformed_ascii.fbx", "gkcore_truncated_binary.fbx", "gkcore_nonfinite_ascii.fbx", "gkcore_skin_ascii.fbx", "gkcore_missing_texture.fbx", "gkcore_uv_transform.fbx", "gkcore_legacy_uv_transform.fbx", "gkcore_uv_set_mismatch.fbx", "gkcore_layered_texture.fbx", "gkcore_obj_renamed.fbx" };
    for (uint32_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i)
        if (!CheckRejected(rejected[i], failure))
            return false;
    if (!CheckDefaultMaterialOrder(failure))
        return false;
    return true;
}

}

/**
 * Runs the FBX contract executable and reports its first assertion failure.
 */
int main()
{
    gk::String failure;
    if (gk::tests::FbxLoadingContract(failure))
        return 0;
    fprintf(stderr, "%s\n", failure.CStr());
    return 1;
}
