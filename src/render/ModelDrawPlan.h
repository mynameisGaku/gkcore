// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_MODELDRAWPLAN_H
#define GKCORE_RENDER_MODELDRAWPLAN_H

#include "../resources/Resources.h"

/**
 * 静的model描画に使うCPU検証と材質対応付け。
 */
namespace gk::render
{

/**
 * 検証済みprimitive範囲と線形基本色・画像選択をまとめた描画情報。
 */
struct ModelPartPlan
{
    // model index配列の開始位置。
    uint32_t firstIndex;
    // primitiveが使うindex数。
    uint32_t indexCount;
    // 元modelの材質index。未指定は-1。
    int32_t materialIndex;
    // 基本色画像index。未使用は-1。
    int32_t textureIndex;
    // 頂点色へ掛ける線形RGBA係数。
    float baseColorFactor[4];
    // PBR金属度係数。
    float metallicFactor = 0.0f;
    // PBR粗さ係数。
    float roughnessFactor = 1.0f;
    // 内蔵の材質描画でアルファ抜きを使うか。
    bool alphaMask = false;
    // アルファ抜きの境界値。1を超えた場合は全画素を抜く。
    float alphaCutoff = 0.5f;
    // 金属度・粗さの画像番号。未使用なら-1。
    int32_t metallicRoughnessTextureIndex = -1;
    // 法線マップの画像番号。未使用は-1。
    int32_t normalTextureIndex = -1;
    // 法線マップの横・縦方向の倍率。
    float normalScale = 1.0f;
};

/**
 * 頂点生成と画像切替へ渡す順序付きprimitive計画。
 */
struct ModelDrawPlan
{
    // 描画順のprimitive情報。
    Array<ModelPartPlan> parts;

    /**
     * 再利用できる容量を保ったまま、現在の描画計画を空にする。
     */
    void Reset();
};

/**
 * primitive範囲と材質係数を検証してから、出力計画を描画順情報へ置き換える。
 */
bool BuildModelDrawPlan(const detail::ModelResource& model, ModelDrawPlan& output, String& error);

// namespace gk::render
}

#endif
