#include "GlbLoader.h"
#include "../image/ImageLoader.h"
#include "../foundation/Memory.h"
#include "../../third_party/cgltf/cgltf.h"
#include <math.h>
#include <string.h>

/**
 * 検証済みglTFデータを保持可能なモデルpayloadへ変換する。
 */
namespace gk::detail
{
/**
 * GLB読み込み中だけ使う境界付きの補助処理。
 */
namespace
{
// 読み込めるGLB全体と画像データの最大byte数。
const uint32_t maxModelFileBytes = 64u * 1024u * 1024u;
// 出力モデルへ追加できる頂点数の上限。
const uint32_t maxOutputVertices = 4000000u;
// 出力モデルへ追加できるindex数の上限。
const uint32_t maxOutputIndices = 6000000u;
// 検査対象にできるsource要素数の上限。
const uint32_t maxSourceValues = 2000000u;
// texture mapに未割当を示すindex値。
const uint32_t missingIndex = 0xffffffffu;
/**
 * 変換後の頂点属性へ保存する前に有限値か確かめる。
 */
bool IsFinite(float value)
{
    return isfinite(value) != 0;
}

/**
 * 列優先のworld行列を位置へ適用する。
 */
void TransformPosition(const float matrix[16], const float source[3], float output[3])
{
    output[0] = matrix[0] * source[0] + matrix[4] * source[1] + matrix[8] * source[2] + matrix[12];
    output[1] = matrix[1] * source[0] + matrix[5] * source[1] + matrix[9] * source[2] + matrix[13];
    output[2] = matrix[2] * source[0] + matrix[6] * source[1] + matrix[10] * source[2] + matrix[14];
}
/**
 * 線形行列の逆転置を法線へ適用し、特異変換では元の値を保つ。
 */
void TransformNormal(const float matrix[16], const float source[3], float output[3])
{
    // 逆転置計算に使う行列の3x3成分。
    const float a = matrix[0], b = matrix[4], c = matrix[8], d = matrix[1], e = matrix[5], f = matrix[9], g = matrix[2], h = matrix[6], i = matrix[10];
    // 法線変換行列の特異性を判定する行列式。
    const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(determinant) < 1.0e-12f)
    {
        output[0] = source[0];
        output[1] = source[1];
        output[2] = source[2];
        return;
    }
    // 逆行列計算に使う行列式の逆数。
    const float inv = 1.0f / determinant;
    // 出力法線の1行目に対応する逆転置成分。
    const float n00 = (e * i - f * h) * inv, n01 = (f * g - d * i) * inv, n02 = (d * h - e * g) * inv;
    // 出力法線の2行目に対応する逆転置成分。
    const float n10 = (c * h - b * i) * inv, n11 = (a * i - c * g) * inv, n12 = (b * g - a * h) * inv;
    // 出力法線の3行目に対応する逆転置成分。
    const float n20 = (b * f - c * e) * inv, n21 = (c * d - a * f) * inv, n22 = (a * e - b * d) * inv;
    output[0] = n00 * source[0] + n10 * source[1] + n20 * source[2];
    output[1] = n01 * source[0] + n11 * source[1] + n21 * source[2];
    output[2] = n02 * source[0] + n12 * source[1] + n22 * source[2];
}

/**
 * glTF材質または既定材質を重複のないモデルentryへ登録する。
 */
int32_t AddMaterial(cgltf_data* data, cgltf_material* source, ModelResource& model, uint32_t* textureMap, String& error)
{
    // 基本色、金属度、粗さ、画像slotを保持する材質値。
    ModelMaterial material{};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.metallicFactor = 1.0f;
    material.roughnessFactor = 1.0f;
    material.baseColorTextureIndex = -1;
    if (source)
    {
        // source材質がdata内にあることを確かめるindex。
        const cgltf_size sourceIndex = cgltf_material_index(data, source);
        if (sourceIndex >= data->materials_count)
            return -1;
        // 対応していない混合描画と、不正なalpha境界値を拒否する。
        if (source->alpha_mode == cgltf_alpha_mode_blend)
        {
            error.Assign("GLB BLEND alpha mode is unsupported");
            return -1;
        }
        if (source->alpha_mode != cgltf_alpha_mode_opaque && source->alpha_mode != cgltf_alpha_mode_mask)
        {
            error.Assign("GLB alpha mode is invalid");
            return -1;
        }
        if (!IsFinite(source->alpha_cutoff) || source->alpha_cutoff < 0.0f)
        {
            error.Assign("GLB alpha cutoff must be finite and nonnegative");
            return -1;
        }
        material.alphaMask = source->alpha_mode == cgltf_alpha_mode_mask;
        material.alphaCutoff = source->alpha_cutoff;
    }
    if (source && source->has_pbr_metallic_roughness)
    {
        // glTFの基本色・金属度・粗さ設定。
        const cgltf_pbr_metallic_roughness& pbr = source->pbr_metallic_roughness;
        // 基本色factorの各成分を材質へ複写するloop。
        for (uint32_t i = 0; i < 4; ++i)
            material.baseColorFactor[i] = pbr.base_color_factor[i];
        material.metallicFactor = pbr.metallic_factor;
        material.roughnessFactor = pbr.roughness_factor;
        if (pbr.base_color_texture.texture)
        {
            // glTF texture配列内での参照位置。
            const cgltf_size textureIndex = static_cast<cgltf_size>(pbr.base_color_texture.texture - data->textures);
            if (textureIndex >= data->textures_count)
            {
                error.Assign("GLB material has an invalid base-color texture");
                return -1;
            }
            if (textureMap[textureIndex] == missingIndex)
            {
                // base color textureが参照するembedded image。
                cgltf_image* sourceImage = pbr.base_color_texture.texture->image;
                if (!sourceImage || !sourceImage->buffer_view || !sourceImage->mime_type || strcmp(sourceImage->mime_type, "image/png") != 0)
                {
                    error.Assign("GLB base-color image must be embedded PNG data");
                    return -1;
                }
                // decodeへ渡すPNG byte列。
                const uint8_t* encoded = cgltf_buffer_view_data(sourceImage->buffer_view);
                if (!encoded || sourceImage->buffer_view->size > maxModelFileBytes)
                {
                    error.Assign("GLB image buffer is invalid or too large");
                    return -1;
                }
                // PNG byte列から作った画像resource。
                ImageResource* image = DecodeImagePayload(encoded, static_cast<uint32_t>(sourceImage->buffer_view->size), error);
                if (!image)
                    return -1;
                textureMap[textureIndex] = model.textures.Count();
                if (!model.textures.Append(image))
                {
                    Release(&image->reference);
                    error.Assign("GLB material texture allocation failed");
                    return -1;
                }
            }
            material.baseColorTextureIndex = static_cast<int32_t>(textureMap[textureIndex]);
        }
    }
    // 既存材質と等しければ同じslotを再利用するloop。
    for (uint32_t i = 0; i < model.materials.Count(); ++i)
    {
        // 比較対象の既登録材質。
        const ModelMaterial& existing = model.materials.At(i);
        // 全factorとtexture indexが一致するかを累積する値。
        bool equal = existing.metallicFactor == material.metallicFactor && existing.roughnessFactor == material.roughnessFactor && existing.baseColorTextureIndex == material.baseColorTextureIndex && existing.alphaMask == material.alphaMask && existing.alphaCutoff == material.alphaCutoff;
        // RGBA factorの各成分を比較するloop。
        for (uint32_t component = 0; component < 4; ++component)
            equal = equal && existing.baseColorFactor[component] == material.baseColorFactor[component];
        if (equal)
            return static_cast<int32_t>(i);
    }
    if (model.materials.Count() == 1 && model.primitives.Count() == 0)
    {
        model.materials.At(0) = material;
        return 0;
    }
    if (!model.materials.Append(material))
    {
        error.Assign("GLB material allocation failed");
        return -1;
    }
    return static_cast<int32_t>(model.materials.Count() - 1);
}

/**
 * 材質が使う画像座標を選ぶ。指定先の欠損、不正な型、未対応の座標変換は失敗する。
 */
bool SelectBaseColorUv(const cgltf_primitive& primitive, const cgltf_accessor*& output, String& error)
{
    // 画像を使わない材質は、従来どおりUV0を保持する。
    const cgltf_material* material = primitive.material;
    if (!material || !material->has_pbr_metallic_roughness || !material->pbr_metallic_roughness.base_color_texture.texture)
    {
        output = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
        return true;
    }

    // 基本色の画像と、そこへ渡す座標セットの指定。
    const cgltf_texture_view& texture = material->pbr_metallic_roughness.base_color_texture;
    if (texture.has_transform)
    {
        error.Assign("GLB base-color texture coordinate transforms are unsupported");
        return false;
    }
    if (texture.texcoord < 0)
    {
        error.Assign("GLB base-color texture coordinate set index must be nonnegative");
        return false;
    }

    // 指定された番号の座標。存在しない番号をUV0へ置き換えない。
    const cgltf_accessor* selected = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, texture.texcoord);
    if (!selected)
    {
        error.Assign("GLB base-color texture coordinate set is missing from the primitive");
        return false;
    }

