#pragma once

#include <stdint.h>

/**
 * Typed resource identifiers used by public gkcore calls.
 */
namespace gk {

/**
 * A small typed identifier. Value zero is reserved as invalid.
 */
template<class Tag>
struct Handle {
    uint32_t value;

    /**
     * Constructs an invalid handle.
     */
    Handle() : value(0) {}
    /**
     * Constructs a handle from its nonzero identifier.
     */
    explicit Handle(uint32_t id) : value(id) {}
    /**
     * Returns whether this identifier refers to a resource.
     */
    bool IsValid() const { return value != 0; }
    /**
     * Converts to true when this identifier is valid.
     */
    explicit operator bool() const { return IsValid(); }
    /**
     * Compares two identifiers.
     */
    bool operator==(Handle other) const { return value == other.value; }
    /**
     * Compares two identifiers.
     */
    bool operator!=(Handle other) const { return value != other.value; }
};

/**
 * Tag used to distinguish image handles from other resource IDs.
 */
struct ImageTag;
/**
 * Tag used to distinguish model handles from other resource IDs.
 */
struct ModelTag;
/**
 * Tag used to distinguish shader handles from other resource IDs.
 */
struct ShaderTag;
/**
 * Typed identifier for a loaded image.
 */
using ImageHandle = Handle<ImageTag>;
/**
 * Typed identifier for a loaded model.
 */
using ModelHandle = Handle<ModelTag>;
/**
 * Typed identifier for a loaded custom shader.
 */
using ShaderHandle = Handle<ShaderTag>;

} // namespace gk
