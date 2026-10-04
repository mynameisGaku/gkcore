#include "../src/render/ModelDrawPlan.h"

#include <stdio.h>

namespace {

using namespace gk;
using namespace gk::render;

bool Check(bool value, const char* label) {
    if (value) return true;
    fprintf(stderr, "model draw plan test failed: %s\n", label);
    return false;
}

detail::ModelVertex Vertex(float x, float y, float z) {
    detail::ModelVertex result{};
    result.position[0] = x;
    result.position[1] = y;
    result.position[2] = z;
    return result;
}

void AddTriangle(detail::ModelResource& model, uint32_t base) {
    model.vertices.Append(Vertex(static_cast<float>(base), 0.0f, 0.0f));
    model.vertices.Append(Vertex(static_cast<float>(base + 1), 0.0f, 0.0f));
    model.vertices.Append(Vertex(static_cast<float>(base), 1.0f, 0.0f));
    model.indices.Append(base);
    model.indices.Append(base + 1);
    model.indices.Append(base + 2);
}

void SetWhite(detail::ModelMaterial& material) {
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.metallicFactor = 1.0f;
    material.roughnessFactor = 1.0f;
    material.baseColorTextureIndex = -1;
}

bool ExpectPreservedFailure(const detail::ModelResource& model, ModelDrawPlan& plan,
                            String& error, const char* label) {
    if (!Check(!BuildModelDrawPlan(model, plan, error), label)) return false;
    return Check(plan.parts.Count() == 1 && plan.parts.At(0).firstIndex == 17,
                 "failed validation preserves the previous draw plan");
}

bool TestPrimitiveRangesAndDefaultMaterial() {
    detail::ModelResource model{};
    AddTriangle(model, 0);
    AddTriangle(model, 3);
    model.primitives.Append({0, 3, -1});
    model.primitives.Append({3, 3, -1});
    ModelDrawPlan plan;
    String error;
    if (!Check(BuildModelDrawPlan(model, plan, error), "valid primitive plan builds")) return false;
    if (!Check(plan.parts.Count() == 2, "one plan entry is emitted per primitive")) return false;
    if (!Check(plan.parts.At(0).firstIndex == 0 && plan.parts.At(0).indexCount == 3 &&
               plan.parts.At(1).firstIndex == 3 && plan.parts.At(1).indexCount == 3,
               "primitive index ranges and order are preserved")) return false;
    if (!Check(plan.parts.At(0).materialIndex == -1 && plan.parts.At(0).textureIndex == -1 &&
               plan.parts.At(0).baseColorFactor[0] == 1.0f &&
               plan.parts.At(0).baseColorFactor[3] == 1.0f,
               "missing material uses opaque linear white without a texture")) return false;
    return true;
}

bool TestMaterialFactorsAndTextureIndex() {
    detail::ModelResource model{};
    AddTriangle(model, 0);
    detail::ImageResource image{};
    model.textures.Append(&image);
    detail::ModelMaterial material{};
    material.baseColorFactor[0] = 0.25f;
    material.baseColorFactor[1] = 0.5f;
    material.baseColorFactor[2] = 0.75f;
    material.baseColorFactor[3] = 0.4f;
    material.metallicFactor = 0.2f;
    material.roughnessFactor = 0.8f;
    material.baseColorTextureIndex = 0;
    model.materials.Append(material);
    model.primitives.Append({0, 3, 0});
    ModelDrawPlan plan;
    String error;
    if (!Check(BuildModelDrawPlan(model, plan, error), "valid material plan builds")) return false;
    const ModelPartPlan& part = plan.parts.At(0);
    if (!Check(part.materialIndex == 0 && part.textureIndex == 0,
               "material texture index is retained")) return false;
    if (!Check(part.baseColorFactor[0] == 0.25f && part.baseColorFactor[1] == 0.5f &&
               part.baseColorFactor[2] == 0.75f && part.baseColorFactor[3] == 0.4f,
               "linear base-color factor is copied without sRGB conversion")) return false;
    if (!Check(part.metallicFactor == 0.2f && part.roughnessFactor == 0.8f,
               "metallic and roughness factors are copied into the draw plan")) return false;
    return true;
}

bool TestInvalidRangesMaterialsTexturesAndFactorsPreserveOutput() {
    detail::ModelResource model{};
    AddTriangle(model, 0);
    model.primitives.Append({1, 3, -1});
    ModelDrawPlan plan;
    plan.parts.Append({17, 18, -1, -1, {0.1f, 0.2f, 0.3f, 0.4f}, 0.0f, 1.0f});
    String error;
    if (!ExpectPreservedFailure(model, plan, error, "out-of-range index range is rejected")) return false;

    model.primitives.Clear();
    model.primitives.Append({0, 2, -1});
    if (!ExpectPreservedFailure(model, plan, error, "non-triangle index range is rejected")) return false;
    model.primitives.Clear();
    model.primitives.Append({0, 3, 4});
    if (!ExpectPreservedFailure(model, plan, error, "invalid material index is rejected")) return false;

    detail::ModelMaterial material{};
    SetWhite(material);
    material.baseColorTextureIndex = 1;
    model.materials.Append(material);
    model.primitives.Clear();
    model.primitives.Append({0, 3, 0});
    if (!ExpectPreservedFailure(model, plan, error, "invalid texture index is rejected")) return false;

    model.materials.At(0).baseColorTextureIndex = -1;
    model.materials.At(0).baseColorFactor[1] = 1.2f;
    if (!ExpectPreservedFailure(model, plan, error, "out-of-range base-color factor is rejected")) return false;
    model.materials.At(0).baseColorFactor[1] = 1.0f;
    model.materials.At(0).roughnessFactor = 0.0f / 0.0f;
    if (!ExpectPreservedFailure(model, plan, error, "non-finite material factor is rejected")) return false;
    model.materials.At(0).roughnessFactor = 1.0f;
    model.materials.At(0).metallicFactor = -0.1f;
    if (!ExpectPreservedFailure(model, plan, error, "negative metallic factor is rejected")) return false;
    model.materials.At(0).metallicFactor = 1.0f;
    model.materials.At(0).baseColorFactor[2] = 0.0f / 0.0f;
    if (!ExpectPreservedFailure(model, plan, error, "non-finite base-color factor is rejected")) return false;
    model.materials.At(0).baseColorFactor[2] = 1.0f;
    model.materials.At(0).metallicFactor = 1.01f;
    if (!ExpectPreservedFailure(model, plan, error, "out-of-range metallic factor is rejected")) return false;
    model.materials.At(0).metallicFactor = 0.5f;
    model.materials.At(0).roughnessFactor = -0.01f;
    if (!ExpectPreservedFailure(model, plan, error, "negative roughness factor is rejected")) return false;
    model.materials.At(0).roughnessFactor = 0.5f;
    model.materials.At(0).metallicFactor = 0.0f / 0.0f;
    if (!ExpectPreservedFailure(model, plan, error, "non-finite metallic factor is rejected")) return false;
    model.materials.At(0).metallicFactor = 0.5f;
    model.materials.At(0).roughnessFactor = 1.0f / 0.0f;
    if (!ExpectPreservedFailure(model, plan, error, "infinite roughness factor is rejected")) return false;
    return true;
}

bool TestDefaultMetallicRoughness() {
    detail::ModelResource model{};
    AddTriangle(model, 0);
    model.primitives.Append({0, 3, -1});
    ModelDrawPlan plan;
    String error;
    if (!Check(BuildModelDrawPlan(model, plan, error), "default material plan builds")) return false;
    return Check(plan.parts.At(0).metallicFactor == 0.0f &&
                 plan.parts.At(0).roughnessFactor == 1.0f,
                 "missing material defaults to dielectric and fully rough");
}

} // namespace

int main() {
    return TestPrimitiveRangesAndDefaultMaterial() && TestMaterialFactorsAndTextureIndex() &&
           TestInvalidRangesMaterialsTexturesAndFactorsPreserveOutput() &&
           TestDefaultMetallicRoughness() ? 0 : 1;
}
