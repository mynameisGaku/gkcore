#include "FbxLoader.h"
#include "FbxGeometry.h"
#include "animation/FbxAnimation.h"
#include "animation/ModelPose.h"
#include "../foundation/Array.h"
#include "../foundation/Memory.h"
#include <limits.h>
#include <string.h>

/**
 * Bounded FBX import routines and their temporary traversal records.
 */
namespace gk::detail
{

/**
 * Private parser adapters and the iterative hierarchy walk.
 */
namespace
{

/**
 * Stores one pending node and its hierarchy depth for an iterative scene walk.
 */
struct PendingNode
{
    const ufbx_node* node;
    uint32_t depth;
};

/**
 * Adapts ufbx allocation to the foundation allocator.
 */
void* FbxAllocate(void*, size_t size)
{
    return Allocate(size);
}

/**
 * Adapts ufbx reallocation to the foundation allocator.
 */
void* FbxReallocate(void*, void* memory, size_t, size_t newSize)
{
    return Reallocate(memory, newSize);
}

/**
 * Releases memory allocated through the foundation allocator.
 */
void FbxDeallocate(void*, void* memory, size_t)
{
    Deallocate(memory);
}

/**
 * Formats a bounded parser diagnostic without allocating temporary strings.
 */
void SetFbxError(const ufbx_error& source, String& error)
{
    char message[1024];
    const size_t length = ufbx_format_error(message, sizeof(message), &source);
    message[sizeof(message) - 1] = '\0';
    if (length && length < sizeof(message))
        error.Assign(message);
    else
        error.Assign("FBX parsing failed");
}

/**
 * Rejects unsupported mesh deformation before static vertices are copied.
 */
bool HasUnsupportedDeformation(const ufbx_mesh* mesh)
{
    return mesh->cache_deformers.count != 0;
}

/**
 * 読み込んだsegmentに初期姿勢で評価すべきskinまたはmorphがあるか調べる。
 */
bool HasVertexDeformers(const ufbx_scene* scene, const Array<FFbxGeometrySegment>& segments)
{
    for (uint32_t segmentIndex = 0; segmentIndex < segments.Count(); ++segmentIndex)
    {
        const FFbxGeometrySegment& segment = segments.At(segmentIndex);
        if (segment.nodeTypedId >= scene->nodes.count)
        {
            return false;
        }
        const ufbx_node* node = scene->nodes.data[segment.nodeTypedId];
        if (node && node->mesh && (node->mesh->skin_deformers.count != 0 || node->mesh->blend_deformers.count != 0))
        {
            return true;
        }
    }
    return false;
}

/**
 * Appends all mesh nodes in parent-before-child order within the 64-level bound.
 */
bool AppendSceneGeometry(const ufbx_scene* scene, const char* utf8Path, ModelResource& model, FbxMaterialContext& materials, Array<FFbxGeometrySegment>& segments, uint32_t& degenerateFaces, String& error)
{
    Array<PendingNode> pending;
    PendingNode root = { scene->root_node, 0 };
    if (!pending.Append(root))
    {
        error.Assign("FBX traversal allocation failed");
        return false;
    }
    uint32_t cursor = 0;
    uint32_t visited = 0;
    while (cursor < pending.Count())
    {
        const PendingNode current = pending.At(cursor++);
        if (!current.node || current.depth > 64 || ++visited > 1000000u)
        {
            error.Assign("FBX node hierarchy is invalid or exceeds its limit");
            return false;
        }
        if (current.node->mesh)
        {
            if (HasUnsupportedDeformation(current.node->mesh))
            {
                error.Assign("FBX geometry caches are unsupported");
                return false;
            }
            // 平坦化した頂点列と元meshの対応を受け取る。
            FFbxGeometrySegment segment{};
            if (!AppendFbxNodeGeometry(current.node, utf8Path, model, materials, &degenerateFaces, &segment, error))
                return false;
            if (!segments.Append(segment))
            {
                error.Assign("FBX geometry segment allocation failed");
                return false;
            }
        }
        const ufbx_node_list& children = current.node->children;
        for (size_t i = 0; i < children.count; ++i)
        {
            if (current.depth == 64)
            {
                error.Assign("FBX node hierarchy exceeds 64 levels");
                return false;
            }
            PendingNode child = { children.data[i], current.depth + 1 };
            if (!pending.Append(child))
            {
                error.Assign("FBX traversal allocation failed");
                return false;
            }
        }
    }
    return true;
}

} // namespace

/**
 * Parses bounded FBX scene data and appends its supported static meshes.
 */
bool LoadFbxPayload(const uint8_t* bytes, uint32_t size, const char* utf8Path, ModelResource& model, String& error)
{
    if (!bytes || size == 0 || size > 64u * 1024u * 1024u)
    {
        error.Assign("FBX file is empty or exceeds 64 MiB");
        return false;
    }

    ufbx_load_opts options = {};
    ufbx_allocator allocator = {};
    allocator.alloc_fn = FbxAllocate;
    allocator.realloc_fn = FbxReallocate;
    allocator.free_fn = FbxDeallocate;
    options.temp_allocator.allocator = allocator;
    options.temp_allocator.memory_limit = 64u * 1024u * 1024u;
    options.temp_allocator.allocation_limit = 1000000u;
    options.result_allocator.allocator = allocator;
    options.result_allocator.memory_limit = 128u * 1024u * 1024u;
    options.result_allocator.allocation_limit = 1000000u;
    options.file_format = UFBX_FILE_FORMAT_FBX;
    options.file_format_lookahead = 0;
    options.node_depth_limit = 64;
    options.load_external_files = false;
    options.ignore_animation = false;
    options.skip_skin_vertices = false;
    options.generate_missing_normals = true;
    options.retain_dom = true;
    options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_PRESERVE;
    options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    options.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.force_single_thread_ascii_parsing = true;
    options.strict = true;
    options.index_error_handling = UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
    if (utf8Path)
    {
        options.filename.data = utf8Path;
        options.filename.length = SIZE_MAX;
    }

    ufbx_error parseError = {};
    ufbx_scene* scene = ufbx_load_memory(bytes, size, &options, &parseError);
    if (!scene)
    {
        SetFbxError(parseError, error);
        return false;
    }
    FbxMaterialContext materials;
    // scene保持sourceへ渡すmesh instance対応。
    Array<FFbxGeometrySegment> segments;
    uint32_t degenerateFaces = 0;
    const bool success = AppendSceneGeometry(scene, utf8Path, model, materials, segments, degenerateFaces, error);
    if (!success)
    {
        ufbx_free_scene(scene);
        return false;
    }
    if (!model.indices.Count())
    {
        ufbx_free_scene(scene);
        error.Assign("FBX contains no supported static triangles");
        return false;
    }
    const bool hasVertexDeformers = HasVertexDeformers(scene, segments);
    // 元sceneの所有権を移してpose評価sourceを作る。
    gk::model::AModelAnimationSource* source = gk::model::CreateFbxAnimationSource(scene, model, segments, error);
    if (!source)
        return false;
    if (hasVertexDeformers)
    {
        gk::model::animation::FModelPose restPose;
        if (!gk::model::animation::InitializeModelPose(source->Skeleton(), restPose, error) || !source->Deform(restPose, model, error))
        {
            delete source;
            return false;
        }
    }
    model.animation = gk::model::CreateModelAnimationAsset(source, error);
    if (!model.animation)
        return false;
    static_cast<void>(degenerateFaces);
    error.Clear();
    return true;
}

} // namespace gk::detail
