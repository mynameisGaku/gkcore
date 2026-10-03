#pragma once

#include <stdint.h>

/**
 * CPU-only cache slot selection shared by renderer code and bound tests.
 */
namespace gk::render {

/**
 * State needed to choose an unpinned cache slot without touching GPU resources.
 */
struct TextureCacheSlotState {
    uint64_t lastUsed;
    uint64_t frameUsed;
    bool occupied;
};

/**
 * Selects a free slot or least-recently-used slot not pinned by the active frame.
 */
bool SelectTextureCacheSlot(const TextureCacheSlotState* slots, uint32_t count,
                            uint64_t activeFrame, uint32_t& selected);
/**
 * Selects the least-recently-used occupied slot that is not pinned by the frame.
 */
bool SelectTextureCacheVictim(const TextureCacheSlotState* slots, uint32_t count,
                              uint64_t activeFrame, uint32_t& selected);
/**
 * Checks a requested allocation without allowing integer overflow past the budget.
 */
bool TextureCacheFitsByteBudget(uint64_t cachedBytes, uint64_t requestedBytes,
                                uint64_t byteCapacity);

}
