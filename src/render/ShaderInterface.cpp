#include "render/ShaderInterface.h"

/**
 * Validates fixed-capacity shader interface metadata independently of platform APIs.
 */
namespace gk::render
{
/**
 * Private validation helpers for normalized shader metadata.
 */
namespace
{

/**
 * Replaces the diagnostic text and returns a validation failure.
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * Checks that one input signature item matches a supported vertex-to-pixel semantic.
 */
bool ValidInput(const ShaderInterfaceParameter& parameter)
{
    const bool semantic = parameter.semantic == ShaderSemantic::Position || parameter.semantic == ShaderSemantic::Color || parameter.semantic == ShaderSemantic::Texcoord;
    const bool maskValid = parameter.semantic == ShaderSemantic::Texcoord ? (parameter.mask != 0 && (parameter.mask & ~0x03u) == 0) : (parameter.mask != 0 && (parameter.mask & ~0x0fu) == 0);
    return semantic && parameter.semanticIndex == 0 && parameter.component == ShaderComponent::Float32 && parameter.mask != 0 && maskValid;
}

/**
 * Checks one binding against the renderer's fixed shader resource layout.
 */
bool ValidBinding(const ShaderInterfaceBinding& binding, ShaderBindingUsage& usage, String& error)
{
    if (binding.bindCount != 1)
        return Fail(error, "resource binding arrays are unsupported");
    switch (binding.resource)
    {
    case ShaderResource::Constants:
        if (usage.constants || binding.bindPoint != 0 || binding.space != 3 || binding.constantBytes != kConstantBlockBytes || binding.constantElements != kConstantSlotCount || binding.constantComponent != ShaderComponent::Float32 || binding.constantClass != ShaderValueClass::Vector || binding.constantRows != 1 || binding.constantWidth != 4)
            return Fail(error, "constant buffer must be float4[64] at b0, space3");
        usage.constants = true;
        return true;
    case ShaderResource::Texture:
        if (usage.texture || binding.bindPoint != 0 || binding.space != 0 || binding.dimension != ShaderDimension::Texture2D || binding.textureComponent != ShaderComponent::Float32)
            return Fail(error, "texture binding must be one Texture2D at t0, space0");
        usage.texture = true;
        return true;
    case ShaderResource::Sampler:
        if (usage.sampler || binding.bindPoint != 0 || binding.space != 0)
            return Fail(error, "sampler binding must be one sampler at s0, space0");
        usage.sampler = true;
        return true;
    default:
        return Fail(error, "pixel shader declares an unsupported resource");
    }
}

} // namespace

/**
 * Validates one fixed-capacity pixel-shader signature and resource layout.
 */
bool ValidatePixelShaderInterface(const ShaderInterface& shader, ShaderBindingUsage& output, String& error)
{
    error.Clear();
    if (shader.stage != ShaderStage::Pixel)
        return Fail(error, "shader bytecode is not a pixel shader");
    if (shader.inputCount > kShaderInterfaceInputCapacity || shader.outputCount > kShaderInterfaceOutputCapacity || shader.bindingCount > kShaderInterfaceBindingCapacity)
        return Fail(error, "shader interface exceeds supported metadata capacity");
    for (uint32_t i = 0; i < shader.inputCount; ++i)
    {
        if (!ValidInput(shader.inputs[i]))
            return Fail(error, "pixel shader input is outside the supported float signature");
        for (uint32_t previous = 0; previous < i; ++previous)
        {
            if (shader.inputs[previous].semantic == shader.inputs[i].semantic && shader.inputs[previous].semanticIndex == shader.inputs[i].semanticIndex)
                return Fail(error, "pixel shader input semantics must be unique");
        }
    }
    if (shader.outputCount != 1)
        return Fail(error, "pixel shader must write exactly one render target");
    const ShaderInterfaceParameter& target = shader.outputs[0];
    if (target.semantic != ShaderSemantic::Target || target.semanticIndex != 0 || target.component != ShaderComponent::Float32 || target.mask != 0x0f)
        return Fail(error, "pixel shader output must be float4 SV_Target0");

    ShaderBindingUsage candidate = { false, false, false };
    for (uint32_t i = 0; i < shader.bindingCount; ++i)
    {
        if (!ValidBinding(shader.bindings[i], candidate, error))
            return false;
    }
    output = candidate;
    return true;
}

} // namespace gk::render
