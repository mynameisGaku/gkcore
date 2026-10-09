#include "foundation/Memory.h"

#include <stdlib.h>

namespace gk
{

#ifdef GKCORE_TESTING
namespace
{
bool failureInjectionEnabled = false;
uint32_t successfulAllocationsBeforeFailure = 0;

/**
 * Counts allowed allocation requests without changing ownership of existing memory.
 */
bool ShouldFailAllocation()
{
    if (!failureInjectionEnabled)
        return false;
    if (successfulAllocationsBeforeFailure == 0)
        return true;
    --successfulAllocationsBeforeFailure;
    return false;
}
}
#endif

void* Allocate(size_t size)
{
    if (size == 0)
        return nullptr;
#ifdef GKCORE_TESTING
    if (ShouldFailAllocation())
        return nullptr;
#endif
    return malloc(size);
}

void* Reallocate(void* memory, size_t size)
{
    if (size == 0)
    {
        free(memory);
        return nullptr;
    }
#ifdef GKCORE_TESTING
    if (ShouldFailAllocation())
        return nullptr;
#endif
    return realloc(memory, size);
}

void Deallocate(void* memory)
{
    free(memory);
}

#ifdef GKCORE_TESTING
void SetAllocationFailureAfterForTesting(uint32_t successfulAllocations)
{
    successfulAllocationsBeforeFailure = successfulAllocations;
    failureInjectionEnabled = true;
}

void ResetAllocationFailureForTesting()
{
    failureInjectionEnabled = false;
    successfulAllocationsBeforeFailure = 0;
}
#endif

} // namespace gk
