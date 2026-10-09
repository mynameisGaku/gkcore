#include "model/FbxGeometry.h"
#include "model/FbxMaterial.h"
#include "foundation/Memory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * Test doubles for the resource-owned FBX material operations.
 */
namespace gk::detail
{
/**
 * Maps fixture material identifiers to stable local slots without decoding images.
 */
bool LoadFbxMaterial(const ufbx_material* source, const char*, ModelResource& model, FbxMaterialContext&, int32_t& outputIndex, String&)
{
    if (!source)
    {
        outputIndex = 0;
        return true;
    }
    outputIndex = source->element.element_id == 1 ? 1 : 2;
    while (model.materials.Count() <= static_cast<uint32_t>(outputIndex))
    {
        ModelMaterial material{};
        material.baseColorFactor[0] = material.baseColorFactor[1] = material.baseColorFactor[2] = material.baseColorFactor[3] = 1.0f;
        material.baseColorTextureIndex = -1;
        if (!model.materials.Append(material))
            return false;
    }
    return true;
}
/**
 * Accepts fixture UV bindings so tests isolate geometry conversion behavior.
 */
bool ValidateFbxMaterialUvSet(const ufbx_material*, const ufbx_mesh*, String&)
{
    return true;
}
}

/**
 * Portable checks for FBX mesh conversion invariants.
 */