    // 浮動小数、または0から1へ正規化する符号なし整数だけを画像座標に使う。
    const bool floating = selected->component_type == cgltf_component_type_r_32f && !selected->normalized;
    // 符号なし8bitと16bitを読み込み時に0から1へ変換する形式。
    const bool normalizedUnsigned = selected->normalized && (selected->component_type == cgltf_component_type_r_8u || selected->component_type == cgltf_component_type_r_16u);
    if (!floating && !normalizedUnsigned)
    {
        error.Assign("GLB base-color texture coordinates require FLOAT or normalized unsigned byte/short data");
        return false;
    }
    output = selected;
    return true;
}

/**
 * index付き三角形primitiveを検証し、変換済み頂点・index・材質を追加する。
 */
bool AppendGlbPrimitive(cgltf_data* data, cgltf_primitive* primitive, const float matrix[16], ModelResource& model, uint32_t* textureMap, String& error)
{
    if (primitive->type != cgltf_primitive_type_triangles || primitive->targets_count || primitive->has_draco_mesh_compression)
    {
        error.Assign("GLB supports static triangle primitives without morph or Draco data");
        return false;
    }
    // primitiveが持つ位置属性。
    const cgltf_accessor* position = cgltf_find_accessor(primitive, cgltf_attribute_type_position, 0);
    // primitiveが持つ法線属性。
    const cgltf_accessor* normal = cgltf_find_accessor(primitive, cgltf_attribute_type_normal, 0);
    // 材質が選んだ座標を頂点へ複写するための入力。
    const cgltf_accessor* uv = nullptr;
    if (!SelectBaseColorUv(*primitive, uv, error))
    {
        return false;
    }
    if (!position || position->type != cgltf_type_vec3 || position->component_type != cgltf_component_type_r_32f || position->is_sparse || !position->buffer_view || position->count == 0 || position->count > maxOutputVertices - model.vertices.Count())
    {
        error.Assign("GLB primitive has invalid or excessive positions");
        return false;
    }
    if ((normal && (normal->type != cgltf_type_vec3 || normal->count != position->count || normal->is_sparse || !normal->buffer_view)) || (uv && (uv->type != cgltf_type_vec2 || uv->count != position->count || uv->is_sparse || !uv->buffer_view)))
    {
        error.Assign("GLB primitive has incompatible normals or texture coordinates");
        return false;
    }
    // このprimitiveで追加を始める頂点位置。
    const uint32_t firstVertex = model.vertices.Count();
    // position accessorの全要素をモデル頂点へ変換するloop。
    for (cgltf_size i = 0; i < position->count; ++i)
    {
        // 現在処理している出力頂点。
        ModelVertex vertex{};
        // cgltf accessorから属性を読む一時配列。
        float value[4]{};
        if (!cgltf_accessor_read_float(position, i, value, 3))
        {
            error.Assign("GLB position accessor could not be read");
            return false;
        }
        TransformPosition(matrix, value, vertex.position);
        if (normal)
        {
            if (!cgltf_accessor_read_float(normal, i, value, 3))
            {
                error.Assign("GLB normal accessor could not be read");
                return false;
            }
            TransformNormal(matrix, value, vertex.normal);
        }
        if (uv)
        {
            if (!cgltf_accessor_read_float(uv, i, value, 2))
            {
                error.Assign("GLB texture accessor could not be read");
                return false;
            }
            vertex.uv[0] = value[0];
            vertex.uv[1] = value[1];
        }
        if (!IsFinite(vertex.position[0]) || !IsFinite(vertex.position[1]) || !IsFinite(vertex.position[2]) || !IsFinite(vertex.normal[0]) || !IsFinite(vertex.normal[1]) || !IsFinite(vertex.normal[2]) || !IsFinite(vertex.uv[0]) || !IsFinite(vertex.uv[1]) || !model.vertices.Append(vertex))
        {
            error.Assign("GLB vertex values or allocation are invalid");
            return false;
        }
    }
    // このprimitiveで追加を始めるindex位置。
    const uint32_t firstIndex = model.indices.Count();
    // 明示index、または連番indexの総数。
    const cgltf_size indexCount = primitive->indices ? primitive->indices->count : position->count;
    if (indexCount == 0 || indexCount % 3 != 0 || indexCount > maxOutputIndices - model.indices.Count())
    {
        error.Assign("GLB primitive index count is invalid or excessive");
        return false;
    }
    // primitiveのlocal indexをモデル全体のindexへ変換するloop。
    for (cgltf_size i = 0; i < indexCount; ++i)
    {
        // accessorから読むprimitive内index。
        const cgltf_size index = primitive->indices ? cgltf_accessor_read_index(primitive->indices, i) : i;
        if (index >= position->count || !model.indices.Append(firstVertex + static_cast<uint32_t>(index)))
        {
            error.Assign("GLB primitive contains an invalid index or could not allocate indices");
            return false;
        }
    }
    // primitiveが参照する重複除去済み材質slot。
    const int32_t materialIndex = AddMaterial(data, primitive->material, model, textureMap, error);
    if (materialIndex < 0)
        return false;
    // 追加したindex範囲と材質slotを結ぶprimitive記録。
    ModelPrimitive group = { firstIndex, static_cast<uint32_t>(indexCount), materialIndex };
    if (!model.primitives.Append(group))
    {
        error.Assign("GLB primitive allocation failed");
        return false;
    }
    return true;
}

