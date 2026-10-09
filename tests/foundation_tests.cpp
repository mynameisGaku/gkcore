#include <gkcore/Handle.h>
#include "foundation/Array.h"
#include "foundation/HandleTable.h"
#include "foundation/RefCount.h"
#include "foundation/String.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
int destroyedRefs = 0;
void destroyRef(gk::RefCounted*)
{
    ++destroyedRefs;
}

// C標準の最大alignment境界を持つ検証用の値型。
struct alignas(std::max_align_t) FFoundationAlignedRecord
{
    // 配列のreserve後も保持される検証値。
    uint64_t value;
};
}

namespace gk::tests
{

bool FoundationContracts()
{
#ifdef GKCORE_TESTING
    ResetAllocationFailureForTesting();
#endif
    static_assert(gk::kAllocationAlignment <= alignof(std::max_align_t), "Array allocation alignment must stay within malloc's standard guarantee");
    // malloc境界の型を格納する配列。
    Array<FFoundationAlignedRecord> alignedValues;
    // reserveとgrowthの後に保持を確かめる値。
    FFoundationAlignedRecord alignedValue = { 17 };
    if (!alignedValues.Reserve(1) || reinterpret_cast<uintptr_t>(alignedValues.Data()) % alignof(FFoundationAlignedRecord) != 0 || !alignedValues.Append(alignedValue) || alignedValues.At(0).value != 17)
        return false;
    alignedValue.value = 29;
    if (!alignedValues.Append(alignedValue) || reinterpret_cast<uintptr_t>(alignedValues.Data()) % alignof(FFoundationAlignedRecord) != 0 || alignedValues.Count() != 2 || alignedValues.At(0).value != 17 || alignedValues.At(1).value != 29)
        return false;

    Array<std::uint32_t> values;
    if (values.Count() != 0 || values.Data() != nullptr || !values.Append(10) || !values.Append(20))
        return false;
    if (values.Count() != 2 || values.At(0) != 10 || values.At(1) != 20 || !values.Reserve(16))
        return false;
    const std::uint32_t* stable = values.Data();
    if (!values.Append(30) || values.Data() != stable || values.Count() != 3)
        return false;
    values.Clear();
    if (values.Count() != 0 || !values.Append(40) || values.At(0) != 40)
        return false;
    Array<std::uint32_t> moved;
    moved.MoveFrom(values);
    if (values.Count() != 0 || moved.Count() != 1 || moved.At(0) != 40)
        return false;
    moved.Reset();
    if (moved.Count() != 0 || moved.Capacity() != 0)
        return false;
    if (!values.Append(99) || values.Reserve(UINT32_MAX) || values.Count() != 1 || values.At(0) != 99)
        return false;
    Array<uint32_t> aliases;
    if (!aliases.Append(1) || !aliases.Append(2) || !aliases.Append(3) || !aliases.Append(4))
        return false;
    Array<uint32_t> allocationFence;
    if (!allocationFence.Reserve(1024))
        return false;
    if (!aliases.Append(aliases.At(0)) || aliases.Count() != 5 || aliases.At(4) != 1)
        return false;
    Array<uint32_t> bulk;
    const uint32_t externalValues[] = { 7, 8, 9, 10 };
    if (!bulk.AppendRange(nullptr, 0) || bulk.AppendRange(nullptr, 1) || !bulk.AppendRange(externalValues, 3) || bulk.Count() != 3 || bulk.At(0) != 7 || bulk.At(1) != 8 || bulk.At(2) != 9)
        return false;
    Array<uint32_t> bulkAliases;
    if (!bulkAliases.Append(1) || !bulkAliases.Append(2) || !bulkAliases.Append(3) || !bulkAliases.Append(4))
        return false;
    const uint32_t* aliasedRange = bulkAliases.Data() + 1;
    if (!bulkAliases.AppendRange(aliasedRange, 3) || bulkAliases.Count() != 7 || bulkAliases.At(4) != 2 || bulkAliases.At(5) != 3 || bulkAliases.At(6) != 4)
        return false;
    if (bulkAliases.AppendRange(bulkAliases.Data() + 6, 2) || bulkAliases.Count() != 7)
        return false;
    if (bulkAliases.AppendRange(bulkAliases.Data() + 7, 1) || bulkAliases.Count() != 7)
        return false;
    const uint32_t* noDereference = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(1));
    if (bulk.AppendRange(noDereference, UINT32_MAX) || bulk.Count() != 3)
        return false;
    Array<uint8_t> countOverflow;
    const uint8_t* noByteDereference = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(1));
    if (!countOverflow.Append(1) || countOverflow.AppendRange(noByteDereference, UINT32_MAX) || countOverflow.Count() != 1 || countOverflow.At(0) != 1)
        return false;
