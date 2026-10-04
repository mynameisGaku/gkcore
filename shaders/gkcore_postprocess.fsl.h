#pragma once

/**
 * Three float4 values shared by bloom, composite, and FXAA shader passes.
 */
STRUCT(PostProcessConstants)
{
    DATA(float4, Parameters, None);
    DATA(float4, ColorAdjustment, None);
    DATA(float4, ImageSize, None);
};

#include "gkcore_postprocess.srt.h"
