#include "../src/render/TextureCachePolicy.h"

#include <cstdio>
#include <cstdlib>

namespace {

void Require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

void TestEmptySlotBeforeEviction() {
    const gk::render::TextureCacheSlotState slots[] = {
        {100, 7, true}, {200, 7, true}, {0, 0, false}
    };
    uint32_t selected = 99;
    Require(gk::render::SelectTextureCacheSlot(slots, 3, 7, selected), "cache finds an empty slot");
    Require(selected == 2, "cache does not evict textures while capacity remains");
}

void TestLeastRecentlyUsedUnpinnedSlot() {
    const gk::render::TextureCacheSlotState slots[] = {
        {10, 3, true}, {20, 4, true}, {30, 3, true}
    };
    uint32_t selected = 99;
    Require(gk::render::SelectTextureCacheSlot(slots, 3, 4, selected), "cache finds an unpinned victim");
    Require(selected == 0, "cache selects the least recently used entry");
}

void TestCurrentFrameEntriesCannotBeEvicted() {
    const gk::render::TextureCacheSlotState slots[] = {
        {10, 8, true}, {20, 8, true}
    };
    uint32_t selected = 99;
    Require(!gk::render::SelectTextureCacheSlot(slots, 2, 8, selected),
            "cache rejects a frame that exceeds its unique-texture capacity");
    Require(selected == 99, "failed cache selection leaves the output unchanged");
}

void TestPinnedLeastRecentlyUsedEntriesAreSkipped() {
    const gk::render::TextureCacheSlotState slots[] = {
        {1, 4, true}, {10, 3, true}, {20, 2, true}
    };
    uint32_t selected = 99;
    Require(gk::render::SelectTextureCacheVictim(slots, 3, 4, selected),
            "cache can select among unpinned entries");
    Require(selected == 1, "cache skips active-frame entry when choosing an LRU victim");
}

void TestVictimSelectionSkipsEmptySlots() {
    const gk::render::TextureCacheSlotState slots[] = {
        {0, 0, false}, {20, 2, true}, {0, 0, false}
    };
    uint32_t selected = 99;
    Require(gk::render::SelectTextureCacheVictim(slots, 3, 4, selected),
            "cache can select an occupied victim when empty slots exist");
    Require(selected == 1, "cache victim selection never returns an empty slot");
}

void TestByteBudgetAvoidsOverflow() {
    const uint64_t capacity = 256u * 1024u * 1024u;
    Require(gk::render::TextureCacheFitsByteBudget(capacity - 4, 4, capacity),
            "cache accepts an allocation that exactly fills its byte budget");
    Require(!gk::render::TextureCacheFitsByteBudget(capacity, 1, capacity),
            "cache rejects one byte beyond its budget");
    Require(!gk::render::TextureCacheFitsByteBudget(0, capacity + 1, capacity),
            "cache rejects an image larger than its full budget");
}

}

int main() {
    TestEmptySlotBeforeEviction();
    TestLeastRecentlyUsedUnpinnedSlot();
    TestCurrentFrameEntriesCannotBeEvicted();
    TestPinnedLeastRecentlyUsedEntriesAreSkipped();
    TestVictimSelectionSkipsEmptySlots();
    TestByteBudgetAvoidsOverflow();
    return 0;
}
