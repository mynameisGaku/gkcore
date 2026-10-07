#include "GlbLoader.h"
#include "../image/ImageLoader.h"
#include "../foundation/Memory.h"
#include "../resources/TextureSampler.h"
#include "../../third_party/cgltf/cgltf.h"
#include <math.h>
#include <stdint.h>
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
// image mapに未割当を示すindex値。
const uint32_t missingIndex = 0xffffffffu;
/**
 * 変換後の頂点属性へ保存する前に有限値か確かめる。
 */
bool IsFinite(float value)
{
    return isfinite(value) != 0;
}

/**
 * 配列内にある要素だけを見つけ、必要ならそのindexを返す。
 */
bool FindArrayIndex(const void* array, cgltf_size count, size_t elementSize, const void* element, cgltf_size* index)
{
    if (!array || !element || !count || !elementSize)
        return false;
    // 配列先頭と照合対象のアドレス。
    const uintptr_t arrayStart = reinterpret_cast<uintptr_t>(array);
    const uintptr_t elementAddress = reinterpret_cast<uintptr_t>(element);
    if (count > static_cast<cgltf_size>((UINTPTR_MAX - arrayStart) / elementSize))
        return false;
    // count上限を確認した配列byte数。
    const uintptr_t arrayBytes = static_cast<uintptr_t>(count) * elementSize;
    if (elementAddress < arrayStart || elementAddress - arrayStart >= arrayBytes || (elementAddress - arrayStart) % elementSize != 0)
        return false;
    if (index)
        *index = (elementAddress - arrayStart) / elementSize;
    return true;
}

/**
 * glTFのfilter値を固定36状態で使うnearest/linearへ変換する。
 * 未対応値では出力を変えず失敗する。
 */
bool ReadSamplerFilter(cgltf_filter_type source, bool minification, ETextureFilter& output)
{
    // sampler省略時と同じ線形補間を初期値にする。
    ETextureFilter filter = ETextureFilter::Linear;
    if (source == cgltf_filter_type_undefined)
    {
        output = filter;
        return true;
    }
    if (source == cgltf_filter_type_nearest || (minification && (source == cgltf_filter_type_nearest_mipmap_nearest || source == cgltf_filter_type_nearest_mipmap_linear)))
        filter = ETextureFilter::Nearest;
    else if (source != cgltf_filter_type_linear && !(minification && (source == cgltf_filter_type_linear_mipmap_nearest || source == cgltf_filter_type_linear_mipmap_linear)))
        return false;
    output = filter;
    return true;
}

/**
 * texture viewが参照するaddress/filter値を材質用samplerへ読み込む。
 * 配列外参照や未対応enumでは診断を返し、出力を維持する。
 */
bool ReadTextureSampler(cgltf_data* data, const cgltf_texture_view& view, const char* role, FTextureSampler& output, String& error)
{
    if (!FindArrayIndex(data->textures, data->textures_count, sizeof(cgltf_texture), view.texture, nullptr))
    {
        error.Assign(role);
        error.Append(" texture reference is invalid");
        return false;
    }
    // glTFでsamplerが省略されたtexture viewの標準値。
    FTextureSampler sampler{};
    sampler.addressU = ETextureAddressMode::Repeat;
    sampler.addressV = ETextureAddressMode::Repeat;
    if (view.texture->sampler)
    {
        // textureが参照するsampler recordを配列内で検証する。
        const cgltf_sampler* source = view.texture->sampler;
        if (!FindArrayIndex(data->samplers, data->samplers_count, sizeof(cgltf_sampler), source, nullptr))
        {
            error.Assign(role);
            error.Append(" sampler reference is invalid");
            return false;
        }
        // S/Tのwrap値を固定address modeへ変換する。
        if (source->wrap_s == cgltf_wrap_mode_clamp_to_edge)
            sampler.addressU = ETextureAddressMode::ClampToEdge;
        else if (source->wrap_s == cgltf_wrap_mode_repeat)
            sampler.addressU = ETextureAddressMode::Repeat;
        else if (source->wrap_s == cgltf_wrap_mode_mirrored_repeat)
            sampler.addressU = ETextureAddressMode::MirroredRepeat;
        else
        {
            error.Assign(role);
            error.Append(" sampler wrapS value is unsupported");
            return false;
        }
        if (source->wrap_t == cgltf_wrap_mode_clamp_to_edge)
            sampler.addressV = ETextureAddressMode::ClampToEdge;
        else if (source->wrap_t == cgltf_wrap_mode_repeat)
            sampler.addressV = ETextureAddressMode::Repeat;
        else if (source->wrap_t == cgltf_wrap_mode_mirrored_repeat)
            sampler.addressV = ETextureAddressMode::MirroredRepeat;
        else
        {
            error.Assign(role);
            error.Append(" sampler wrapT value is unsupported");
            return false;
        }
        if (!ReadSamplerFilter(source->min_filter, true, sampler.minFilter))
        {
            error.Assign(role);
            error.Append(" sampler minFilter value is unsupported");
            return false;
        }
        if (!ReadSamplerFilter(source->mag_filter, false, sampler.magFilter))
        {
            error.Assign(role);
            error.Append(" sampler magFilter value is unsupported");
            return false;
        }
    }
    output = sampler;
    return true;
}

