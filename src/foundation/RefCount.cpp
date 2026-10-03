#include "RefCount.h"

#include <stdint.h>

namespace gk {

bool Retain(RefCounted* object) {
    if (!object || object->references == 0 || object->references == UINT32_MAX) return false;
    ++object->references;
    return true;
}

uint32_t Release(RefCounted* object) {
    if (!object || object->references == 0) return 0;
    --object->references;
    const uint32_t remaining = object->references;
    if (remaining == 0 && object->destroy) object->destroy(object);
    return remaining;
}

} // namespace gk
