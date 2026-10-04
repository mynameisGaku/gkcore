#include "../src/render/ShaderInterface.h"

#include <stdio.h>

namespace {

using namespace gk::render;

bool Check(bool value, const char* label) {
    if (!value) fprintf(stderr, "shader interface test failed: %s\n", label);
    return value;
}

bool Validate(const ShaderInterface& shader, ShaderBindingUsage& usage, gk::String& error) {
    return ValidatePixelShaderInterface(shader, usage, error);
}

ShaderInterface ValidMinimal() {
    ShaderInterface shader = {};
    shader.stage = ShaderStage::Pixel;
    shader.outputCount = 1;
    shader.outputs[0] = {ShaderSemantic::Target, 0, ShaderComponent::Float32, 0x0f};
    return shader;
}

ShaderInterface ValidFull() {
    ShaderInterface shader = ValidMinimal();
    shader.inputCount = 3;
    shader.inputs[0] = {ShaderSemantic::Position, 0, ShaderComponent::Float32, 0x0f};
    shader.inputs[1] = {ShaderSemantic::Color, 0, ShaderComponent::Float32, 0x0f};
    shader.inputs[2] = {ShaderSemantic::Texcoord, 0, ShaderComponent::Float32, 0x03};
    shader.bindingCount = 3;
    shader.bindings[0] = {ShaderResource::Constants, 0, 3, 1, ShaderDimension::None,
                          kConstantBlockBytes, kConstantSlotCount, ShaderComponent::Float32,
                          ShaderValueClass::Vector, 1, 4, ShaderComponent::Unknown};
    shader.bindings[1] = {ShaderResource::Texture, 0, 0, 1, ShaderDimension::Texture2D,
                          0, 0, ShaderComponent::Unknown, ShaderValueClass::Unknown, 0, 0,
                          ShaderComponent::Float32};
    shader.bindings[2] = {ShaderResource::Sampler, 0, 0, 1, ShaderDimension::None,
                          0, 0, ShaderComponent::Unknown, ShaderValueClass::Unknown, 0, 0,
                          ShaderComponent::Unknown};
    return shader;
}

bool ExpectRejected(ShaderInterface shader, const char* label) {
    ShaderBindingUsage usage = {true, true, true};
    gk::String error;
    const bool result = Validate(shader, usage, error);
    return Check(!result, label) && Check(usage.constants && usage.texture && usage.sampler,
                                          "failure leaves usage unchanged");
}

} // namespace

int main() {
    gk::String error;
    ShaderBindingUsage usage = {false, false, false};
    ShaderInterface minimal = ValidMinimal();
    if (!Check(Validate(minimal, usage, error), "minimal pixel shader accepted") ||
        !Check(!usage.constants && !usage.texture && !usage.sampler,
               "unused declarations remain optional")) return 1;

    ShaderInterface full = ValidFull();
    if (!Check(Validate(full, usage, error), "full supported interface accepted") ||
        !Check(usage.constants && usage.texture && usage.sampler,
               "full interface usage is reported")) return 1;

    ShaderInterface bad = full;
    bad.stage = ShaderStage::Vertex;
    if (!ExpectRejected(bad, "wrong shader stage rejected")) return 1;
    bad = full;
    bad.inputs[1].component = ShaderComponent::UInt32;
    if (!ExpectRejected(bad, "integer input rejected")) return 1;
    bad = full;
    bad.inputs[1].semantic = ShaderSemantic::Unknown;
    if (!ExpectRejected(bad, "unknown input semantic rejected")) return 1;
    bad = full;
    bad.inputs[1].semanticIndex = 1;
    if (!ExpectRejected(bad, "nonzero input semantic index rejected")) return 1;
    bad = full;
    bad.inputs[1].mask = 0x1f;
    if (!ExpectRejected(bad, "input mask outside known components rejected")) return 1;
    bad = full;
    bad.inputs[1].mask = 0;
    if (!ExpectRejected(bad, "zero-use input mask rejected")) return 1;
    bad = full;
    bad.inputs[2].mask = 0x0f;
    if (!ExpectRejected(bad, "float4 texture coordinate input rejected")) return 1;
    bad = full;
    bad.inputs[2].semantic = ShaderSemantic::Color;
    if (!ExpectRejected(bad, "duplicate pixel input semantic rejected")) return 1;
    bad = full;
    bad.inputs[2].mask = 0x01;
    usage = {false, false, false};
    if (!Check(Validate(bad, usage, error), "partial float2 coordinate mask accepted")) return 1;
    bad = full;
    bad.outputCount = 2;
    if (!ExpectRejected(bad, "extra render target rejected")) return 1;
    bad = full;
    bad.outputs[0].semanticIndex = 1;
    if (!ExpectRejected(bad, "nonzero render target index rejected")) return 1;
    bad = full;
    bad.outputs[0].mask = 0x07;
    if (!ExpectRejected(bad, "non-float4 render target rejected")) return 1;
    bad = full;
    bad.bindings[0].constantBytes = 1008;
    if (!ExpectRejected(bad, "wrong constant buffer size rejected")) return 1;
    bad = full;
    bad.bindings[0].constantElements = 63;
    if (!ExpectRejected(bad, "wrong constant array length rejected")) return 1;
    bad = full;
    bad.bindings[0].constantClass = ShaderValueClass::Matrix;
    if (!ExpectRejected(bad, "wrong constant value class rejected")) return 1;
    bad = full;
    bad.bindings[0].space = 2;
    if (!ExpectRejected(bad, "wrong constant buffer space rejected")) return 1;
    bad = full;
    bad.bindings[0].bindCount = 2;
    if (!ExpectRejected(bad, "constant buffer array rejected")) return 1;
    bad = full;
    bad.bindings[1].dimension = ShaderDimension::TextureCube;
    if (!ExpectRejected(bad, "non-2D texture rejected")) return 1;
    bad = full;
    bad.bindings[1].bindPoint = 1;
    if (!ExpectRejected(bad, "wrong texture register rejected")) return 1;
    bad = full;
    bad.bindings[1].textureComponent = ShaderComponent::UInt32;
    if (!ExpectRejected(bad, "integer texture return type rejected")) return 1;
    bad = full;
    bad.bindings[2].bindPoint = 1;
    if (!ExpectRejected(bad, "wrong sampler register rejected")) return 1;
    bad = full;
    bad.bindings[2].space = 1;
    if (!ExpectRejected(bad, "wrong sampler space rejected")) return 1;
    bad = full;
    bad.bindingCount = 4;
    if (!ExpectRejected(bad, "too many bindings rejected")) return 1;
    bad = full;
    bad.bindingCount = 1;
    bad.bindings[0].resource = ShaderResource::Unknown;
    if (!ExpectRejected(bad, "unknown resource type rejected")) return 1;
    bad = full;
    bad.bindingCount = 2;
    bad.bindings[0] = bad.bindings[1];
    bad.bindings[1] = bad.bindings[0];
    if (!ExpectRejected(bad, "duplicate texture binding rejected")) return 1;

    return 0;
}