/**
 * nodeのworld変換を使ってmesh内の全primitiveを追加する。
 */
bool AppendGlbMesh(cgltf_data* data, cgltf_mesh* mesh, const float matrix[16], ModelResource& model, uint32_t* textureMap, String& error)
{
    // mesh内のprimitiveを順番に追加するloop。
    for (cgltf_size i = 0; i < mesh->primitives_count; ++i)
        if (!AppendGlbPrimitive(data, &mesh->primitives[i], matrix, model, textureMap, error))
            return false;
    return true;
}

/**
 * 選択sceneのnode subtreeをたどり、階層深度とskin制約を守る。
 */
bool VisitGlbNode(cgltf_data* data, cgltf_node* node, uint32_t depth, ModelResource& model, uint32_t* textureMap, String& error)
{
    if (!node || depth > 64 || node->skin)
    {
        error.Assign("GLB node tree is invalid or uses unsupported skinning");
        return false;
    }
    // nodeから子へ適用するworld変換行列。
    float matrix[16];
    cgltf_node_transform_world(node, matrix);
    if (node->mesh && !AppendGlbMesh(data, node->mesh, matrix, model, textureMap, error))
        return false;
    // 子nodeを深さを進めて再帰処理するloop。
    for (cgltf_size i = 0; i < node->children_count; ++i)
        if (!VisitGlbNode(data, node->children[i], depth + 1, model, textureMap, error))
            return false;
    return true;
}

