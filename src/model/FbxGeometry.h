#pragma once

#include "Model.h"
#include "FbxMaterial.h"
#include "../../third_party/ufbx/ufbx.h"
#include "../foundation/String.h"
#include <stdint.h>

/**
 * Converts a single static FBX mesh instance into ordered model geometry.
 */
namespace gk::detail {

/**
 * Appends one node's mesh after applying its geometry-to-world transform.
 *
 * Faces remain in file order and adjacent faces with the same resolved material
 * are coalesced into primitive ranges. FBX UV coordinates are converted to the
 * top-left image origin used by image resources. The optional count receives
 * the number of source faces containing degenerate triangles.
 */
bool AppendFbxNodeGeometry(const ufbx_node* node, const char* utf8ModelPath,
                           ModelResource& model, FbxMaterialContext& materialContext,
                           uint32_t* degenerateFaceCount, String& error);

} // namespace gk::detail
