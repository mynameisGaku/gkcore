// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODEL_SPARSE_POSE_GEOMETRY_H
#define GKCORE_MODEL_ANIMATION_FMODEL_SPARSE_POSE_GEOMETRY_H

#include "foundation/Array.h"

namespace gk::model::animation
{

/**
 * unique位置とnormal groupの変形結果を保持する。
 */
struct FModelSparsePoseGeometry
{
    /**
     * shaderのfloat4と同じ並びで位置または法線を渡す。
     */
    struct FModelVector4
    {
        // XYZ値と、位置は1・法線は0を入れるW成分。
        float value[4];
    };

    // source内unique vertexの変形後位置。
    gk::Array<FModelVector4> positions;
    // source内normal groupの変形後法線。
    gk::Array<FModelVector4> normals;
};

}

#endif
