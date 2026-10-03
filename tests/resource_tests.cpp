#include "../src/resources/Resources.h"
#include "../src/model/GlbLoader.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace gk::tests {
namespace {
class TempDirectory {
public:
    TempDirectory() {
        std::random_device random;
        for (uint32_t attempt = 0; attempt < 128; ++attempt) {
            const uint64_t nonce = (uint64_t(random()) << 32) ^ random() ^
                static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            path_ = std::filesystem::temp_directory_path() / ("gkcore-resource-" + std::to_string(nonce));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
        }
        throw std::runtime_error("could not create a unique resource test directory");
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        detail::ClearResources();
    }
    std::filesystem::path File(const char* name) const { return path_ / std::filesystem::u8path(name); }
private:
    std::filesystem::path path_;
};

const uint8_t pngBytes[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,
    0x89,0x00,0x00,0x00,0x0d,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x50,0x70,0x48,0x68,
    0x00,0x00,0x02,0x85,0x01,0x41,0x5c,0x42,0x11,0x57,0x00,0x00,0x00,0x00,0x49,0x45,
    0x4e,0x44,0xae,0x42,0x60,0x82
};

void PutU16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}
void PutU32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (uint32_t i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
void PutF32(std::vector<uint8_t>& bytes, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    PutU32(bytes, bits);
}
void Append(std::vector<uint8_t>& bytes, const void* data, size_t size) {
    const uint8_t* first = static_cast<const uint8_t*>(data);
    bytes.insert(bytes.end(), first, first + size);
}
bool Write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}
std::vector<uint8_t> Png() { return std::vector<uint8_t>(std::begin(pngBytes), std::end(pngBytes)); }
std::vector<uint8_t> MakeGlb(bool cyclicNodes = false, bool mixedMaterials = false,
                             uint32_t deepNodeCount = 0) {
    std::vector<uint8_t> binary;
    const float positions[9] = {0,0,0, 1,0,0, 0,1,0};
    for (float value : positions) PutF32(binary, value);
    const float uv[6] = {0,0, 1,0, 0,1};
    for (float value : uv) PutF32(binary, value);
    PutU16(binary, 0); PutU16(binary, 1); PutU16(binary, 2);
    while (binary.size() % 4) binary.push_back(0);
    const uint32_t imageOffset = static_cast<uint32_t>(binary.size());
    Append(binary, pngBytes, sizeof(pngBytes));
    const uint32_t imageLength = static_cast<uint32_t>(sizeof(pngBytes));
    while (binary.size() % 4) binary.push_back(0);

    std::string json = "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[";
    json += "0";
    json += "]}],\"nodes\":[";
    if (deepNodeCount) {
        for (uint32_t i = 0; i < deepNodeCount; ++i) {
            if (i) json += ",";
            json += "{";
            if (i + 1 < deepNodeCount) json += "\"children\":[" + std::to_string(i + 1) + "]";
            else json += "\"mesh\":0";
            json += "}";
        }
    } else {
        json += "{\"mesh\":0,\"translation\":[2,3,4]";
        if (cyclicNodes) json += ",\"children\":[0]";
        json += "}";
    }
    json += "],\"meshes\":[{\"primitives\":[";
    json += "{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":2,\"material\":0}";
    if (mixedMaterials)
        json += ", {\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":2}";
    json += "]}],\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.6,0.7,0.8],\"metallicFactor\":0.25,\"roughnessFactor\":0.4";
    if (!mixedMaterials) json += ",\"baseColorTexture\":{\"index\":0}";
    json += "}}],"
        "\"textures\":[{\"source\":0}],"
        "\"images\":[{\"bufferView\":3,\"mimeType\":\"image/png\"}],"
        "\"buffers\":[{\"byteLength\":" + std::to_string(binary.size()) + "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":60,\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(imageOffset) + ",\"byteLength\":" + std::to_string(imageLength) + "}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}";
    while (json.size() % 4) json.push_back(' ');
    std::vector<uint8_t> glb;
    PutU32(glb, 0x46546c67); PutU32(glb, 2);
    PutU32(glb, static_cast<uint32_t>(12 + 8 + json.size() + 8 + binary.size()));
    PutU32(glb, static_cast<uint32_t>(json.size())); PutU32(glb, 0x4e4f534a);
    Append(glb, json.data(), json.size());
    PutU32(glb, static_cast<uint32_t>(binary.size())); PutU32(glb, 0x004e4942);
    Append(glb, binary.data(), binary.size());
    return glb;
}
}

