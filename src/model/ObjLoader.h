#pragma once
#include "../resources/Resources.h"
#include "../foundation/String.h"
/**
 * Bounded text geometry import into retained model resources.
 */
namespace gk::detail {
/**
 * Parses static OBJ geometry with checked signed position, normal, and UV indices.
 */
bool LoadObjPayload(const uint8_t* bytes, uint32_t size, ModelResource& model, String& error);
}