/**
 * node world行列の線形部分から行列式を求める。
 */
double LinearDeterminant(const float matrix[16])
{
    // 列優先行列から取り出す線形部分の行成分。
    const double a = matrix[0], b = matrix[4], c = matrix[8], d = matrix[1], e = matrix[5], f = matrix[9], g = matrix[2], h = matrix[6], i = matrix[10];
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

/**
 * double精度の方向を安定して単位長へ揃え、floatへ保存する。
 */
bool NormalizeDoubleDirection(const double source[3], float output[3])
{
    // 二乗計算のoverflowを避けるための最大成分。
    const double maximum = fmax(fabs(source[0]), fmax(fabs(source[1]), fabs(source[2])));
    if (!(maximum > 0.0) || !isfinite(maximum))
        return false;
    // 最大成分で割った方向と、その安全な長さ。
    const double x = source[0] / maximum, y = source[1] / maximum, z = source[2] / maximum;
    const double length = sqrt(x * x + y * y + z * z);
    if (!(length > 0.0) || !isfinite(length))
        return false;
    output[0] = static_cast<float>(x / length);
    output[1] = static_cast<float>(y / length);
    output[2] = static_cast<float>(z / length);
    return IsFinite(output[0]) && IsFinite(output[1]) && IsFinite(output[2]);
}

/**
 * 元の法線と接線が同じ方向を指していないことを確かめる。
 */
bool HasIndependentNormalTangent(const float normal[3], const float tangent[3])
{
    // 入力方向をdoubleへ広げ、長さの計算を安定させる。
    const double nx = normal[0], ny = normal[1], nz = normal[2];
    const double tx = tangent[0], ty = tangent[1], tz = tangent[2];
    const double normalScale = fmax(fabs(nx), fmax(fabs(ny), fabs(nz)));
    const double tangentScale = fmax(fabs(tx), fmax(fabs(ty), fabs(tz)));
    if (!(normalScale > 0.0) || !(tangentScale > 0.0) || !isfinite(normalScale) || !isfinite(tangentScale))
        return false;
    // 最大成分で割った単位長計算用の方向。
    const double n0 = nx / normalScale, n1 = ny / normalScale, n2 = nz / normalScale;
    const double t0 = tx / tangentScale, t1 = ty / tangentScale, t2 = tz / tangentScale;
    const double nLength = sqrt(n0 * n0 + n1 * n1 + n2 * n2);
    const double tLength = sqrt(t0 * t0 + t1 * t1 + t2 * t2);
    // 正規化した方向同士の外積長は、平行ならゼロになる。
    const double c0 = (n1 / nLength) * (t2 / tLength) - (n2 / nLength) * (t1 / tLength);
    const double c1 = (n2 / nLength) * (t0 / tLength) - (n0 / nLength) * (t2 / tLength);
    const double c2 = (n0 / nLength) * (t1 / tLength) - (n1 / nLength) * (t0 / tLength);
    return sqrt(c0 * c0 + c1 * c1 + c2 * c2) > 1.0e-12;
}

/**
 * 法線へnode world行列の逆転置を適用し、float保存前に正規化する。
 */
bool TransformMappedNormal(const float matrix[16], double determinant, const float source[3], float output[3])
{
    // 線形部分の逆転置行列を作るcofactor成分。
    const double a = matrix[0], b = matrix[4], c = matrix[8], d = matrix[1], e = matrix[5], f = matrix[9], g = matrix[2], h = matrix[6], i = matrix[10];
    const double n00 = e * i - f * h, n01 = f * g - d * i, n02 = d * h - e * g;
    const double n10 = c * h - b * i, n11 = a * i - c * g, n12 = b * g - a * h;
    const double n20 = b * f - c * e, n21 = c * d - a * f, n22 = a * e - b * d;
    // detで割った逆転置方向を単位長に整える。
    const double transformed[3] = { (n00 * source[0] + n01 * source[1] + n02 * source[2]) / determinant, (n10 * source[0] + n11 * source[1] + n12 * source[2]) / determinant, (n20 * source[0] + n21 * source[1] + n22 * source[2]) / determinant };
    return isfinite(transformed[0]) && isfinite(transformed[1]) && isfinite(transformed[2]) && NormalizeDoubleDirection(transformed, output);
}

/**
 * 接線をdouble精度でnode変換し、法線面へ正規化してhandednessを保つ。
 */
bool TransformMappedTangent(const float matrix[16], const float source[4], const float normal[3], float determinantSign, float output[4])
{
    // node変換後の接線をfloatへ狭める前のdouble方向。
    const double transformed[3] = { static_cast<double>(matrix[0]) * source[0] + static_cast<double>(matrix[4]) * source[1] + static_cast<double>(matrix[8]) * source[2], static_cast<double>(matrix[1]) * source[0] + static_cast<double>(matrix[5]) * source[1] + static_cast<double>(matrix[9]) * source[2], static_cast<double>(matrix[2]) * source[0] + static_cast<double>(matrix[6]) * source[1] + static_cast<double>(matrix[10]) * source[2] };
    if (!isfinite(transformed[0]) || !isfinite(transformed[1]) || !isfinite(transformed[2]))
        return false;
    // 接線から法線方向の成分を取り除いたdouble方向。
    const double projection = transformed[0] * normal[0] + transformed[1] * normal[1] + transformed[2] * normal[2];
    const double orthogonal[3] = { transformed[0] - projection * normal[0], transformed[1] - projection * normal[1], transformed[2] - projection * normal[2] };
    if (!NormalizeDoubleDirection(orthogonal, output))
        return false;
    output[3] = source[3] * determinantSign;
    return IsFinite(output[3]);
}

/**
 * glTF textureを画像resourceへ登録し、共有済みslotを返す。
 * 参照外、未対応画像、読み込みや確保の失敗では-1を返す。
 */
int32_t AddTexture(cgltf_data* data, const cgltf_texture_view& view, const char* role, ModelResource& model, uint32_t* imageMap, String& error)
{
    if (!FindArrayIndex(data->textures, data->textures_count, sizeof(cgltf_texture), view.texture, nullptr))
    {
        error.Assign(role);
        error.Append(" texture reference is invalid");
        return -1;
    }
    // textureが参照する画像recordと、その配列内index。
    cgltf_image* sourceImage = view.texture->image;
    cgltf_size imageIndex = 0;
    if (!FindArrayIndex(data->images, data->images_count, sizeof(cgltf_image), sourceImage, &imageIndex))
    {
        error.Assign(role);
        error.Append(" image reference is invalid");
        return -1;
    }
    if (sourceImage->uri || !sourceImage->buffer_view || !FindArrayIndex(data->buffer_views, data->buffer_views_count, sizeof(cgltf_buffer_view), sourceImage->buffer_view, nullptr) || !sourceImage->mime_type || strcmp(sourceImage->mime_type, "image/png") != 0)
    {
        error.Assign(role);
        error.Append(" image must be embedded PNG data");
        return -1;
    }
    if (sourceImage->buffer_view->size > maxModelFileBytes)
    {
        error.Assign(role);
        error.Append(" image buffer is invalid or too large");
        return -1;
    }
    // alias参照でもbuffer範囲を確認してからdecoded image mapを見る。
    const uint8_t* encoded = cgltf_buffer_view_data(sourceImage->buffer_view);
    if (!encoded)
    {
        error.Assign(role);
        error.Append(" image buffer is invalid or too large");
        return -1;
    }
    if (imageMap[imageIndex] == missingIndex)
    {
        // 同じimage recordに対して一度だけ作る所有resource。
        ImageResource* image = DecodeImagePayload(encoded, static_cast<uint32_t>(sourceImage->buffer_view->size), error);
        if (!image)
            return -1;
        const uint32_t textureSlot = model.textures.Count();
        if (!model.textures.Append(image))
        {
            Release(&image->reference);
            error.Assign("GLB material texture allocation failed");
            return -1;
        }
        imageMap[imageIndex] = textureSlot;
    }
    return static_cast<int32_t>(imageMap[imageIndex]);
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
int32_t AddMaterial(cgltf_data* data, cgltf_material* source, ModelResource& model, uint32_t* imageMap, String& error)
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
    material.metallicRoughnessTextureIndex = -1;
    material.normalTextureIndex = -1;
    material.normalScale = 1.0f;
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
            if (!ReadTextureSampler(data, pbr.base_color_texture, "GLB base-color", material.baseColorSampler, error))
                return -1;
            material.baseColorTextureIndex = AddTexture(data, pbr.base_color_texture, "GLB base-color", model, imageMap, error);
            if (material.baseColorTextureIndex < 0)
                return -1;
        }
        if (pbr.metallic_roughness_texture.texture)
        {
            if (!ReadTextureSampler(data, pbr.metallic_roughness_texture, "GLB metallic-roughness", material.metallicRoughnessSampler, error))
                return -1;
            material.metallicRoughnessTextureIndex = AddTexture(data, pbr.metallic_roughness_texture, "GLB metallic-roughness", model, imageMap, error);
            if (material.metallicRoughnessTextureIndex < 0)
                return -1;
        }
    }
    if (source && source->normal_texture.texture)
    {
        if (!ReadTextureSampler(data, source->normal_texture, "GLB normal", material.normalSampler, error))
            return -1;
        // normal textureへ掛ける有限scale値。
        if (!IsFinite(source->normal_texture.scale))
        {
            error.Assign("GLB normal texture scale must be finite");
            return -1;
        }
        material.normalTextureIndex = AddTexture(data, source->normal_texture, "GLB normal", model, imageMap, error);
        if (material.normalTextureIndex < 0)
            return -1;
        material.normalScale = source->normal_texture.scale;
    }
    // 既存材質と等しければ同じslotを再利用するloop。
    for (uint32_t i = 0; i < model.materials.Count(); ++i)
    {
        // 比較対象の既登録材質。
        const ModelMaterial& existing = model.materials.At(i);
        // 全factorとtexture indexが一致するかを累積する値。
        bool equal = existing.metallicFactor == material.metallicFactor && existing.roughnessFactor == material.roughnessFactor && existing.baseColorTextureIndex == material.baseColorTextureIndex && existing.metallicRoughnessTextureIndex == material.metallicRoughnessTextureIndex && existing.normalTextureIndex == material.normalTextureIndex && existing.normalScale == material.normalScale && existing.alphaMask == material.alphaMask && existing.alphaCutoff == material.alphaCutoff && AreTextureSamplersEqual(existing.baseColorSampler, material.baseColorSampler) && AreTextureSamplersEqual(existing.metallicRoughnessSampler, material.metallicRoughnessSampler) && AreTextureSamplersEqual(existing.normalSampler, material.normalSampler);
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
bool SelectTextureUv(const cgltf_primitive& primitive, const cgltf_texture_view& texture, const char* role, const cgltf_accessor*& output, String& error)
{
    if (texture.has_transform)
    {
        error.Assign("GLB ");
        error.Append(role);
        error.Append(" texture coordinate transforms are unsupported");
        return false;
    }
    if (texture.texcoord < 0)
    {
        error.Assign("GLB ");
        error.Append(role);
        error.Append(" texture coordinate set index must be nonnegative");
        return false;
    }

    // 指定された番号の座標。存在しない番号をUV0へ置き換えない。
    const cgltf_accessor* selected = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, texture.texcoord);
    if (!selected)
    {
        error.Assign("GLB ");
        error.Append(role);
        error.Append(" texture coordinate set is missing from the primitive");
        return false;
    }

    // 浮動小数、または0から1へ正規化する符号なし整数だけを画像座標に使う。
    const bool floating = selected->component_type == cgltf_component_type_r_32f && !selected->normalized;
    // 符号なし8bitと16bitを読み込み時に0から1へ変換する形式。
    const bool normalizedUnsigned = selected->normalized && (selected->component_type == cgltf_component_type_r_8u || selected->component_type == cgltf_component_type_r_16u);
    if (!floating && !normalizedUnsigned)
    {
        error.Assign("GLB ");
        error.Append(role);
        error.Append(" texture coordinates require FLOAT or normalized unsigned byte/short data");
        return false;
    }
    output = selected;
    return true;
}

