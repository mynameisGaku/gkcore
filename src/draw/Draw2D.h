#pragma once

#include <gkcore.h>

/**
 * Screen-space rectangles and image commands queued into the active frame.
 */
namespace gk {

/**
 * Queues a rectangle; an unfilled rectangle uses the default one-pixel stroke.
 */
int DrawRect(float x, float y, float width, float height, uint32_t color, bool filled);

/**
 * Queues a positive finite inward stroke, filling the bounds when it is wide enough.
 */
int DrawRectOutline(float x, float y, float width, float height,
                    uint32_t color, float thickness);
int DrawImage(ImageHandle image, float x, float y, bool alphaBlend);
int DrawImageRotated(ImageHandle image, float centerX, float centerY,
                     float scale, float angleRadians, bool alphaBlend);

}
