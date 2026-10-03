#pragma once

#include <stdint.h>

/**
 * Owned UTF-8 text for paths and diagnostic messages.
 */
namespace gk {

/**
 * Owns a null-terminated UTF-8 byte string for API errors and resource paths.
 */
class String {
public:
    /**
     * Creates an empty string.
     */
    String();
    /**
     * Releases owned text storage.
     */
    ~String();
    String(const String&) = delete;
    String& operator=(const String&) = delete;

    /**
     * Replaces text and preserves the old value if allocation fails.
     */
    bool Assign(const char* text);
    /**
     * Replaces text with an explicit byte range.
     */
    bool Assign(const char* text, uint32_t length);
    /**
     * Appends a null-terminated string.
     */
    bool Append(const char* text);
    /**
     * Appends an explicit byte range.
     */
    bool Append(const char* text, uint32_t length);
    /**
     * Appends an unsigned base-10 number.
     */
    bool AppendUnsigned(uint64_t value);
    /**
     * Clears text while retaining capacity.
     */
    void Clear();
    /**
     * Clears text and releases allocated storage.
     */
    void Reset();
    /**
     * Transfers storage and content from another string.
     */
    void MoveFrom(String& source);
    /**
     * Returns true when the string has no bytes.
     */
    bool Empty() const;
    /**
     * Returns the byte length, excluding the terminator.
     */
    uint32_t Length() const;
    /**
     * Returns a null-terminated view valid until mutation.
     */
    const char* CStr() const;

private:
    char* data_;
    uint32_t length_;
    uint32_t capacity_;
};

} // namespace gk