/**
 * index付き三角形primitiveを検証し、変換済み頂点・index・材質を追加する。
 */
bool AppendGlbPrimitive(cgltf_data* data, cgltf_primitive* primitive, const float matrix[16], ModelResource& model, uint32_t* imageMap, String& error)
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
    // 基本色画像へ渡す座標accessor。画像がなければ従来どおりUV0を保持する。
    const cgltf_accessor* baseColorUv = cgltf_find_accessor(primitive, cgltf_attribute_type_texcoord, 0);
    // 金属度・粗さ画像へ渡す独立した座標accessor。
    const cgltf_accessor* metallicRoughnessUv = nullptr;
    // normal textureの有無でのみ必要となる属性と変換条件。
    const bool hasNormalTexture = primitive->material && primitive->material->normal_texture.texture;
    const cgltf_accessor* tangent = hasNormalTexture ? cgltf_find_accessor(primitive, cgltf_attribute_type_tangent, 0) : nullptr;
    const cgltf_accessor* normalUv = nullptr;
    double normalMapDeterminant = 1.0;
    float normalMapDeterminantSign = 1.0f;
    const cgltf_pbr_metallic_roughness* pbr = primitive->material && primitive->material->has_pbr_metallic_roughness ? &primitive->material->pbr_metallic_roughness : nullptr;
    if (pbr && pbr->base_color_texture.texture && !SelectTextureUv(*primitive, pbr->base_color_texture, "base-color", baseColorUv, error))
    {
        return false;
    }
    if (pbr && pbr->metallic_roughness_texture.texture && !SelectTextureUv(*primitive, pbr->metallic_roughness_texture, "metallic-roughness", metallicRoughnessUv, error))
    {
        return false;
    }
    if (hasNormalTexture)
    {
        if (!SelectTextureUv(*primitive, primitive->material->normal_texture, "normal", normalUv, error))
            return false;
        // normalと接線を一意に変換できるnode行列の向きと可逆性。
        normalMapDeterminant = LinearDeterminant(matrix);
        if (!isfinite(normalMapDeterminant) || normalMapDeterminant == 0.0)
        {
            error.Assign("GLB normal mapping cannot use a singular node transform");
            return false;
        }
        normalMapDeterminantSign = normalMapDeterminant < 0.0 ? -1.0f : 1.0f;
    }
    if (!position || position->type != cgltf_type_vec3 || position->component_type != cgltf_component_type_r_32f || position->is_sparse || !position->buffer_view || position->count == 0 || position->count > maxOutputVertices - model.vertices.Count())
    {
        error.Assign("GLB primitive has invalid or excessive positions");
        return false;
    }
    if ((normal && (normal->type != cgltf_type_vec3 || normal->count != position->count || normal->is_sparse || !normal->buffer_view)) || (baseColorUv && (baseColorUv->type != cgltf_type_vec2 || baseColorUv->count != position->count || baseColorUv->is_sparse || !baseColorUv->buffer_view)) || (metallicRoughnessUv && (metallicRoughnessUv->type != cgltf_type_vec2 || metallicRoughnessUv->count != position->count || metallicRoughnessUv->is_sparse || !metallicRoughnessUv->buffer_view)))
    {
        error.Assign("GLB primitive has incompatible normals or texture coordinates");
        return false;
    }
    if (normalUv && (normalUv->type != cgltf_type_vec2 || normalUv->count != position->count || normalUv->is_sparse || !normalUv->buffer_view))
    {
        error.Assign("GLB normal texture coordinates are incompatible with primitive positions");
        return false;
    }
    if (hasNormalTexture && (!normal || normal->type != cgltf_type_vec3 || normal->component_type != cgltf_component_type_r_32f || normal->normalized || normal->count != position->count || normal->is_sparse || !normal->buffer_view || !tangent || tangent->type != cgltf_type_vec4 || tangent->component_type != cgltf_component_type_r_32f || tangent->normalized || tangent->count != position->count || tangent->is_sparse || !tangent->buffer_view))
    {
        error.Assign("GLB normal mapping requires matching FLOAT NORMAL and TANGENT attributes");
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
            if (hasNormalTexture)
            {
                if (!TransformMappedNormal(matrix, normalMapDeterminant, value, vertex.normal))
                {
                    error.Assign("GLB normal mapping contains a zero or non-finite normal");
                    return false;
                }
                // tangent accessorから読むFLOAT vec4。
                float tangentValue[4]{};
                if (!cgltf_accessor_read_float(tangent, i, tangentValue, 4) || !IsFinite(tangentValue[0]) || !IsFinite(tangentValue[1]) || !IsFinite(tangentValue[2]) || (tangentValue[3] != -1.0f && tangentValue[3] != 1.0f))
                {
                    error.Assign("GLB normal mapping tangent must be finite with handedness -1 or 1");
                    return false;
                }
                if (!HasIndependentNormalTangent(value, tangentValue))
                {
                    error.Assign("GLB normal mapping source tangent is zero or parallel to its normal");
                    return false;
                }
                if (!TransformMappedTangent(matrix, tangentValue, vertex.normal, normalMapDeterminantSign, vertex.tangent))
                {
                    error.Assign("GLB normal mapping tangent is zero, parallel, or outside the numeric range");
                    return false;
                }
            }
            else
            {
                TransformNormal(matrix, value, vertex.normal);
            }
        }
        if (baseColorUv)
        {
            if (!cgltf_accessor_read_float(baseColorUv, i, value, 2))
            {
                error.Assign("GLB texture accessor could not be read");
                return false;
            }
            vertex.uv[0] = value[0];
            vertex.uv[1] = value[1];
        }
        if (metallicRoughnessUv)
        {
            if (!cgltf_accessor_read_float(metallicRoughnessUv, i, value, 2))
            {
                error.Assign("GLB metallic-roughness texture accessor could not be read");
                return false;
            }
            vertex.metallicRoughnessUv[0] = value[0];
            vertex.metallicRoughnessUv[1] = value[1];
        }
        if (normalUv)
        {
            if (!cgltf_accessor_read_float(normalUv, i, value, 2))
            {
                error.Assign("GLB normal texture accessor could not be read");
                return false;
            }
            vertex.normalUv[0] = value[0];
            vertex.normalUv[1] = value[1];
        }
        if (!IsFinite(vertex.position[0]) || !IsFinite(vertex.position[1]) || !IsFinite(vertex.position[2]) || !IsFinite(vertex.normal[0]) || !IsFinite(vertex.normal[1]) || !IsFinite(vertex.normal[2]) || !IsFinite(vertex.uv[0]) || !IsFinite(vertex.uv[1]) || !IsFinite(vertex.metallicRoughnessUv[0]) || !IsFinite(vertex.metallicRoughnessUv[1]) || !IsFinite(vertex.normalUv[0]) || !IsFinite(vertex.normalUv[1]) || !IsFinite(vertex.tangent[0]) || !IsFinite(vertex.tangent[1]) || !IsFinite(vertex.tangent[2]) || !IsFinite(vertex.tangent[3]) || !model.vertices.Append(vertex))
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
    if (hasNormalTexture)
    {
        // 各三角形の3頂点で接線handednessが揃うか確認するloop。
        for (uint32_t offset = 0; offset < static_cast<uint32_t>(indexCount); offset += 3)
        {
            // 三角形の先頭頂点と残り2頂点。
            const uint32_t first = model.indices.At(firstIndex + offset);
            const uint32_t second = model.indices.At(firstIndex + offset + 1);
            const uint32_t third = model.indices.At(firstIndex + offset + 2);
            const float sign = model.vertices.At(first).tangent[3];
            if (sign != model.vertices.At(second).tangent[3] || sign != model.vertices.At(third).tangent[3])
            {
                error.Assign("GLB normal mapping tangent handedness must match within each triangle");
                return false;
            }
        }
    }
    // primitiveが参照する重複除去済み材質slot。
    const int32_t materialIndex = AddMaterial(data, primitive->material, model, imageMap, error);
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
bool AppendGlbMesh(cgltf_data* data, cgltf_mesh* mesh, const float matrix[16], ModelResource& model, uint32_t* imageMap, String& error)
{
    // mesh内のprimitiveを順番に追加するloop。
    for (cgltf_size i = 0; i < mesh->primitives_count; ++i)
        if (!AppendGlbPrimitive(data, &mesh->primitives[i], matrix, model, imageMap, error))
            return false;
    return true;
}

