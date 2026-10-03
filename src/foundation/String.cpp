#include "String.h"
#include "Memory.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

namespace gk {
namespace {
const char kEmptyString[] = "";
}

String::String() : data_(nullptr), length_(0), capacity_(0) {}
String::~String() { Reset(); }

bool String::Assign(const char* text) {
    if (!text) return false;
    const size_t length = strlen(text);
    if (length > UINT32_MAX) return false;
    return Assign(text, static_cast<uint32_t>(length));
}

bool String::Assign(const char* text, uint32_t length) {
    if (!text && length != 0) return false;
    if (length == UINT32_MAX) return false;
    const uintptr_t source = reinterpret_cast<uintptr_t>(text);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(data_);
    if (data_ && source >= begin && source - begin < length_) {
        String copy;
        if (!copy.Assign(text, length)) return false;
        MoveFrom(copy);
        return true;
    }
    const uint32_t needed = length + 1;
    if (needed > capacity_) {
        char* replacement = static_cast<char*>(Allocate(needed));
        if (!replacement) return false;
        if (length != 0) memcpy(replacement, text, length);
        replacement[length] = '\0';
        Deallocate(data_);
        data_ = replacement;
        capacity_ = needed;
    } else {
        if (length != 0) memcpy(data_, text, length);
        data_[length] = '\0';
    }
    length_ = length;
    return true;
}

bool String::Append(const char* text) {
    if (!text) return false;
    const size_t length = strlen(text);
    if (length > UINT32_MAX) return false;
    return Append(text, static_cast<uint32_t>(length));
}

bool String::Append(const char* text, uint32_t length) {
    if (!text && length != 0) return false;
    if (length > UINT32_MAX - length_ - 1) return false;
    const uintptr_t source = reinterpret_cast<uintptr_t>(text);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(data_);
    if (data_ && source >= begin && source - begin < length_) {
        String copy;
        if (!copy.Assign(text, length)) return false;
        return Append(copy.CStr(), copy.Length());
    }
    const uint32_t nextLength = length_ + length;
    const uint32_t needed = nextLength + 1;
    if (needed > capacity_) {
        uint32_t nextCapacity = capacity_ == 0 ? 16 : capacity_;
        while (nextCapacity < needed) {
            if (nextCapacity > UINT32_MAX / 2) { nextCapacity = needed; break; }
            nextCapacity *= 2;
        }
        char* replacement = static_cast<char*>(Reallocate(data_, nextCapacity));
        if (!replacement) return false;
        data_ = replacement;
        capacity_ = nextCapacity;
    }
    if (length != 0) memcpy(data_ + length_, text, length);
    length_ = nextLength;
    data_[length_] = '\0';
    return true;
}

bool String::AppendUnsigned(uint64_t value) {
    char digits[21];
    uint32_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    if (count > UINT32_MAX - length_ - 1) return false;
    const uint32_t oldLength = length_;
    const uint32_t needed = oldLength + count + 1;
    if (needed > capacity_) {
        uint32_t nextCapacity = capacity_ == 0 ? 16 : capacity_;
        while (nextCapacity < needed) {
            if (nextCapacity > UINT32_MAX / 2) { nextCapacity = needed; break; }
            nextCapacity *= 2;
        }
        char* replacement = static_cast<char*>(Reallocate(data_, nextCapacity));
        if (!replacement) return false;
        data_ = replacement;
        capacity_ = nextCapacity;
    }
    for (uint32_t i = 0; i < count; ++i) data_[oldLength + i] = digits[count - i - 1];
    length_ = oldLength + count;
    data_[length_] = '\0';
    return true;
}

void String::Clear() {
    length_ = 0;
    if (data_) data_[0] = '\0';
}

void String::Reset() {
    Deallocate(data_);
    data_ = nullptr;
    length_ = 0;
    capacity_ = 0;
}

void String::MoveFrom(String& source) {
    if (this == &source) return;
    Reset();
    data_ = source.data_;
    length_ = source.length_;
    capacity_ = source.capacity_;
    source.data_ = nullptr;
    source.length_ = 0;
    source.capacity_ = 0;
}

bool String::Empty() const { return length_ == 0; }
uint32_t String::Length() const { return length_; }
const char* String::CStr() const { return data_ ? data_ : kEmptyString; }

} // namespace gk