/**
 * parent graphの循環と、変換処理上限を超えるnode深度を検出する。
 */
bool ValidateParentGraph(cgltf_data* data, String& error)
{
    if (data->nodes_count > maxSourceValues)
    {
        error.Assign("GLB contains too many nodes");
        return false;
    }
    // nodeごとの未訪問・探索中・完了状態。
    Array<uint8_t> state;
    // parent方向へたどったnode indexを一時保持する配列。
    Array<uint32_t> chain;
    // 完了済みnodeからrootまでの階層深度。
    Array<uint32_t> depths;
    // graph検査で使うnode配列の要素数。
    const uint32_t nodeCount = static_cast<uint32_t>(data->nodes_count);
    if (!state.Reserve(nodeCount) || !depths.Reserve(nodeCount) || !chain.Reserve(nodeCount))
    {
        error.Assign("GLB parent graph allocation failed");
        return false;
    }
    // 全nodeの状態と深度を初期化するloop。
    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        if (!state.Append(0) || !depths.Append(0))
        {
            error.Assign("GLB parent graph allocation failed");
            return false;
        }
    }
    // 各nodeからparent graphをたどって循環と深度を検査するloop。
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        // 現在のgraph探索起点。
        const cgltf_node* node = &data->nodes[i];
        chain.Clear();
        // parent側の確定済みdepthが見つかったか。
        bool hasKnownParentDepth = false;
        // 探索済みparent chainの起点depth。
        uint32_t parentDepth = 0;
        while (node)
        {
            // data node配列内での現在node位置。
            const ptrdiff_t index = node - data->nodes;
            if (index < 0 || static_cast<cgltf_size>(index) >= data->nodes_count)
            {
                error.Assign("GLB node parent reference is invalid");
                return false;
            }
            // nodeが未訪問、探索中、完了のどれかを示す値。
            const uint8_t mark = state.At(static_cast<uint32_t>(index));
            if (mark == 1)
            {
                error.Assign("GLB node parent graph contains a cycle");
                return false;
            }
            if (mark == 2)
            {
                hasKnownParentDepth = true;
                parentDepth = depths.At(static_cast<uint32_t>(index));
                break;
            }
            state.At(static_cast<uint32_t>(index)) = 1;
            if (!chain.Append(static_cast<uint32_t>(index)))
            {
                error.Assign("GLB parent graph allocation failed");
                return false;
            }
            node = node->parent;
        }
        // root側から起点へ戻り、各nodeのdepthを確定するloop。
        for (uint32_t j = chain.Count(); j > 0; --j)
        {
            // parent chainから取り出したnode配列位置。
            const uint32_t index = chain.At(j - 1);
            // 直近parent深度から求める現在nodeのdepth。
            const uint32_t depth = hasKnownParentDepth ? parentDepth + 1 : 0;
            if (depth > 64)
            {
                error.Assign("GLB node hierarchy exceeds 64 levels");
                return false;
            }
            depths.At(index) = depth;
            state.At(index) = 2;
            parentDepth = depth;
            hasKnownParentDepth = true;
        }
    }
    return true;
}

