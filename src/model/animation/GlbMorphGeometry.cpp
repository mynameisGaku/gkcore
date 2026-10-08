// SPDX-License-Identifier: NOASSERTION
#include "GlbMorphGeometry.h"
#include "../ModelNormals.h"
#include "../ModelTangents.h"
#include "../../foundation/Array.h"
#include <stdint.h>

namespace gk::model::animation
{
namespace
{

/**
 * corner順を保ってflat normalと必要な接線を作る。
 */
bool GenerateFrame(const Array<detail::ModelVertex>& cornerVertices, bool generateNormals, bool generateTangents, Array<detail::ModelVertex>& outputVertices, String& error)
{
    if (!cornerVertices.Count() || cornerVertices.Count() % 3 != 0)
    {
        error.Assign("GLB morph frame needs complete triangle corners");
        return false;
    }
    Array<uint32_t> indices;
    if (!indices.Reserve(cornerVertices.Count()))
    {
        error.Assign("GLB morph frame index allocation failed");
        return false;
    }
    for (uint32_t corner = 0; corner < cornerVertices.Count(); ++corner)
        if (!indices.Append(corner))
        {
            error.Assign("GLB morph frame index allocation failed");
            return false;
        }
    Array<detail::ModelVertex> normalVertices;
    Array<uint32_t> normalIndices;
    const Array<detail::ModelVertex>* frameVertices = &cornerVertices;
    const Array<uint32_t>* frameIndices = &indices;
    if (generateNormals)
    {
        if (!GenerateModelNormals(cornerVertices, indices, cornerVertices.Count(), normalVertices, normalIndices, error))
            return false;
        frameVertices = &normalVertices;
        frameIndices = &normalIndices;
    }
    if (!generateTangents)
    {
        if (generateNormals)
            outputVertices.MoveFrom(normalVertices);
        else if (!outputVertices.AppendRange(cornerVertices.Data(), cornerVertices.Count()))
        {
            error.Assign("GLB morph frame output allocation failed");
            return false;
        }
        error.Clear();
        return true;
    }
    Array<detail::ModelVertex> tangentVertices;
    Array<uint32_t> tangentIndices;
    if (!GenerateModelTangents(*frameVertices, *frameIndices, cornerVertices.Count(), tangentVertices, tangentIndices, error))
        return false;
    if (tangentVertices.Count() != cornerVertices.Count() || tangentIndices.Count() != cornerVertices.Count())
    {
        error.Assign("GLB morph tangent frame changed its corner count");
        return false;
    }
    outputVertices.MoveFrom(tangentVertices);
    error.Clear();
    return true;
}

}

/**
 * morph後のcorner頂点からflat normalと必要なMikk接線を作る。
 */
bool GenerateGlbMorphedFlatFrame(const Array<detail::ModelVertex>& cornerVertices, bool generateTangents, Array<detail::ModelVertex>& outputVertices, String& error)
{
    return GenerateFrame(cornerVertices, true, generateTangents, outputVertices, error);
}

/**
 * 明示法線を持つmorph後corner頂点から必要なMikk接線を作る。
 */
bool GenerateGlbMorphedTangentFrame(const Array<detail::ModelVertex>& cornerVertices, Array<detail::ModelVertex>& outputVertices, String& error)
{
    return GenerateFrame(cornerVertices, false, true, outputVertices, error);
}

}
