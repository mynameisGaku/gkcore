// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELGPU_SKINNINGGEOMETRY_H
#define GKCORE_MODEL_ANIMATION_FMODELGPU_SKINNINGGEOMETRY_H

#include "foundation/Array.h"
#include <stdint.h>

namespace gk::model::animation
{

/**
 * GPU変形へ渡す不変の位置、skin、面、法線対応を保持する。
 */
struct FModelGpuSkinningGeometry
{
    /**
     * bind poseの頂点位置を保持する。
     */
    struct FPosition
    {
        double value[3]; // ufbxと同じ精度のXYZ位置。
    };

    /**
     * 頂点ごとのinfluence範囲を保持する。
     */
    struct FInfluenceRange
    {
        uint32_t firstInfluence; // influence配列内の先頭。
        uint32_t influenceCount; // 連続するinfluence数。
    };

    /**
     * 1個の線形skin influenceを保持する。
     */
    struct FInfluence
    {
        uint32_t clusterIndex; // 参照するcluster行列。
        double weight;         // 頂点へ掛ける有限weight。
    };

    /**
     * cluster行列を作るnodeとbind変換を保持する。
     */
    struct FCluster
    {
        uint32_t nodeIndex;        // 共通skeleton内のbone node。
        double geometryToBone[12]; // ufbxの列基準4x3 affine行列。
    };

    /**
     * 親基準affine行列を保持する。
     */
    struct FMatrix
    {
        double value[12]; // ufbxの列基準4x3順。
    };

    /**
     * 1面のcorner範囲を保持する。
     */
    struct FFace
    {
        uint32_t firstCorner; // corner配列内の先頭。
        uint32_t cornerCount; // ufbx面corner数。
    };

    /**
     * 面cornerの位置と生成法線groupを対応付ける。
     */
    struct FCorner
    {
        uint32_t positionIndex;    // skin後位置配列のindex。
        uint32_t normalGroupIndex; // 生成法線group配列のindex。
    };

    /**
     * 法線groupに寄与するface列の範囲を保持する。
     */
    struct FNormalGroupRange
    {
        uint32_t firstFace; // groupのface incidence列内先頭。
        uint32_t faceCount; // 順序と重複を含むface incidence数。
    };

    /**
     * 1 mesh segmentがgeometryとclusterの範囲を示す。
     */
    struct FSegment
    {
        uint32_t firstPosition;    // このmeshの位置先頭。
        uint32_t positionCount;    // このmeshの位置数。
        uint32_t firstCluster;     // このmeshのcluster先頭。
        uint32_t clusterCount;     // 実cluster数。
        uint32_t nodeIndex;        // unweighted頂点へ使うmesh node。
        double geometryToNode[12]; // unweighted頂点へ使うgeometry変換。
        uint32_t firstFace;        // このmeshのface先頭。
        uint32_t faceCount;        // このmeshのface数。
    };

    Array<FPosition> positions;
    Array<FInfluenceRange> influenceRanges;
    Array<FInfluence> influences;
    Array<FCluster> clusters;
    Array<FFace> faces;
    Array<FCorner> corners;
    Array<FNormalGroupRange> normalGroupRanges;
    Array<uint32_t> normalFaceIds;
    Array<FSegment> segments;
};

}

#endif
