#pragma once

BEGIN_SRT_NO_AB(PostProcessResources)
    BEGIN_SRT_SET(Persistent)
        DECL_ARRAY_TEXTURES(Persistent, Tex2D(float4), gPostTextures, 2)
        DECL_SAMPLER(Persistent, SamplerState, gPostSampler)
        DECL_CBUFFER(Persistent, CBUFFER(PostProcessConstants), gPostConstants)
    END_SRT_SET(Persistent)
END_SRT(PostProcessResources)
