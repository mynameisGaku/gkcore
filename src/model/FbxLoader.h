#pragma once

#include "model/Model.h"
#include "model/FbxMaterial.h"
#include <ufbx/ufbx.h>
#include "foundation/String.h"
#include <stdint.h>

/**
 * Bounded FBX scene parsing and static geometry conversion.
 */
namespace gk::detail
{

/**
 * Parses static FBX geometry in Y-up meters and appends it to a model payload.
 */
bool LoadFbxPayload(const uint8_t* bytes, uint32_t size, const char* utf8Path, ModelResource& model, String& error);

} // namespace gk::detail
