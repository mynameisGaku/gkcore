#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * C-runtime memory allocation helpers used by foundation containers.
 */
namespace gk {

#if defined(_MSC_VER)
// MSVCのmallocが基本型向けに保証する境界を使う。
constexpr size_t kAllocationAlignment = alignof(double);
#else
// 他環境ではC標準のmalloc用最大境界を使う。
constexpr size_t kAllocationAlignment = alignof(max_align_t);
#endif

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

#ifdef GKCORE_TESTING
/**
 * Allows the requested number of successful allocations before later requests fail.
 */
void SetAllocationFailureAfterForTesting(uint32_t successfulAllocations);
/**
 * Disables deterministic allocation failure injection.
 */
void ResetAllocationFailureForTesting();
#endif

} // namespace gk
