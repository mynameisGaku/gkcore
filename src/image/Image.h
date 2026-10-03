#pragma once

#include "../resources/Resources.h"

namespace gk::detail {

/**
 * Creates an empty refcounted image payload.
 */
ImageResource* CreateImageResource();
/**
 * Releases image storage and the image object after its final reference.
 */
void DestroyImageResource(RefCounted* object);

}
