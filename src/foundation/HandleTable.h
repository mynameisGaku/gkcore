#pragma once

#include "Array.h"
#include "../../include/gkcore/Handle.h"

/**
 * Non-owning handle lookup table backed by a POD array.
 */
namespace gk {

/**
 * Stores non-owning object pointers under unique typed handles. The caller
 * assigns monotonic identifiers and remains responsible for object lifetime.
 */
template<class Tag, class T>
class HandleTable {
public:
    /**
     * Inserts a valid, unique handle and a non-null object pointer.
     */
    bool Insert(Handle<Tag> handle, T* object) {
        if (!handle.IsValid() || !object || Find(handle)) return false;
        Entry entry = {handle.value, object};
        return entries_.Append(entry);
    }

    /**
     * Returns the object for a live handle, or null.
     */
    T* Find(Handle<Tag> handle) const {
        if (!handle.IsValid()) return nullptr;
        for (uint32_t i = 0; i < entries_.Count(); ++i) {
            if (entries_.At(i).handle == handle.value) return entries_.At(i).object;
        }
        return nullptr;
    }

    /**
     * Removes a handle and returns its object without destroying it.
     */
    bool Remove(Handle<Tag> handle, T*& object) {
        object = nullptr;
        if (!handle.IsValid()) return false;
        for (uint32_t i = 0; i < entries_.Count(); ++i) {
            if (entries_.At(i).handle == handle.value) {
                object = entries_.At(i).object;
                return entries_.RemoveAt(i);
            }
        }
        return false;
    }

    /**
     * Removes all entries without taking ownership of their objects.
     */
    void Clear() { entries_.Clear(); }
    /**
     * Returns the number of entries.
     */
    uint32_t Count() const { return entries_.Count(); }

private:
    struct Entry {
        uint32_t handle;
        T* object;
    };
    Array<Entry> entries_;
};

} // namespace gk
