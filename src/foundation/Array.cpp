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
    if (!element || count_ == UINT32_MAX) return false;
    void* aliasCopy = nullptr;
    const size_t usedBytes = static_cast<size_t>(count_) * elementSize_;
    if (data_ && elementSize_ != 0) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(data_);
        const uintptr_t source = reinterpret_cast<uintptr_t>(element);
        if (source >= base && source - base < usedBytes &&
            elementSize_ <= usedBytes - static_cast<size_t>(source - base)) {
            aliasCopy = Allocate(elementSize_);
            if (!aliasCopy) return false;
            memcpy(aliasCopy, element, elementSize_);
            element = aliasCopy;
        }
    }
    if (count_ == capacity_) {
        uint32_t next = capacity_ == 0 ? 4 : capacity_;
        if (capacity_ != 0) {
            if (capacity_ > UINT32_MAX / 2) next = UINT32_MAX;
            else next = capacity_ * 2;
        }
        if (next <= capacity_ || !Reserve(next)) {
            Deallocate(aliasCopy);
            return false;
        }
    }
    memcpy(static_cast<unsigned char*>(data_) + static_cast<size_t>(count_) * elementSize_, element, elementSize_);
    ++count_;
    Deallocate(aliasCopy);
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