#ifdef GKCORE_TESTING
    Array<uint32_t> failedBulk;
    if (!failedBulk.AppendRange(externalValues, 4))
        return false;
    const uint32_t* failedAlias = failedBulk.Data();
    SetAllocationFailureAfterForTesting(0);
    const bool bulkAllocationSucceeded = failedBulk.AppendRange(externalValues, 2);
    ResetAllocationFailureForTesting();
    if (bulkAllocationSucceeded || failedBulk.Count() != 4 || failedBulk.At(0) != 7 || failedBulk.At(3) != 10)
    {
        return false;
    }
    SetAllocationFailureAfterForTesting(0);
    const bool aliasAllocationSucceeded = failedBulk.AppendRange(failedAlias, 4);
    ResetAllocationFailureForTesting();
    if (aliasAllocationSucceeded || failedBulk.Count() != 4 || failedBulk.Data() != failedAlias || failedBulk.At(0) != 7 || failedBulk.At(3) != 10)
        return false;
#endif

    String message;
    if (!message.Assign("cannot load ") || !message.Append("image.bmp"))
        return false;
    if (message.Empty() || message.Length() != 21 || std::strcmp(message.CStr(), "cannot load image.bmp") != 0)
        return false;
    String movedMessage;
    movedMessage.MoveFrom(message);
    if (!message.Empty() || std::strcmp(movedMessage.CStr(), "cannot load image.bmp") != 0)
        return false;
    movedMessage.Clear();
    if (!movedMessage.Empty() || movedMessage.CStr()[0] != '\0' || message.Assign(nullptr))
        return false;
    if (!movedMessage.Assign("abcdef") || !movedMessage.Append(movedMessage.CStr(), movedMessage.Length()) || std::strcmp(movedMessage.CStr(), "abcdefabcdef") != 0)
        return false;
    if (!movedMessage.Assign(movedMessage.CStr() + 2, 4) || std::strcmp(movedMessage.CStr(), "cdef") != 0)
        return false;
    if (movedMessage.Assign("x", UINT32_MAX) || std::strcmp(movedMessage.CStr(), "cdef") != 0)
        return false;
    if (!movedMessage.Assign("id=") || !movedMessage.AppendUnsigned(UINT64_MAX) || std::strcmp(movedMessage.CStr(), "id=18446744073709551615") != 0)
        return false;

    ImageHandle invalid;
    ImageHandle first(1);
    ImageHandle second(2);
    if (invalid.IsValid() || static_cast<bool>(invalid) || !first.IsValid() || !static_cast<bool>(second))
        return false;
    int imageA = 11, imageB = 22, *removed = nullptr;
    HandleTable<ImageTag, int> images;
    if (!images.Insert(first, &imageA) || images.Find(first) != &imageA || images.Find(second) != nullptr)
        return false;
    if (images.Insert(first, &imageB) || !images.Insert(second, &imageB))
        return false;
    if (!images.Remove(first, removed) || removed != &imageA || images.Find(first) != nullptr)
        return false;
    images.Clear();
    if (images.Find(second) != nullptr)
        return false;

    RefCounted reference;
    reference.references = 1;
    reference.destroy = destroyRef;
    destroyedRefs = 0;
    if (!Retain(&reference) || reference.references != 2)
        return false;
    if (Release(&reference) != 1 || reference.references != 1 || destroyedRefs != 0)
        return false;
    if (Release(&reference) != 0 || destroyedRefs != 1)
        return false;
    if (Release(&reference) != 0 || destroyedRefs != 1)
        return false;
    RefCounted overflowReference = { UINT32_MAX, nullptr };
    if (Retain(&overflowReference) || overflowReference.references != UINT32_MAX)
        return false;
    if (Allocate(0) != nullptr)
        return false;
    if (Release(&overflowReference) != UINT32_MAX - 1)
        return false;
#ifdef GKCORE_TESTING
    String preserved;
    if (!preserved.Assign("stable"))
        return false;
    SetAllocationFailureAfterForTesting(0);
    const bool replaced = preserved.Assign("replacement text");
    ResetAllocationFailureForTesting();
    if (replaced || std::strcmp(preserved.CStr(), "stable") != 0)
        return false;

    SetAllocationFailureAfterForTesting(0);
    const bool appendedText = preserved.Append(" that cannot fit");
    ResetAllocationFailureForTesting();
    if (appendedText || std::strcmp(preserved.CStr(), "stable") != 0)
        return false;

    Array<uint32_t> full;
    if (!full.Append(1) || !full.Append(2) || !full.Append(3) || !full.Append(4))
        return false;
    SetAllocationFailureAfterForTesting(0);
    const bool appendedValue = full.Append(5);
    ResetAllocationFailureForTesting();
    if (appendedValue || full.Count() != 4 || full.At(0) != 1 || full.At(3) != 4)
        return false;

    void* original = Allocate(8);
    if (!original)
        return false;
    static_cast<unsigned char*>(original)[0] = 42;
    SetAllocationFailureAfterForTesting(0);
    void* resized = Reallocate(original, 64);
    ResetAllocationFailureForTesting();
    if (resized != nullptr || static_cast<unsigned char*>(original)[0] != 42)
    {
        Deallocate(resized ? resized : original);
        return false;
    }
    Deallocate(original);

    SetAllocationFailureAfterForTesting(1);
    void* firstAllocation = Allocate(8);
    void* secondAllocation = Allocate(8);
    ResetAllocationFailureForTesting();
    if (!firstAllocation || secondAllocation)
    {
        Deallocate(firstAllocation);
        Deallocate(secondAllocation);
        return false;
    }
    Deallocate(firstAllocation);
#endif
    return true;
}

} // namespace gk::tests