bool ResourceContract(String& failure) {
    TempDirectory temp;
    String error;
    const uint32_t noParent = 0xffffffffu;
    std::vector<uint32_t> parentIndices(65, noParent);
    for (uint32_t i = 1; i < parentIndices.size(); ++i) parentIndices[i] = i - 1;
    if (!detail::ValidateGlbParentsForTesting(parentIndices.data(), static_cast<uint32_t>(parentIndices.size()),
                                             64, error)) {
        failure.Assign("GLB parent depth 64 should be accepted: "); failure.Append(error.CStr()); return false;
    }
    parentIndices.push_back(64);
    if (detail::ValidateGlbParentsForTesting(parentIndices.data(), static_cast<uint32_t>(parentIndices.size()),
                                            65, error) || !std::strstr(error.CStr(), "hierarchy")) {
        failure.Assign("GLB parent depth 65 must be rejected even when the selected scene node is the leaf"); return false;
    }
    const auto pngPath = temp.File("rgba.png");
    if (!Write(pngPath, Png())) { failure.Assign("could not write PNG fixture"); return false; }
    const std::string pngPathUtf8 = pngPath.u8string();
    const ImageHandle imageHandle = detail::LoadImage(pngPathUtf8.c_str(), error);
    if (!imageHandle.IsValid()) { failure.Assign("valid PNG rejected: "); failure.Append(error.CStr()); return false; }
    detail::ImageResource* image = detail::FindImage(imageHandle);
    if (!image || image->width != 1 || image->height != 1 || image->rgba.Count() != 4 ||
        image->rgba.At(0) != 32 || image->rgba.At(1) != 64 || image->rgba.At(2) != 96 || image->rgba.At(3) != 128) {
        failure.Assign("PNG RGBA decode did not preserve source color and alpha"); return false;
    }
    if (!gk::Retain(&image->reference) || !detail::DeleteImage(imageHandle, error) || image->rgba.At(3) != 128) {
        failure.Assign("image handle deletion invalidated a retained frame payload"); return false;
    }
    gk::Release(&image->reference);

    const auto corruptPath = temp.File("truncated.png");
    std::vector<uint8_t> corrupt = Png(); corrupt.resize(44);
    Write(corruptPath, corrupt);
    const std::string corruptPathUtf8 = corruptPath.u8string();
    if (detail::LoadImage(corruptPathUtf8.c_str(), error).IsValid() || error.Empty()) {
        failure.Assign("truncated PNG must fail with an error"); return false;
    }

    const auto modelPath = temp.File(u8"日本語-scene.glb");
    if (!Write(modelPath, MakeGlb())) { failure.Assign("could not write GLB fixture"); return false; }
    const std::string modelPathUtf8 = modelPath.u8string();
    const ModelHandle modelHandle = detail::LoadModel(modelPathUtf8.c_str(), error);
    if (!modelHandle.IsValid()) { failure.Assign("valid GLB rejected: "); failure.Append(error.CStr()); return false; }
    detail::ModelResource* model = detail::FindModel(modelHandle);
    if (!model || model->vertices.Count() != 3 || model->indices.Count() != 3 || model->primitives.Count() != 1 ||
        model->materials.Count() != 1 || model->textures.Count() != 1) {
        failure.Assign("GLB static mesh or PBR payload missing: ");
        failure.AppendUnsigned(model ? model->vertices.Count() : 0);
        failure.Append(" vertices, ");
        failure.AppendUnsigned(model ? model->indices.Count() : 0);
        failure.Append(" indices, ");
        failure.AppendUnsigned(model ? model->primitives.Count() : 0);
        failure.Append(" primitives, ");
        failure.AppendUnsigned(model ? model->materials.Count() : 0);
        failure.Append(" materials, ");
        failure.AppendUnsigned(model ? model->textures.Count() : 0);
        failure.Append(" textures");
        return false;
    }
    if (model->vertices.At(0).position[0] != 2 || model->vertices.At(0).position[1] != 3 || model->vertices.At(0).position[2] != 4 ||
        model->materials.At(0).baseColorFactor[0] != 0.5f || model->materials.At(0).metallicFactor != 0.25f ||
        model->materials.At(0).roughnessFactor != 0.4f || model->materials.At(0).baseColorTextureIndex != 0 ||
        model->textures.At(0)->rgba.At(3) != 128) {
        failure.Assign("GLB node transform, PBR factors, or embedded PNG failed"); return false;
    }
    const auto oldValue = modelHandle.value;
    if (!gk::Retain(&model->reference) || !detail::DeleteModel(modelHandle, error) || model->indices.Count() != 3) {
        failure.Assign("model handle deletion invalidated a retained frame payload"); return false;
    }
    gk::Release(&model->reference);

    const auto objPath = temp.File("triangle.obj");
    { std::ofstream obj(objPath); obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n"; }
    const std::string objPathUtf8 = objPath.u8string();
    const ModelHandle objHandle = detail::LoadModel(objPathUtf8.c_str(), error);
    if (!objHandle.IsValid() || objHandle.value == oldValue) { failure.Assign("OBJ load or monotonic handle failed"); return false; }
    { std::ofstream obj(objPath); obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 999999999999999999999999999999999999999 2 3\n"; }
    if (detail::LoadModel(objPathUtf8.c_str(), error).IsValid() || error.Empty()) {
        failure.Assign("OBJ index outside int32 must be rejected"); return false;
    }
    const auto cyclePath = temp.File("cycle.glb");
    if (!Write(cyclePath, MakeGlb(true))) { failure.Assign("could not write cyclic GLB fixture"); return false; }
    const std::string cyclePathUtf8 = cyclePath.u8string();
    if (detail::LoadModel(cyclePathUtf8.c_str(), error).IsValid() || error.Empty()) {
        failure.Assign("GLB cyclic node parent graph must be rejected"); return false;
    }
    const auto deepPath = temp.File("deep-hierarchy.glb");
    if (!Write(deepPath, MakeGlb(false, false, 66))) { failure.Assign("could not write deep GLB fixture"); return false; }
    const std::string deepPathUtf8 = deepPath.u8string();
    if (detail::LoadModel(deepPathUtf8.c_str(), error).IsValid() ||
        !std::strstr(error.CStr(), "hierarchy")) {
        failure.Assign("GLB hierarchy deeper than 64 nodes must be rejected before scene validation: ");
        failure.Append(error.CStr());
        return false;
    }
    const auto materialsPath = temp.File("mixed-materials.glb");
    if (!Write(materialsPath, MakeGlb(false, true))) { failure.Assign("could not write mixed-material GLB"); return false; }
    const std::string materialsPathUtf8 = materialsPath.u8string();
    const ModelHandle materialsHandle = detail::LoadModel(materialsPathUtf8.c_str(), error);
    detail::ModelResource* materialsModel = detail::FindModel(materialsHandle);
    if (!materialsModel || materialsModel->primitives.Count() != 2 || materialsModel->materials.Count() != 2 ||
        materialsModel->primitives.At(0).materialIndex != 0 || materialsModel->primitives.At(1).materialIndex != 1 ||
        materialsModel->materials.At(0).baseColorFactor[0] != 0.5f ||
        materialsModel->materials.At(1).baseColorFactor[0] != 1.0f ||
        materialsModel->materials.At(1).baseColorTextureIndex != -1) {
        failure.Assign("unassigned GLB primitive must use the default material after an explicit first material"); return false;
    }
    detail::ClearResources();
    if (detail::FindModel(objHandle)) { failure.Assign("resource shutdown did not invalidate live handles"); return false; }
    return true;
}
}
