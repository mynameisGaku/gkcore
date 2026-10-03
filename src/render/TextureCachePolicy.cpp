#include "TextureCachePolicy.h"

namespace gk::render {

bool SelectTextureCacheSlot(const TextureCacheSlotState* slots, uint32_t count,
                            uint64_t activeFrame, uint32_t& selected) {
    if (!slots || count == 0) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!slots[i].occupied) {
            selected = i;
            return true;
        }
    }
    return SelectTextureCacheVictim(slots, count, activeFrame, selected);
}

bool SelectTextureCacheVictim(const TextureCacheSlotState* slots, uint32_t count,
                              uint64_t activeFrame, uint32_t& selected) {
    if (!slots || count == 0) return false;
    bool found = false;
    uint64_t oldestUse = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (!slots[i].occupied) continue;
        if (slots[i].frameUsed == activeFrame) continue;
        if (!found || slots[i].lastUsed < oldestUse) {
            found = true;
            oldestUse = slots[i].lastUsed;
            selected = i;
        }
    }
    return found;
}

bool TextureCacheFitsByteBudget(uint64_t cachedBytes, uint64_t requestedBytes,
                                uint64_t byteCapacity) {
    return requestedBytes <= byteCapacity && cachedBytes <= byteCapacity - requestedBytes;
}

}