namespace
{
int failures = 0;

/**
 * Creates a ufbx vector value for compact synthetic mesh fixtures.
 */
ufbx_vec3 Vec3(ufbx_real x, ufbx_real y, ufbx_real z)
{
    ufbx_vec3 value{};
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}
/**
 * Creates a ufbx UV value for compact synthetic mesh fixtures.
 */
ufbx_vec2 Vec2(ufbx_real x, ufbx_real y)
{
    ufbx_vec2 value{};
    value.x = x;
    value.y = y;
    return value;
}

/**
 * Records a failed assertion while allowing the remaining checks to run.
 */
void Check(bool value, const char* message)
{
    if (!value)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

/**
 * Checks triangulation, face order, material runs, transforms, UV origin, and degenerates.
 */
void TestFaceOrderTransformsAndUvOrigin()
{
    using namespace gk;
    using namespace gk::detail;
    const ufbx_vec3 positions[] = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0), Vec3(0, 1, 0), Vec3(2, 0, 0), Vec3(3, 0, 0), Vec3(2, 1, 0), Vec3(2, 2, 0), Vec3(2, 2, 0), Vec3(3, 2, 0) };
    const ufbx_vec3 normals[] = { Vec3(0, 0, 2), Vec3(0, 0, 2), Vec3(0, 0, 2), Vec3(0, 0, 2), Vec3(0, 0, 2), Vec3(0, 0, 2), Vec3(0, 0, 2), Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(0, 0, 0) };
    const ufbx_vec2 uvs[] = { Vec2(0.1, 0.2), Vec2(0.3, 0.4), Vec2(0.5, 0.6), Vec2(0.7, 0.8), Vec2(0.9, 0.1), Vec2(0.2, 0.3), Vec2(0.4, 0.5), Vec2(0.6, 0.7), Vec2(0.6, 0.7), Vec2(0.8, 0.9) };
    const uint32_t vertexIndices[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    const ufbx_face faces[] = { { 0, 4 }, { 4, 3 }, { 7, 3 } };
    const uint32_t faceMaterials[] = { 0, 1, 1 };
    ufbx_mesh mesh{};
    mesh.num_indices = 10;
    mesh.num_faces = 3;
    mesh.num_triangles = 4;
    mesh.max_face_triangles = 2;
    mesh.faces = { const_cast<ufbx_face*>(faces), 3 };
    mesh.face_material = { const_cast<uint32_t*>(faceMaterials), 3 };
    mesh.vertex_indices = { const_cast<uint32_t*>(vertexIndices), 10 };
    mesh.vertex_position.exists = true;
    mesh.vertex_position.values = { const_cast<ufbx_vec3*>(positions), 10 };
    mesh.vertex_position.indices = { const_cast<uint32_t*>(vertexIndices), 10 };
    mesh.vertex_normal.exists = true;
    mesh.vertex_normal.values = { const_cast<ufbx_vec3*>(normals), 10 };
    mesh.vertex_normal.indices = { const_cast<uint32_t*>(vertexIndices), 10 };
    mesh.vertex_uv.exists = true;
    mesh.vertex_uv.values = { const_cast<ufbx_vec2*>(uvs), 10 };
    mesh.vertex_uv.indices = { const_cast<uint32_t*>(vertexIndices), 10 };
    ufbx_material sourceMaterials[2]{};
    sourceMaterials[0].element.element_id = 1;
    sourceMaterials[1].element.element_id = 2;
    ufbx_node node{};
    node.mesh = &mesh;
    ufbx_material* materialPointers[] = { &sourceMaterials[0], &sourceMaterials[1] };
    node.materials = { materialPointers, 2 };
    node.geometry_to_world = {};
    node.geometry_to_world.m00 = 1;
    node.geometry_to_world.m11 = 1;
    node.geometry_to_world.m22 = 1;
    node.geometry_to_world.m00 = -2;
    node.geometry_to_world.m11 = 3;
    node.geometry_to_world.m22 = 1;
    node.geometry_to_world.m03 = 10;

    ModelResource model{};
    ModelMaterial defaultMaterial{};
    defaultMaterial.baseColorFactor[0] = defaultMaterial.baseColorFactor[1] = defaultMaterial.baseColorFactor[2] = defaultMaterial.baseColorFactor[3] = 1.0f;
    defaultMaterial.baseColorTextureIndex = -1;
    model.materials.Append(defaultMaterial);
    FbxMaterialContext context{};
    String error;
    uint32_t degenerateFaces = 0;
    const bool ok = AppendFbxNodeGeometry(&node, "fixture.fbx", model, context, &degenerateFaces, error);
    Check(ok, "valid mixed triangle/quad FBX node is accepted");
    Check(error.Empty(), "valid conversion leaves no error");
    Check(model.vertices.Count() == 10, "mesh index vertices are emitted once");
    Check(model.indices.Count() == 9, "quad and triangle are triangulated in order");
    Check(model.primitives.Count() == 2, "contiguous material changes split primitive runs");
    if (model.primitives.Count() == 2)
    {
        Check(model.primitives.At(0).firstIndex == 0 && model.primitives.At(0).indexCount == 6 && model.primitives.At(0).materialIndex == 1, "first primitive retains first-face order and material");
        Check(model.primitives.At(1).firstIndex == 6 && model.primitives.At(1).indexCount == 3 && model.primitives.At(1).materialIndex == 2, "second primitive retains later-face order and material");
    }
    if (model.vertices.Count() == 10)
    {
        const ModelVertex& v = model.vertices.At(0);
        Check(fabsf(v.position[0] - 10.0f) < 1e-5f, "geometry matrix transforms the position");
        Check(fabsf(v.normal[2] - 1.0f) < 1e-5f, "inverse-transpose normals are normalized");
        Check(fabsf(v.uv[1] - 0.8f) < 1e-5f, "FBX bottom-origin UV is flipped to top-left");
    }
    if (model.indices.Count() >= 3)
    {
        const ModelVertex& a = model.vertices.At(model.indices.At(0));
        const ModelVertex& b = model.vertices.At(model.indices.At(1));
        const ModelVertex& c = model.vertices.At(model.indices.At(2));
        const float crossZ = (b.position[0] - a.position[0]) * (c.position[1] - a.position[1]) - (b.position[1] - a.position[1]) * (c.position[0] - a.position[0]);
        Check(crossZ > 0, "negative determinant preserves front-face winding");
    }
    Check(degenerateFaces == 1, "degenerate source faces are counted while valid triangles remain");
}

/**
 * Verifies malformed attribute indices fail with a useful diagnostic.
 */
void TestMalformedAttributeIndexFailsWithoutOverflow()
{
    using namespace gk;
    using namespace gk::detail;
    const ufbx_vec3 positionValues[] = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0) };
    const ufbx_vec3 normalValues[] = { Vec3(0, 0, 1) };
    const uint32_t badAttributeIndices[] = { 0, 1, 9 };
    const uint32_t validNormalIndices[] = { 0, 0, 0 };
    const ufbx_face face[] = { { 0, 3 } };
    ufbx_mesh mesh{};
    mesh.num_indices = 3;
    mesh.num_faces = 1;
    mesh.max_face_triangles = 1;
    mesh.faces = { const_cast<ufbx_face*>(face), 1 };
    mesh.vertex_position.exists = true;
    mesh.vertex_position.values = { const_cast<ufbx_vec3*>(positionValues), 3 };
    mesh.vertex_position.indices = { const_cast<uint32_t*>(badAttributeIndices), 3 };
    mesh.vertex_normal.exists = true;
    mesh.vertex_normal.values = { const_cast<ufbx_vec3*>(normalValues), 1 };
    mesh.vertex_normal.indices = { const_cast<uint32_t*>(validNormalIndices), 3 };
    ufbx_node node{};
    node.mesh = &mesh;
    node.geometry_to_world = {};
    node.geometry_to_world.m00 = 1;
    node.geometry_to_world.m11 = 1;
    node.geometry_to_world.m22 = 1;
    ModelResource model{};
    FbxMaterialContext context{};
    String error;
    uint32_t degenerateFaces = 0;
    Check(!AppendFbxNodeGeometry(&node, "fixture.fbx", model, context, &degenerateFaces, error), "out-of-range attribute references are rejected");
    Check(strstr(error.CStr(), "invalid vertex attribute") != nullptr, "out-of-range index is diagnosed after valid required normals are present");
}
}

/**
 * Runs the FBX geometry contract checks and returns a failing process status if needed.
 */
int main()
{
    TestFaceOrderTransformsAndUvOrigin();
    TestMalformedAttributeIndexFailsWithoutOverflow();
    if (failures)
        fprintf(stderr, "%d FBX geometry test(s) failed\n", failures);
    return failures ? 1 : 0;
}
