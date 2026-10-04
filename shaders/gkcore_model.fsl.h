#pragma once

/**
 * Per-frame directional and ambient lighting shared with the CPU ABI.
 */
STRUCT(ModelLightingConstants)
{
    DATA(float4, DirectionIntensity, None);
    DATA(float4, Ambient, None);
};

#include "gkcore_model.srt.h"
