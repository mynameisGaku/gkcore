// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_SHADERS_MODEL_SRT_H
#define GKCORE_SHADERS_MODEL_SRT_H

#include "gkcore_model_textures.srt.h"

BEGIN_SRT_NO_AB(ModelLightingResources)
BEGIN_SRT_SET(PerFrame)
DECL_CBUFFER(PerFrame, CBUFFER(ModelLightingConstants), gModelLighting)
END_SRT_SET(PerFrame)
END_SRT(ModelLightingResources)

#endif
