#pragma once

#include "Model.h"
#include "../foundation/String.h"

namespace gk::detail {

/**
 * Loads static OBJ or GLB 2.0 geometry and material payloads.
 */
ModelResource* LoadModelPayload(const char* path, String& error);

}
