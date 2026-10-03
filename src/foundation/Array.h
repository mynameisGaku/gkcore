#pragma once

#include "Memory.h"
#include <stddef.h>
#include <stdint.h>

/**
 * Dynamic storage helpers for POD runtime records.
 */
namespace gk {

/**
 * Owns a growable sequence of trivially copyable records. Elements must not
 * own resources or require construction and destruction.
 */
class RawArray {
public:
    /**
     * Creates an empty array for records of the specified byte size.
     */
    explicit RawArray(uint32_t elementSize);
    /**
     * Releases the backing allocation.
     */
    ~RawArray();
    RawArray(const RawArray&) = delete;
    RawArray& operator=(const RawArray&) = delete;

    /**
     * Ensures capacity for at least the requested number of records.
     */
    bool Reserve(uint32_t capacity);
    /**
     * Appends one record while preserving a record aliased into this array.
     */
    bool Append(const void* element);
    /**
     * Removes all records while retaining allocated capacity.
     */
    void Clear();
    /**
     * Removes all records and releases allocated capacity.
     */
    void Reset();
    /**
     * Transfers storage from another array of the same record size.
     */
    void MoveFrom(RawArray& source);
    /**
     * Returns contiguous mutable storage, or null when empty.
     */
    void* Data();
    /**
     * Returns contiguous read-only storage, or null when empty.
     */
    const void* Data() const;
    /**
     * Returns the number of stored records.
     */
    uint32_t Count() const;
    /**
     * Returns allocated record capacity.
     */
    uint32_t Capacity() const;
    /**
     * Removes a record and preserves the order of later records.
     */
    bool RemoveAt(uint32_t index);

private:
    void* data_;
    uint32_t elementSize_;
    uint32_t count_;
    uint32_t capacity_;
};

/**
 * Typed view over RawArray for trivially copyable values only.
 */
template<class T>
class Array {
    static_assert(__is_trivially_copyable(T), "gk::Array only stores trivially copyable values");
    static_assert(alignof(T) <= alignof(max_align_t), "gk::Array does not support over-aligned values");
public:
    /**
     * Creates an empty array.
     */
    Array() : storage_(static_cast<uint32_t>(sizeof(T))) {}
    /**
     * Releases the backing allocation.
     */
    ~Array() = default;
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    /**
     * Ensures capacity for at least the requested number of records.
     */
    bool Reserve(uint32_t capacity) { return storage_.Reserve(capacity); }
    /**
     * Appends a record, including when value aliases an existing element.
     */
    bool Append(const T& value) { return storage_.Append(&value); }
    /**
     * Removes all records while retaining capacity.
     */
    void Clear() { storage_.Clear(); }
    /**
     * Removes all records and releases capacity.
     */
    void Reset() { storage_.Reset(); }
    /**
     * Transfers storage from another array.
     */
    void MoveFrom(Array& source) { storage_.MoveFrom(source.storage_); }
    /**
     * Returns contiguous mutable record storage, or null when empty.
     */
    T* Data() { return static_cast<T*>(storage_.Data()); }
    /**
     * Returns contiguous read-only record storage, or null when empty.
     */
    const T* Data() const { return static_cast<const T*>(storage_.Data()); }
    /**
     * Returns a record by index; the caller must check Count().
     */
    T& At(uint32_t index) { return Data()[index]; }
    /**
     * Returns a record by index; the caller must check Count().
     */
    const T& At(uint32_t index) const { return Data()[index]; }
    /**
     * Returns the current number of records.
     */
    uint32_t Count() const { return storage_.Count(); }
    /**
     * Returns allocated record capacity.
     */
    uint32_t Capacity() const { return storage_.Capacity(); }
    /**
     * Removes the indexed record, preserving the order of later values.
     */
    bool RemoveAt(uint32_t index) { return storage_.RemoveAt(index); }

private:
    RawArray storage_;
};

} // namespace gk
