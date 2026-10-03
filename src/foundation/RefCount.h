#pragma once

#include <stdint.h>

/**
 * Intrusive ownership helpers for resources shared with queued draw commands.
 */
namespace gk {

/**
 * Intrusive single-thread reference count. The final Release calls destroy.
 */
struct RefCounted {
    uint32_t references;
    void (*destroy)(RefCounted* object);
};

/**
 * Adds a reference unless the count is zero or would overflow.
 */
bool Retain(RefCounted* object);
/**
 * Releases a reference and returns the remaining count, or zero if already empty.
 */
uint32_t Release(RefCounted* object);

} // namespace gk
