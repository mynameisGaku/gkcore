// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RESOURCES_RESOURCES_H
#define GKCORE_RESOURCES_RESOURCES_H

#include "../../include/gkcore/Handle.h"
#include "../foundation/Array.h"
#include "../foundation/RefCount.h"
#include "../foundation/String.h"
#include "FTextureSampler.h"
#include <stdint.h>

/**
 * gkcoreが所有する画像・モデルのデータと操作。
 */
namespace gk::detail
{

/**
 * 左上から並ぶRGBA画素を保持するdecode済み画像。
 */
struct ImageResource
{
    // 共有所有権を管理する参照情報。
    RefCounted reference;
    // 画像の横幅。
    uint32_t width;
    // 画像の縦幅。
    uint32_t height;
    // 左上から詰めて並べたRGBA画素byte列。
    Array<uint8_t> rgba;
};

/**
 * 位置、法線、画像座標を持つモデル頂点。
 */
struct ModelVertex
{
    // model localの位置。
    float position[3];
    // model localの法線。
    float normal[3];
    // base color imageを参照する座標。
    float uv[2];
    // 金属度・粗さの画像を読む座標。基本色とは別の座標を保持する。
    float metallicRoughnessUv[2]{};
    // 法線マップに使う独立した画像座標。
    float normalUv[2]{};
    // 画像の横方向の接線と、縦方向を決める符号。
    float tangent[4]{};
    // 自己発光画像に使う独立した画像座標。
    float emissiveUv[2]{};
    // 環境遮蔽画像に使う独立した画像座標。
    float occlusionUv[2]{};
};

/**
 * mesh primitiveの材質係数と任意の基本色画像参照。
 */
struct ModelMaterial
{
    // RGBA基本色係数。
    float baseColorFactor[4];
    // 金属度係数。
    float metallicFactor;
    // 粗さ係数。
    float roughnessFactor;
    // 基本色画像配列のindex。未使用は-1。
    int32_t baseColorTextureIndex;
    // 基本色のアルファ値で画素を切り抜く場合はtrue。
    bool alphaMask = false;
    // 切り抜く画素の境界値。0以上の有限値を使い、1を超える値も保持する。
    float alphaCutoff = 0.5f;
    // 金属度・粗さの画像番号。画像がない場合は-1。
    int32_t metallicRoughnessTextureIndex = -1;
    // 法線マップ画像の番号。使用しない場合は-1。
    int32_t normalTextureIndex = -1;
    // 接線座標の横・縦成分に掛ける倍率。有限値を使う。
    float normalScale = 1.0f;
    // base color画像の座標処理と補間方法。
    FTextureSampler baseColorSampler{};
    // 金属度・粗さ画像の座標処理と補間方法。
    FTextureSampler metallicRoughnessSampler{};
    // 法線マップ画像の座標処理と補間方法。
    FTextureSampler normalSampler{};
    // 線形自己発光色のRGB係数。
    float emissiveFactor[3]{};
    // 自己発光係数へ掛ける非負の強度。
    float emissiveStrength = 1.0f;
    // 自己発光画像配列のindex。未使用は-1。
    int32_t emissiveTextureIndex = -1;
    // 自己発光画像の座標処理と補間方法。
    FTextureSampler emissiveSampler{};
    // 環境遮蔽画像の効果量。0から1の範囲を使う。
    float occlusionStrength = 1.0f;
    // 環境遮蔽画像の配列index。未使用は-1。
    int32_t occlusionTextureIndex = -1;
    // 環境遮蔽画像の座標処理と補間方法。
    FTextureSampler occlusionSampler{};
};

/**
 * 1つの材質に結び付く連続したindex範囲。
 */
struct ModelPrimitive
{
    // index配列内の開始位置。
    uint32_t firstIndex;
    // primitiveが使うindex数。
    uint32_t indexCount;
    // 材質配列のindex。未指定は-1。
    int32_t materialIndex;
};

/**
 * 静的な頂点・index、primitive材質、保持中の画像をまとめたモデル。
 */
struct ModelResource
{
    // 共有所有権を管理する参照情報。
    RefCounted reference;
    // 読み込み済み頂点。
    Array<ModelVertex> vertices;
    // 頂点を結ぶ三角形index。
    Array<uint32_t> indices;
    // 描画単位ごとの範囲と材質。
    Array<ModelPrimitive> primitives;
    // primitiveから参照される材質。
    Array<ModelMaterial> materials;
    // 材質が参照する保持中の画像。
    Array<ImageResource*> textures;
};

/**
 * 対応画像を読み込み、失敗理由と画像handleを返す。
 */
ImageHandle LoadImage(const char* path, String& error);
/**
 * 画像handleを登録解除する。描画側が保持する参照は有効なまま残る。
 */
bool DeleteImage(ImageHandle handle, String& error);
/**
 * 登録中handleの画像payloadを借用参照で返す。無効handleならnullを返す。
 */
ImageResource* FindImage(ImageHandle handle);
/**
 * 対応model fileから静的形状と材質を読み込み、失敗理由とhandleを返す。
 */
ModelHandle LoadModel(const char* path, String& error);
/**
 * model handleを登録解除する。描画側が保持する参照は有効なまま残る。
 */
bool DeleteModel(ModelHandle handle, String& error);
/**
 * 登録中handleのmodel payloadを借用参照で返す。無効handleならnullを返す。
 */
ModelResource* FindModel(ModelHandle handle);
/**
 * 読み込み済みresourceすべてのregistry所有権を解放する。
 */
void ClearResources();

// namespace gk::detail
}

#endif
