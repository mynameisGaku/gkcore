#pragma once

#include <stddef.h>

/**
 * C-runtime memory allocation helpers used by foundation containers.
 */
namespace gk {

/**
 * Allocates uninitialized storage, returning null when size is zero or allocation fails.
 */
void* Allocate(size_t size);
/**
 * Resizes storage while preserving its bytes; size zero frees the allocation.
 */
void* Reallocate(void* memory, size_t size);
/**
 * Releases storage returned by Allocate or Reallocate.
 */
void Deallocate(void* memory);

} // namespace gk
