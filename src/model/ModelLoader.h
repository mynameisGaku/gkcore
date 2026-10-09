#pragma once

#include "model/Model.h"
#include "foundation/String.h"

namespace gk::detail
{

/**
 * Loads bounded static OBJ, GLB 2.0, or FBX geometry and material payloads.
 */
ModelResource* LoadModelPayload(const char* path, String& error);

}
