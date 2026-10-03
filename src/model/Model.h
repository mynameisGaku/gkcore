#pragma once

#include "../resources/Resources.h"

namespace gk::detail {

/**
 * Creates a model payload with its default material ready for primitive loads.
 */
ModelResource* CreateModelResource();
/**
 * Releases model arrays and referenced texture images after the final retain.
 */
void DestroyModelResource(RefCounted* object);

}
