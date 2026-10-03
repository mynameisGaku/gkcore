#include "Memory.h"

#include <stdlib.h>

namespace gk {

void* Allocate(size_t size) {
    return size == 0 ? nullptr : malloc(size);
}

void* Reallocate(void* memory, size_t size) {
    if (size == 0) {
        free(memory);
        return nullptr;
    }
    return realloc(memory, size);
}

void Deallocate(void* memory) {
    free(memory);
}

} // namespace gk
