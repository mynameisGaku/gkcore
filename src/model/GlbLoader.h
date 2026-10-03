#pragma once
#include "../resources/Resources.h"
#include "../foundation/String.h"
/**
 * Static model import and validation without renderer dependencies.
 */
namespace gk::detail {
/**
 * Parses embedded static GLB 2.0 geometry, transforms, and PBR payloads.
 */
bool LoadGlbPayload(const uint8_t* bytes, uint32_t size, ModelResource& model, String& error);
#if defined(GKCORE_TESTING)
/**
 * Exercises parent-depth validation with a selected scene node, including non-root selections.
 */
bool ValidateGlbParentsForTesting(const uint32_t* parentIndices, uint32_t nodeCount,
                                  uint32_t selectedSceneNode, String& error);
#endif
}
