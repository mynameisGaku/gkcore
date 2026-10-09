#include "render/Fxaa.h"

#include "foundation/String.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

namespace gk::tests
{
namespace
{
bool Near(float left, float right, float tolerance = 0.0001f)
{
    return fabsf(left - right) <= tolerance;
}
}

bool FxaaReferenceContract(String& failure)
{
    const render::LinearColor flat = { 0.25f, 0.5f, 0.75f, 0.4f };
    render::LinearColor flatSource[6];
    render::LinearColor flatOutput[6]{};
    for (uint32_t i = 0; i < 6; ++i)
        flatSource[i] = flat;
    if (!render::ApplyFxaaReference(flatSource, 6, 3, 2, flatOutput, 6, failure))
        return false;
    for (uint32_t i = 0; i < 6; ++i)
    {
        if (!Near(flatOutput[i].r, flat.r) || !Near(flatOutput[i].g, flat.g) || !Near(flatOutput[i].b, flat.b) || !Near(flatOutput[i].a, flat.a))
        {
            failure.Assign("FXAA changed a flat region");
            return false;
        }
    }

    render::LinearColor axisSource[35];
    render::LinearColor axisOutput[35]{};
    for (uint32_t y = 0; y < 5; ++y)
    {
        for (uint32_t x = 0; x < 7; ++x)
        {
            const float level = x < 3 ? 0.0f : 1.0f;
            axisSource[y * 7 + x] = { level, level, level, 0.2f + 0.1f * y };
        }
    }
    if (!render::ApplyFxaaReference(axisSource, 35, 7, 5, axisOutput, 35, failure))
        return false;
    for (uint32_t i = 0; i < 35; ++i)
    {
        if (!Near(axisOutput[i].r, axisSource[i].r) || !Near(axisOutput[i].g, axisSource[i].g) || !Near(axisOutput[i].b, axisSource[i].b) || !Near(axisOutput[i].a, axisSource[i].a))
        {
            failure.Assign("FXAA softened an axis-aligned pixel-art edge");
            return false;
        }
    }

    render::LinearColor horizontalSource[35];
    render::LinearColor horizontalOutput[35]{};
    for (uint32_t y = 0; y < 5; ++y)
    {
        for (uint32_t x = 0; x < 7; ++x)
        {
            const float level = y < 2 ? 0.0f : 1.0f;
            horizontalSource[y * 7 + x] = { level, level, level, 0.35f };
        }
    }
    if (!render::ApplyFxaaReference(horizontalSource, 35, 7, 5, horizontalOutput, 35, failure))
        return false;
    for (uint32_t i = 0; i < 35; ++i)
    {
        if (!Near(horizontalOutput[i].r, horizontalSource[i].r) || !Near(horizontalOutput[i].a, horizontalSource[i].a))
        {
            failure.Assign("FXAA softened a horizontal pixel-art edge");
            return false;
        }
    }

    render::LinearColor stairSource[35];
    render::LinearColor stairOutput[35]{};
    for (uint32_t y = 0; y < 5; ++y)
    {
        for (uint32_t x = 0; x < 7; ++x)
        {
            const float level = x <= y + 1 ? 0.0f : 1.0f;
            stairSource[y * 7 + x] = { level, level, level, 0.2f + 0.1f * y };
        }
    }
    if (!render::ApplyFxaaReference(stairSource, 35, 7, 5, stairOutput, 35, failure))
        return false;
    bool edgeChanged = false;
    for (uint32_t i = 0; i < 35; ++i)
    {
        if (stairOutput[i].r < 0.0f || stairOutput[i].r > 1.0f || stairOutput[i].g < 0.0f || stairOutput[i].g > 1.0f || stairOutput[i].b < 0.0f || stairOutput[i].b > 1.0f || !Near(stairOutput[i].a, stairSource[i].a))
        {
            failure.Assign("FXAA output exceeded the color range or changed source alpha");
            return false;
        }
        if (fabsf(stairOutput[i].r - stairSource[i].r) > 0.01f)
            edgeChanged = true;
    }
    if (!edgeChanged)
    {
        failure.Assign("FXAA did not smooth diagonal steps in a non-square image");
        return false;
    }

    const render::LinearColor single[1] = { { 0.8f, 0.3f, 0.1f, 0.6f } };
    render::LinearColor singleOutput[1]{};
    if (!render::ApplyFxaaReference(single, 1, 1, 1, singleOutput, 1, failure) || !Near(singleOutput[0].r, single[0].r) || !Near(singleOutput[0].g, single[0].g) || !Near(singleOutput[0].b, single[0].b) || !Near(singleOutput[0].a, single[0].a))
    {
        failure.Assign("FXAA did not preserve a 1x1 image");
        return false;
    }

    render::LinearColor column[4];
    render::LinearColor columnOutput[4]{};
    for (uint32_t i = 0; i < 4; ++i)
        column[i] = { 0.1f, 0.2f, 0.3f, 1.0f };
    if (!render::ApplyFxaaReference(column, 4, 1, 4, columnOutput, 4, failure))
        return false;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (!Near(columnOutput[i].r, column[i].r) || !Near(columnOutput[i].g, column[i].g))
        {
            failure.Assign("FXAA edge sampling did not clamp for a one-pixel-wide image");
            return false;
        }
    }

    render::LinearColor preserved[2] = { { 0.7f, 0.6f, 0.5f, 0.4f }, { 0.3f, 0.2f, 0.1f, 0.9f } };
    const render::LinearColor sentinel = preserved[0];
    if (render::ApplyFxaaReference(flatSource, 6, 0, 2, preserved, 2, failure) || !Near(preserved[0].r, sentinel.r) || render::ApplyFxaaReference(flatSource, 5, 3, 2, preserved, 2, failure) || !Near(preserved[0].r, sentinel.r) || render::ApplyFxaaReference(flatSource, 6, UINT32_MAX, 2, preserved, 2, failure) || !Near(preserved[0].r, sentinel.r))
    {
        failure.Assign("invalid FXAA image bounds were accepted or modified destination pixels");
        return false;
    }

    render::LinearColor inPlace[6];
    for (uint32_t i = 0; i < 6; ++i)
        inPlace[i] = flatSource[i];
    if (!render::ApplyFxaaReference(inPlace, 6, 3, 2, inPlace, 6, failure))
        return false;
    for (uint32_t i = 0; i < 6; ++i)
    {
        if (!Near(inPlace[i].r, flat.r) || !Near(inPlace[i].a, flat.a))
        {
            failure.Assign("FXAA did not preserve source pixels when input and output alias");
            return false;
        }
    }
    return true;
}

}

int main()
{
    gk::String failure;
    if (gk::tests::FxaaReferenceContract(failure))
        return 0;
    fprintf(stderr, "%s\n", failure.CStr());
    return 1;
}
