// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_GLBMORPHGEOMETRY_H
#define GKCORE_MODEL_ANIMATION_GLBMORPHGEOMETRY_H

#include "resources/Resources.h"

namespace gk::model::animation
{

/**
 * morph後のcorner頂点からflat normalと必要なMikk接線を作る。
 */
bool GenerateGlbMorphedFlatFrame(const Array<detail::ModelVertex>& cornerVertices, bool generateTangents, Array<detail::ModelVertex>& outputVertices, String& error);

/**
 * 明示法線を持つmorph後corner頂点から必要なMikk接線を作る。
 */
bool GenerateGlbMorphedTangentFrame(const Array<detail::ModelVertex>& cornerVertices, Array<detail::ModelVertex>& outputVertices, String& error);

}

#endif
