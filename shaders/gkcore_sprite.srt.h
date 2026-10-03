#pragma once

BEGIN_SRT_NO_AB(SpriteResources)
    BEGIN_SRT_SET(Persistent)
        DECL_TEXTURE(Persistent, Tex2D(float4), gImageTexture)
        DECL_SAMPLER(Persistent, SamplerState, gImageSampler)
    END_SRT_SET(Persistent)
END_SRT(SpriteResources)
