#pragma once

#include <gkcore.h>

/**
 * Screen-space rectangles and image commands queued into the active frame.
 */
namespace gk {

int DrawRect(float x, float y, float width, float height, uint32_t color, bool filled);
int DrawImage(ImageHandle image, float x, float y, bool alphaBlend);
int DrawImageRotated(ImageHandle image, float centerX, float centerY,
                     float scale, float angleRadians, bool alphaBlend);

}
