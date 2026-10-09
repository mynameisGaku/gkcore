#pragma once

#include "foundation/String.h"

#include <stdint.h>

/**
 * Compact reflected shader metadata validated independently of Direct3D.
 */
namespace gk::render
{

inline constexpr uint32_t kShaderInterfaceInputCapacity = 3;
inline constexpr uint32_t kShaderInterfaceOutputCapacity = 1;
inline constexpr uint32_t kShaderInterfaceBindingCapacity = 3;
inline constexpr uint32_t kConstantBlockBytes = 1024;
inline constexpr uint32_t kConstantSlotCount = 64;

/**
 * Reflected shader program stage.
 */
enum class ShaderStage : uint8_t
{
    Unknown,
    Vertex,
    Pixel
};
/**
 * Normalized signature semantic accepted by the renderer.
 */
enum class ShaderSemantic : uint8_t
{
    Unknown,
    Position,
    Color,
    Texcoord,
    Target
};
/**
 * Scalar component encoding reported by reflection.
 */
enum class ShaderComponent : uint8_t
{
    Unknown,
    Float32,
    UInt32,
    SInt32
};
/**
 * Constant value class reported by reflection.
 */
enum class ShaderValueClass : uint8_t
{
    Unknown,
    Scalar,
    Vector,
    Matrix,
    Struct
};
/**
 * Resource category reported by reflection.
 */
enum class ShaderResource : uint8_t
{
    Unknown,
    Constants,
    Texture,
    Sampler
};
/**
 * Texture dimensionality reported by reflection.
 */
enum class ShaderDimension : uint8_t
{
    None,
    Texture2D,
    TextureCube,
    Other
};

/**
 * One reflected signature parameter, with semantic already normalized.
 */
struct ShaderInterfaceParameter
{
    ShaderSemantic semantic;
    uint32_t semanticIndex;
    ShaderComponent component;
    uint32_t mask;
};

/**
 * One reflected binding and optional constant-buffer shape.
 */
struct ShaderInterfaceBinding
{
    ShaderResource resource;
    uint32_t bindPoint;
    uint32_t space;
    uint32_t bindCount;
    ShaderDimension dimension;
    uint32_t constantBytes;
    uint32_t constantElements;
    ShaderComponent constantComponent;
    ShaderValueClass constantClass;
    uint32_t constantRows;
    uint32_t constantWidth;
    ShaderComponent textureComponent;
};

/**
 * Fixed-capacity metadata for one shader stage.
 */
struct ShaderInterface
{
    ShaderStage stage;
    uint32_t inputCount;
    ShaderInterfaceParameter inputs[kShaderInterfaceInputCapacity];
    uint32_t outputCount;
    ShaderInterfaceParameter outputs[kShaderInterfaceOutputCapacity];
    uint32_t bindingCount;
    ShaderInterfaceBinding bindings[kShaderInterfaceBindingCapacity];
};

/**
 * Resource declarations consumed by the renderer.
 */
struct ShaderBindingUsage
{
    bool constants;
    bool texture;
    bool sampler;
};

/**
 * Validates a normalized pixel-shader signature and supported resource bindings.
 * Output usage is replaced only after the complete descriptor passes validation.
 */
bool ValidatePixelShaderInterface(const ShaderInterface& shader, ShaderBindingUsage& output, String& error);

} // namespace gk::render