/**
 * 埋込bufferを持つGLBをparse・検証し、モデルpayloadへ変換する。
 */
bool LoadGlb(const uint8_t* bytes, uint32_t size, ModelResource& model, String& error)
{
    // cgltfにGLB形式を指定するparse option。
    cgltf_options options{};
    options.type = cgltf_file_type_glb;
    // cgltfがparseしたGLB document。
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, bytes, size, &data) != cgltf_result_success || !data)
    {
        error.Assign("GLB 2.0 parsing failed");
        return false;
    }
    // finishで返す最終読み込み結果。
    bool success = false;
    // glTF textureからモデルtexture slotへの対応表。
    uint32_t* textureMap = nullptr;
    if (data->file_type != cgltf_file_type_glb || !data->asset.version || strcmp(data->asset.version, "2.0") != 0 || data->buffers_count != 1 || data->buffers[0].uri || data->skins_count || !ValidateParentGraph(data, error) || cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success || cgltf_validate(data) != cgltf_result_success)
    {
        if (error.Empty())
            error.Assign("GLB must be valid version 2.0 with one embedded static buffer");
        goto finish;
    }
    if (data->textures_count > maxSourceValues || data->nodes_count > maxSourceValues || data->meshes_count > maxSourceValues)
    {
        error.Assign("GLB contains too many textures, nodes, or meshes");
        goto finish;
    }
    if (data->textures_count)
    {
        textureMap = static_cast<uint32_t*>(Allocate(sizeof(uint32_t) * data->textures_count));
        if (!textureMap)
        {
            error.Assign("GLB texture map allocation failed");
            goto finish;
        }
        // texture対応表を未割当状態で初期化するloop。
        for (cgltf_size i = 0; i < data->textures_count; ++i)
            textureMap[i] = missingIndex;
    }
    if (data->scene)
    {
        // 選択sceneのroot nodeを順に変換するloop。
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i)
            if (!VisitGlbNode(data, data->scene->nodes[i], 0, model, textureMap, error))
                goto finish;
    }
    else
    {
        // 明示sceneがない場合にroot nodeが見つかったか。
        bool foundRoot = false;
        // 全nodeからparentを持たないrootを探すloop。
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            if (!data->nodes[i].parent)
            {
                foundRoot = true;
                if (!VisitGlbNode(data, &data->nodes[i], 0, model, textureMap, error))
                    goto finish;
            }
        }
        if (!foundRoot)
        {
            // node graphがない場合は各meshをidentity変換で追加するloop。
            for (cgltf_size i = 0; i < data->meshes_count; ++i)
            {
                // node transformがないmeshに適用する単位行列。
                const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                if (!AppendGlbMesh(data, &data->meshes[i], identity, model, textureMap, error))
                    goto finish;
            }
        }
    }
    if (!model.indices.Count())
    {
        error.Assign("GLB contains no static triangles");
        goto finish;
    }
    success = true;
