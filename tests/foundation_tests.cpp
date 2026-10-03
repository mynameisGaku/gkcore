#include "../include/gkcore/Handle.h"
#include "../src/foundation/Array.h"
#include "../src/foundation/HandleTable.h"
#include "../src/foundation/RefCount.h"
#include "../src/foundation/String.h"

#include <cstdint>
#include <cstring>

namespace {
int destroyedRefs = 0;
void destroyRef(gk::RefCounted*) { ++destroyedRefs; }
}

namespace gk::tests {

bool FoundationContracts() {
    Array<std::uint32_t> values;
    if (values.Count() != 0 || values.Data() != nullptr || !values.Append(10) || !values.Append(20)) return false;
    if (values.Count() != 2 || values.At(0) != 10 || values.At(1) != 20 || !values.Reserve(16)) return false;
    const std::uint32_t* stable = values.Data();
    if (!values.Append(30) || values.Data() != stable || values.Count() != 3) return false;
    values.Clear();
    if (values.Count() != 0 || !values.Append(40) || values.At(0) != 40) return false;
    Array<std::uint32_t> moved;
    moved.MoveFrom(values);
    if (values.Count() != 0 || moved.Count() != 1 || moved.At(0) != 40) return false;
    moved.Reset();
    if (moved.Count() != 0 || moved.Capacity() != 0) return false;
    if (!values.Append(99) || values.Reserve(UINT32_MAX) || values.Count() != 1 || values.At(0) != 99) return false;
    Array<uint32_t> aliases;
    if (!aliases.Append(1) || !aliases.Append(2) || !aliases.Append(3) || !aliases.Append(4)) return false;
    Array<uint32_t> allocationFence;
    if (!allocationFence.Reserve(1024)) return false;
    if (!aliases.Append(aliases.At(0)) || aliases.Count() != 5 || aliases.At(4) != 1) return false;

    String message;
    if (!message.Assign("cannot load ") || !message.Append("image.bmp")) return false;
    if (message.Empty() || message.Length() != 21 || std::strcmp(message.CStr(), "cannot load image.bmp") != 0) return false;
    String movedMessage;
    movedMessage.MoveFrom(message);
    if (!message.Empty() || std::strcmp(movedMessage.CStr(), "cannot load image.bmp") != 0) return false;
    movedMessage.Clear();
    if (!movedMessage.Empty() || movedMessage.CStr()[0] != '\0' || message.Assign(nullptr)) return false;
    if (!movedMessage.Assign("abcdef") || !movedMessage.Append(movedMessage.CStr(), movedMessage.Length()) ||
        std::strcmp(movedMessage.CStr(), "abcdefabcdef") != 0) return false;
    if (!movedMessage.Assign(movedMessage.CStr() + 2, 4) || std::strcmp(movedMessage.CStr(), "cdef") != 0) return false;
    if (movedMessage.Assign("x", UINT32_MAX) || std::strcmp(movedMessage.CStr(), "cdef") != 0) return false;

    ImageHandle invalid;
    ImageHandle first(1);
    ImageHandle second(2);
    if (invalid.IsValid() || static_cast<bool>(invalid) || !first.IsValid() || !static_cast<bool>(second)) return false;
    int imageA = 11, imageB = 22, *removed = nullptr;
    HandleTable<ImageTag, int> images;
    if (!images.Insert(first, &imageA) || images.Find(first) != &imageA || images.Find(second) != nullptr) return false;
    if (images.Insert(first, &imageB) || !images.Insert(second, &imageB)) return false;
    if (!images.Remove(first, removed) || removed != &imageA || images.Find(first) != nullptr) return false;
    images.Clear();
    if (images.Find(second) != nullptr) return false;

    RefCounted reference;
    reference.references = 1;
    reference.destroy = destroyRef;
    destroyedRefs = 0;
    if (!Retain(&reference) || reference.references != 2) return false;
    if (Release(&reference) != 1 || reference.references != 1 || destroyedRefs != 0) return false;
    if (Release(&reference) != 0 || destroyedRefs != 1) return false;
    if (Release(&reference) != 0 || destroyedRefs != 1) return false;
    RefCounted overflowReference = {UINT32_MAX, nullptr};
    if (Retain(&overflowReference) || overflowReference.references != UINT32_MAX) return false;
    if (Allocate(0) != nullptr) return false;
    if (Release(&overflowReference) != UINT32_MAX - 1) return false;
    return true;
}

} // namespace gk::tests
