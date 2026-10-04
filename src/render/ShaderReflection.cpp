#include "ShaderReflection.h"

#if defined(_WIN32) && defined(DIRECT3D12)
#include <windows.h>
#include <objbase.h>
#include <dxcapi.h>
#include <d3d12shader.h>

namespace gk::render {
/**
 * Private DXC metadata conversion and COM lifetime helpers.
 */
namespace {

const uint32_t kMaximumReflectionBytecodeBytes = 64u * 1024u * 1024u;

/**
 * Releases one non-null COM interface and clears its pointer.
 */
template <typename T>
void Release(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

/**
 * Compares semantic names using case-insensitive ASCII matching without allocation.
 */
bool EqualText(const char* left, const char* right) {
    if (!left || !right) return false;
    while (*left && *right) {
        char leftChar = *left;
        char rightChar = *right;
        if (leftChar >= 'A' && leftChar <= 'Z') leftChar = static_cast<char>(leftChar + ('a' - 'A'));
        if (rightChar >= 'A' && rightChar <= 'Z') rightChar = static_cast<char>(rightChar + ('a' - 'A'));
        if (leftChar != rightChar) break;
        ++left;
        ++right;
    }
    return *left == *right;
}

/**
 * Replaces the diagnostic text and returns a validation failure.
 */
bool Fail(String& error, const char* message) {
    error.Assign(message);
    return false;
}

/**
 * Converts a Direct3D signature component to the portable shader descriptor.
 */
ShaderComponent NormalizeComponent(D3D_REGISTER_COMPONENT_TYPE component) {
    if (component == D3D_REGISTER_COMPONENT_FLOAT32) return ShaderComponent::Float32;
    if (component == D3D_REGISTER_COMPONENT_UINT32) return ShaderComponent::UInt32;
    if (component == D3D_REGISTER_COMPONENT_SINT32) return ShaderComponent::SInt32;
    return ShaderComponent::Unknown;
}

/**
 * Converts a reflected signature item to a renderer-independent semantic descriptor.
 */
ShaderInterfaceParameter NormalizeParameter(const D3D12_SIGNATURE_PARAMETER_DESC& source,
                                            bool output) {
    ShaderSemantic semantic = ShaderSemantic::Unknown;
    if (output && source.SystemValueType == D3D_NAME_TARGET) {
        semantic = ShaderSemantic::Target;
    } else if (!output && source.SystemValueType == D3D_NAME_POSITION) {
        semantic = ShaderSemantic::Position;
    } else if (!output && source.SystemValueType == D3D_NAME_UNDEFINED &&
               EqualText(source.SemanticName, "COLOR")) {
        semantic = ShaderSemantic::Color;
    } else if (!output && source.SystemValueType == D3D_NAME_UNDEFINED &&
               EqualText(source.SemanticName, "TEXCOORD")) {
        semantic = ShaderSemantic::Texcoord;
    }
    return {semantic, source.SemanticIndex, NormalizeComponent(source.ComponentType), source.Mask};
}

/**
 * Extracts enough constant-buffer type metadata for the portable ABI validator.
 */
void ReadConstantLayout(ID3D12ShaderReflection* reflection, const char* name,
                        ShaderInterfaceBinding& binding) {
    ID3D12ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByName(name);
    if (!buffer) return;
    D3D12_SHADER_BUFFER_DESC bufferDesc = {};
    if (FAILED(buffer->GetDesc(&bufferDesc)) || bufferDesc.Type != D3D_CT_CBUFFER ||
        bufferDesc.Variables != 1) return;
    binding.constantBytes = bufferDesc.Size;
    ID3D12ShaderReflectionVariable* variable = buffer->GetVariableByIndex(0);
    if (!variable) return;
    D3D12_SHADER_VARIABLE_DESC variableDesc = {};
    if (FAILED(variable->GetDesc(&variableDesc)) || variableDesc.StartOffset != 0 ||
        variableDesc.Size != 1024) return;
    ID3D12ShaderReflectionType* type = variable->GetType();
    if (!type) return;
    D3D12_SHADER_TYPE_DESC typeDesc = {};
    const HRESULT typeResult = type->GetDesc(&typeDesc);
    if (SUCCEEDED(typeResult) && typeDesc.Class == D3D_SVC_VECTOR) {
        binding.constantElements = typeDesc.Elements;
        binding.constantComponent = typeDesc.Type == D3D_SVT_FLOAT ?
                                   ShaderComponent::Float32 : ShaderComponent::Unknown;
        binding.constantClass = ShaderValueClass::Vector;
        binding.constantRows = typeDesc.Rows;
        binding.constantWidth = typeDesc.Columns;
    } else if (SUCCEEDED(typeResult)) {
        binding.constantClass = typeDesc.Class == D3D_SVC_SCALAR ? ShaderValueClass::Scalar :
                                (typeDesc.Class == D3D_SVC_MATRIX_ROWS ||
                                 typeDesc.Class == D3D_SVC_MATRIX_COLUMNS ?
                                 ShaderValueClass::Matrix : ShaderValueClass::Struct);
        binding.constantRows = typeDesc.Rows;
        binding.constantWidth = typeDesc.Columns;
    }
}

/**
 * Copies reflected signatures and resource slots into fixed-capacity portable metadata.
 */
bool NormalizeInterface(ID3D12ShaderReflection* reflection, const D3D12_SHADER_DESC& shader,
                        ShaderInterface& normalized, String& error) {
    normalized.stage = ShaderStage::Pixel;
    if (shader.InputParameters > kShaderInterfaceInputCapacity) {
        normalized.inputCount = kShaderInterfaceInputCapacity + 1;
    } else {
        normalized.inputCount = shader.InputParameters;
        for (UINT i = 0; i < shader.InputParameters; ++i) {
            D3D12_SIGNATURE_PARAMETER_DESC parameter = {};
            if (FAILED(reflection->GetInputParameterDesc(i, &parameter)))
                return Fail(error, "DXIL input signature could not be reflected");
            normalized.inputs[i] = NormalizeParameter(parameter, false);
        }
    }
    if (shader.OutputParameters > kShaderInterfaceOutputCapacity) {
        normalized.outputCount = kShaderInterfaceOutputCapacity + 1;
    } else {
        normalized.outputCount = shader.OutputParameters;
        for (UINT i = 0; i < shader.OutputParameters; ++i) {
            D3D12_SIGNATURE_PARAMETER_DESC parameter = {};
            if (FAILED(reflection->GetOutputParameterDesc(i, &parameter)))
                return Fail(error, "DXIL output signature could not be reflected");
            normalized.outputs[i] = NormalizeParameter(parameter, true);
        }
    }
    normalized.bindingCount = shader.BoundResources > kShaderInterfaceBindingCapacity ?
                              kShaderInterfaceBindingCapacity + 1 : shader.BoundResources;
    for (UINT i = 0; i < shader.BoundResources &&
                      i < kShaderInterfaceBindingCapacity; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC source = {};
        if (FAILED(reflection->GetResourceBindingDesc(i, &source)))
            return Fail(error, "DXIL resource binding could not be reflected");
        ShaderInterfaceBinding& target = normalized.bindings[i];
        target.bindPoint = source.BindPoint;
        target.space = source.Space;
        target.bindCount = source.BindCount;
        target.dimension = source.Dimension == D3D_SRV_DIMENSION_TEXTURE2D ?
                           ShaderDimension::Texture2D :
                           (source.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE ?
                            ShaderDimension::TextureCube : ShaderDimension::Other);
        if (source.Type == D3D_SIT_CBUFFER) {
            target.resource = ShaderResource::Constants;
            ReadConstantLayout(reflection, source.Name, target);
        } else if (source.Type == D3D_SIT_TEXTURE) {
            target.resource = ShaderResource::Texture;
            if (source.ReturnType == D3D_RETURN_TYPE_FLOAT ||
                source.ReturnType == D3D_RETURN_TYPE_UNORM ||
                source.ReturnType == D3D_RETURN_TYPE_SNORM)
                target.textureComponent = ShaderComponent::Float32;
            else if (source.ReturnType == D3D_RETURN_TYPE_UINT)
                target.textureComponent = ShaderComponent::UInt32;
            else if (source.ReturnType == D3D_RETURN_TYPE_SINT)
                target.textureComponent = ShaderComponent::SInt32;
        } else if (source.Type == D3D_SIT_SAMPLER) {
            target.resource = ShaderResource::Sampler;
        } else {
            target.resource = ShaderResource::Unknown;
        }
    }
    return true;
}

} // namespace

/**
 * Reflects a DXIL container and validates the pixel shader interface without creating a device.
 */
bool ValidatePixelShaderReflection(const void* bytecode, uint32_t bytes,
                                   ShaderBindingUsage& output, String& error) {
    error.Clear();
    if (!bytecode || bytes == 0) return Fail(error, "pixel shader bytecode is empty");
    if (bytes > kMaximumReflectionBytecodeBytes)
        return Fail(error, "pixel shader bytecode exceeds the 64 MiB limit");

    IDxcUtils* utils = nullptr;
    IDxcBlobEncoding* blob = nullptr;
    IDxcContainerReflection* container = nullptr;
    ID3D12ShaderReflection* reflection = nullptr;
    ShaderBindingUsage candidate = {false, false, false};
    bool valid = false;

    HRESULT result = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (SUCCEEDED(result))
        result = utils->CreateBlobFromPinned(bytecode, bytes, 0, &blob);
    if (SUCCEEDED(result))
        result = DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&container));
    if (SUCCEEDED(result)) result = container->Load(blob);
    UINT32 dxilPart = 0;
    if (SUCCEEDED(result)) result = container->FindFirstPartKind(DXC_PART_DXIL, &dxilPart);
    if (SUCCEEDED(result))
        result = container->GetPartReflection(dxilPart, IID_ID3D12ShaderReflection,
                                              reinterpret_cast<void**>(&reflection));
    if (FAILED(result) || !reflection) {
        error.Assign("DXIL container reflection could not be created");
    } else {
        D3D12_SHADER_DESC shader = {};
        if (FAILED(reflection->GetDesc(&shader))) {
            error.Assign("DXIL shader description could not be reflected");
        } else if (D3D12_SHVER_GET_TYPE(shader.Version) != D3D12_SHVER_PIXEL_SHADER) {
            error.Assign("shader bytecode is not a pixel shader");
        } else {
            ShaderInterface normalized = {};
            valid = NormalizeInterface(reflection, shader, normalized, error) &&
                    ValidatePixelShaderInterface(normalized, candidate, error);
        }
    }
    Release(reflection);
    Release(container);
    Release(blob);
    Release(utils);
    if (valid) output = candidate;
    return valid;
}

} // namespace gk::render

#else

namespace gk::render {

/**
 * Reports that native Direct3D 12 reflection is unavailable on this platform.
 */
bool ValidatePixelShaderReflection(const void*, uint32_t, ShaderBindingUsage&, String& error) {
    error.Assign("pixel shader reflection requires the Direct3D 12 Windows runtime");
    return false;
}

} // namespace gk::render

#endif
