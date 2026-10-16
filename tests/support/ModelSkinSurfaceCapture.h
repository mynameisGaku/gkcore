// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_TESTS_SUPPORT_MODEL_SKIN_SURFACE_CAPTURE_H
#define GKCORE_TESTS_SUPPORT_MODEL_SKIN_SURFACE_CAPTURE_H

#include "foundation/Array.h"
#include "foundation/String.h"
#include "model/animation/FModelGpuSkinningGeometry.h"
#include "model/animation/FModelSparseVertexMap.h"
#include "model/animation/FModelSparsePoseGeometry.h"
#include "model/animation/FModelSkinPointCorrection.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"

/**
 * 描画と同じ三角形・位置を保存する、開発capture専用の処理。
 */
namespace gk::testing
{

/**
 * 選んだ身体meshと衣服点の、instance共通SRT適用前の描画参照位置を所有する。
 */
struct FModelSkinSurfaceCapture
{
    /**
     * 元のunique位置IDと、補正もfloat保存した描画参照位置。
     */
    struct FPoint
    {
        // sparse位置配列内の元ID。位置が同じでも別IDをまとめない。
        uint32_t positionId = 0u;
        // 描画へ渡すfloatのXYZ。
        float position[3]{};
    };

    // 呼出し側が厳密に解決した身体mesh node名。
    gk::String bodyMeshName;
    // 共通source内の身体mesh node番号。
    uint32_t bodyNode = 0u;
    // 身体meshに対応する全geometry segment番号。
    gk::Array<uint32_t> bodySegmentIndices;
    // 身体segment内の全unique位置。未参照位置も保持する。
    gk::Array<FPoint> bodyPoints;
    // 描画index順を保つglobal unique位置IDの三つ組。
    gk::Array<uint32_t> trianglePositionIds;
    // 呼出し側が選んだ衣服点の元IDと描画参照位置。
    gk::Array<FPoint> clothPoints;
    // 衣服半径を加えない、現在のmodel-space身体形状。
    gk::Array<gk::model::animation::FModelSecondaryMotionCollisionShape> collisionShapes;

    /**
     * 全所有配列と名前を移し、sourceを空にする。
     */
    void MoveFrom(FModelSkinSurfaceCapture& source);
};

/**
 * 実描画indexをSparseVertexMapでunique位置へ写し、bodyNodeの全segmentを保存する。
 * 不正範囲、身体と他meshの混在三角形、空の身体や衣服点、非有限値、確保失敗ではoutputを保つ。
 */
bool BuildModelSkinSurfaceCapture(const gk::model::animation::FModelGpuSkinningGeometry& geometry, const gk::Array<gk::model::animation::FModelSparseVertexMap>& vertexMap, const gk::Array<uint32_t>& drawIndices, uint32_t bodyNode, const char* bodyMeshName, const gk::model::animation::FModelSparsePoseGeometry& reference, const gk::Array<gk::model::animation::FModelSkinPointCorrection>& corrections, const gk::Array<uint32_t>& selectedClothIds, const gk::Array<gk::model::animation::FModelSecondaryMotionCollisionShape>& collisionShapes, FModelSkinSurfaceCapture& output, gk::String& error);

/**
 * Build成功後のsnapshotをJSONL一行へ変換する。非有限値や確保失敗ではoutputを保つ。
 * 環境変数・frame選択と実際の書込みは呼出し側が行い、通常のskin検証結果と分ける。
 */
bool EncodeModelSkinSurfaceCapture(uint32_t frame, const FModelSkinSurfaceCapture& snapshot, gk::String& output, gk::String& error);

/**
 * 全boneの基準行列をcolumn-majorのJSONLへ変換する。
 * nullの名前、行列数不一致、非有限値、確保失敗ではoutputを保つ。
 * 名前の無いboneは空文字を保存し、bone番号で識別する。
 */
bool EncodeModelSkinSurfaceBaseMatrices(uint32_t frame, const gk::Array<const char*>& boneNames, const gk::Array<float>& matrices, gk::String& output, gk::String& error);

}

#endif