finish:
    Deallocate(textureMap);
    cgltf_free(data);
    return success;
}
}

#if defined(GKCORE_TESTING)
/**
 * glTF scene root検査と分けて、テスト用parent graphの深度制限を検証する。
 */
bool ValidateGlbParentsForTesting(const uint32_t* parentIndices, uint32_t nodeCount, uint32_t selectedSceneNode, String& error)
{
    if (!parentIndices || !nodeCount || nodeCount > maxSourceValues || selectedSceneNode >= nodeCount)
    {
        error.Assign("test parent graph arguments are invalid");
        return false;
    }
    // テスト入力から作る一時node配列。
    cgltf_node* nodes = static_cast<cgltf_node*>(Allocate(sizeof(cgltf_node) * nodeCount));
    if (!nodes)
    {
        error.Assign("test parent graph allocation failed");
        return false;
    }
    memset(nodes, 0, sizeof(cgltf_node) * nodeCount);
    // 指定parent indexをnode pointerへ変換するloop。
    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        // 現在nodeが持つparent配列位置。
        const uint32_t parent = parentIndices[i];
        if (parent != missingIndex)
        {
            if (parent >= nodeCount)
            {
                Deallocate(nodes);
                error.Assign("test parent index is invalid");
                return false;
            }
            nodes[i].parent = &nodes[parent];
        }
    }
    // 検査開始点としてsceneへ登録するnode。
    cgltf_node* sceneNode = &nodes[selectedSceneNode];
    // 開始nodeを1件だけ持つ一時scene。
    cgltf_scene scene{};
    scene.nodes = &sceneNode;
    scene.nodes_count = 1;
    // 一時node配列とsceneをまとめた検査用document。
    cgltf_data data{};
    data.nodes = nodes;
    data.nodes_count = nodeCount;
    data.scene = &scene;
    // 一時graphの検査結果。
    const bool result = ValidateParentGraph(&data, error);
    Deallocate(nodes);
    return result;
}
#endif

/**
 * 形式共通model loaderから呼ばれるGLB読み込みentry point。
 */
bool LoadGlbPayload(const uint8_t* bytes, uint32_t size, ModelResource& model, String& error)
{
    return LoadGlb(bytes, size, model, error);
}
}
