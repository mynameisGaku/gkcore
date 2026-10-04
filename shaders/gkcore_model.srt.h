#pragma once

#include "gkcore_sprite.srt.h"

BEGIN_SRT_NO_AB(ModelLightingResources)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(ModelLightingConstants), gModelLighting)
    END_SRT_SET(PerFrame)
END_SRT(ModelLightingResources)
