#include "Array.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

namespace gk {

RawArray::RawArray(uint32_t elementSize)
    : data_(nullptr), elementSize_(elementSize), count_(0), capacity_(0) {}

RawArray::~RawArray() {
    Reset();
}

bool RawArray::Reserve(uint32_t capacity) {
    if (capacity <= capacity_) return true;
    if (elementSize_ == 0 || capacity > UINT32_MAX / elementSize_) return false;
    const size_t byteCount = static_cast<size_t>(capacity) * elementSize_;
    void* replacement = Reallocate(data_, byteCount);
    if (!replacement) return false;
    data_ = replacement;
    capacity_ = capacity;
    return true;
}

bool RawArray::Append(const void* element) {
    return AppendRange(element, 1);
}

bool RawArray::AppendRange(const void* elements, uint32_t count) {
    if (count == 0) return true;
    if (!elements || elementSize_ == 0 || count > UINT32_MAX - count_) return false;
    if (count > UINT32_MAX / elementSize_) return false;
    const uint32_t requiredCount = count_ + count;
    if (requiredCount > UINT32_MAX / elementSize_) return false;
    const size_t usedBytes = static_cast<size_t>(count_) * elementSize_;
    const size_t allocatedBytes = static_cast<size_t>(capacity_) * elementSize_;
    const size_t appendBytes = static_cast<size_t>(count) * elementSize_;
    const uintptr_t base = reinterpret_cast<uintptr_t>(data_);
    const uintptr_t source = reinterpret_cast<uintptr_t>(elements);
    if (appendBytes > UINTPTR_MAX - source || allocatedBytes > UINTPTR_MAX - base) return false;
    const uintptr_t usedEnd = base + usedBytes;
    const uintptr_t allocatedEnd = base + allocatedBytes;
    const uintptr_t sourceEnd = source + appendBytes;
    const bool overlapsUsed = data_ && source < usedEnd && base < sourceEnd;
    const bool overlapsAllocation = data_ && source < allocatedEnd && base < sourceEnd;
    size_t aliasOffset = 0;
    if (overlapsAllocation) {
        if (!overlapsUsed) return false;
        if (source < base) return false;
        aliasOffset = static_cast<size_t>(source - base);
        if (aliasOffset > usedBytes || appendBytes > usedBytes - aliasOffset) return false;
    }

    if (requiredCount > capacity_) {
        uint32_t next = capacity_ == 0 ? 4 : capacity_;
        while (next < requiredCount) {
            if (next > UINT32_MAX / 2) {
                next = requiredCount;
                break;
            }
            next *= 2;
        }
        const uint32_t maximumCount = UINT32_MAX / elementSize_;
        if (next > maximumCount) next = requiredCount;
        if (next < requiredCount || !Reserve(next)) return false;
    }

    const void* sourceAfterGrowth = overlapsAllocation
        ? static_cast<const unsigned char*>(data_) + aliasOffset
        : elements;
    memcpy(static_cast<unsigned char*>(data_) + usedBytes, sourceAfterGrowth, appendBytes);
    count_ = requiredCount;
    return true;
}

void RawArray::Clear() {
    count_ = 0;
}

void RawArray::Reset() {
    Deallocate(data_);
    data_ = nullptr;
    count_ = 0;
    capacity_ = 0;
}

void RawArray::MoveFrom(RawArray& source) {
    if (this == &source) return;
    Reset();
    elementSize_ = source.elementSize_;
    data_ = source.data_;
    count_ = source.count_;
    capacity_ = source.capacity_;
    source.data_ = nullptr;
    source.count_ = 0;
    source.capacity_ = 0;
}

void* RawArray::Data() { return data_; }
const void* RawArray::Data() const { return data_; }
uint32_t RawArray::Count() const { return count_; }
uint32_t RawArray::Capacity() const { return capacity_; }

bool RawArray::RemoveAt(uint32_t index) {
    if (index >= count_) return false;
    unsigned char* bytes = static_cast<unsigned char*>(data_);
    const size_t tailBytes = static_cast<size_t>(count_ - index - 1) * elementSize_;
    if (tailBytes != 0) memmove(bytes + static_cast<size_t>(index) * elementSize_,
                                bytes + static_cast<size_t>(index + 1) * elementSize_, tailBytes);
    --count_;
    return true;
}

} // namespace gk