/**
 * 選択sceneのnode subtreeをたどり、階層深度とskin制約を守る。
 */
bool VisitGlbNode(cgltf_data* data, cgltf_node* node, uint32_t depth, ModelResource& model, uint32_t* imageMap, String& error)
{
    if (!node || depth > 64 || node->skin)
    {
        error.Assign("GLB node tree is invalid or uses unsupported skinning");
        return false;
    }
    // nodeから子へ適用するworld変換行列。
    float matrix[16];
    cgltf_node_transform_world(node, matrix);
    if (node->mesh && !AppendGlbMesh(data, node->mesh, matrix, model, imageMap, error))
        return false;
    // 子nodeを深さを進めて再帰処理するloop。
    for (cgltf_size i = 0; i < node->children_count; ++i)
        if (!VisitGlbNode(data, node->children[i], depth + 1, model, imageMap, error))
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
    // glTF image recordからdecoded resource slotへの対応表。
    uint32_t* imageMap = nullptr;
    if (data->file_type != cgltf_file_type_glb || !data->asset.version || strcmp(data->asset.version, "2.0") != 0 || data->buffers_count != 1 || data->buffers[0].uri || data->skins_count || !ValidateParentGraph(data, error) || cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success || cgltf_validate(data) != cgltf_result_success)
    {
        if (error.Empty())
            error.Assign("GLB must be valid version 2.0 with one embedded static buffer");
        goto finish;
    }
    if (data->textures_count > maxSourceValues || data->samplers_count > maxSourceValues || data->images_count > maxSourceValues || data->nodes_count > maxSourceValues || data->meshes_count > maxSourceValues)
    {
        error.Assign("GLB contains too many textures, samplers, images, nodes, or meshes");
        goto finish;
    }
    if (data->images_count)
    {
        imageMap = static_cast<uint32_t*>(Allocate(sizeof(uint32_t) * data->images_count));
        if (!imageMap)
        {
            error.Assign("GLB image map allocation failed");
            goto finish;
        }
        // image recordとdecoded resourceの対応表を未割当で初期化するloop。
        for (cgltf_size i = 0; i < data->images_count; ++i)
            imageMap[i] = missingIndex;
    }
    if (data->scene)
    {
        // 選択sceneのroot nodeを順に変換するloop。
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i)
            if (!VisitGlbNode(data, data->scene->nodes[i], 0, model, imageMap, error))
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
                if (!VisitGlbNode(data, &data->nodes[i], 0, model, imageMap, error))
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
                if (!AppendGlbMesh(data, &data->meshes[i], identity, model, imageMap, error))
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
    Deallocate(imageMap);
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
