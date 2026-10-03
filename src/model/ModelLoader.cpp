#include "ModelLoader.h"
#include "GlbLoader.h"
#include "ObjLoader.h"
#include "../resources/ResourceIO.h"
#include "../foundation/Memory.h"
#include <string.h>

namespace gk::detail {
ModelResource* LoadModelPayload(const char* path, String& error) {
    uint8_t* bytes = nullptr;
    uint32_t size = 0;
    const uint32_t maxModelFileBytes = 64u * 1024u * 1024u;
    if (!ReadResourceFile(path, maxModelFileBytes, bytes, size, error)) return nullptr;
    ModelResource* model = CreateModelResource();
    if (!model) { Deallocate(bytes); error.Assign("model allocation failed"); return nullptr; }
    bool success = false;
    if (size >= 4 && bytes[0] == 'g' && bytes[1] == 'l' && bytes[2] == 'T' && bytes[3] == 'F')
        success = LoadGlbPayload(bytes, size, *model, error);
    else
        success = LoadObjPayload(bytes, size, *model, error);
    Deallocate(bytes);
    if (!success) { Release(&model->reference); return nullptr; }
    if (!model->primitives.Count()) {
        ModelPrimitive primitive = {0, model->indices.Count(), 0};
        if (!model->primitives.Append(primitive)) { Release(&model->reference); error.Assign("model primitive allocation failed"); return nullptr; }
    }
    return model;
}
}
