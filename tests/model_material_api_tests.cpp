// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include "resources/Resources.h"

#include <filesystem>
#include <fstream>
#include <stdio.h>
#include <string>
#include <vector>

namespace
{
class FTemporaryDirectory
{
  public:
    FTemporaryDirectory()
    {
        static uint32_t next = 1;
        path_ = std::filesystem::temp_directory_path() / ("gkcore-model-material-api-" + std::to_string(next++));
        std::error_code error;
        std::filesystem::create_directories(path_, error);
    }
    ~FTemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        gk::detail::ClearResources();
    }
    const std::filesystem::path& Path() const
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

bool Write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

bool WriteObj(const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::binary);
    file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    return static_cast<bool>(file);
}

bool Check(bool condition, const char* message)
{
    if (!condition)
        fprintf(stderr, "%s\n", message);
    return condition;
}
}

int main()
{
    FTemporaryDirectory temporary;
    const auto modelPath = temporary.Path() / "triangle.obj";
    const auto imagePath = temporary.Path() / "pixel.png";
    const auto secondImagePath = temporary.Path() / "second-pixel.png";
    const std::vector<uint8_t> png = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x50, 0x70, 0x48, 0x68, 0x00, 0x00, 0x02, 0x85, 0x01, 0x41, 0x5c, 0x42, 0x11, 0x57, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
    if (!Check(WriteObj(modelPath) && Write(imagePath, png) && Write(secondImagePath, png), "could not write model material fixtures"))
        return 1;
    const std::string modelUtf8 = modelPath.u8string();
    const std::string imageUtf8 = imagePath.u8string();
    const std::string secondImageUtf8 = secondImagePath.u8string();
    const gk::ModelHandle modelHandle = gk::LoadModel(modelUtf8.c_str());
    // 別の材質役割とも共有する既存画像handle。
    const gk::ImageHandle legacyImageHandle = gk::LoadImage(imageUtf8.c_str());
    // GLB等で基本色とMRが同じslotを参照する状態を作る元resource。
    auto* sourceModel = gk::detail::FindModel(modelHandle);
    // 旧base/MR slotが参照する画像payload。
    auto* legacyImage = gk::detail::FindImage(legacyImageHandle);
    if (!Check(legacyImageHandle.IsValid() && sourceModel && legacyImage, "could not create a shared base and MR image"))
        return 1;
    if (!gk::Retain(&legacyImage->reference))
        return 1;
    if (!sourceModel->textures.Append(legacyImage))
    {
        gk::Release(&legacyImage->reference);
        Check(false, "could not prepare a shared base and MR texture slot");
        return 1;
    }
    sourceModel->materials.At(0).baseColorTextureIndex = 0;
    sourceModel->materials.At(0).metallicRoughnessTextureIndex = 0;
    sourceModel->materials.At(0).baseColorSampler.addressU = gk::detail::ETextureAddressMode::Repeat;
    sourceModel->materials.At(0).baseColorSampler.addressV = gk::detail::ETextureAddressMode::MirroredRepeat;
    sourceModel->materials.At(0).baseColorSampler.minFilter = gk::detail::ETextureFilter::Nearest;
    sourceModel->materials.At(0).baseColorSampler.magFilter = gk::detail::ETextureFilter::Nearest;
    sourceModel->materials.At(0).baseColorSampler.mipFilter = gk::detail::ETextureMipFilter::Nearest;
    sourceModel->materials.At(0).metallicRoughnessSampler.addressU = gk::detail::ETextureAddressMode::MirroredRepeat;
    sourceModel->materials.At(0).metallicRoughnessSampler.magFilter = gk::detail::ETextureFilter::Nearest;
    const gk::ModelHandle instanceHandle = gk::CreateModelInstance(modelHandle);
    if (!Check(modelHandle.IsValid() && instanceHandle.IsValid() && gk::GetModelMaterialCount(modelHandle) == 1, "model material API did not expose the default material"))
        return 1;
    auto* oldModel = gk::detail::FindModel(modelHandle);
    if (!Check(oldModel && gk::Retain(&oldModel->reference), "could not retain the original model snapshot"))
        return 1;
    auto* oldInstance = gk::detail::FindModel(instanceHandle);
    const gk::ImageHandle imageHandle = gk::LoadImage(imageUtf8.c_str());
    gk::FModelMaterialSettings settings;
    settings.baseColorFactor[0] = 0.5f;
    settings.baseColorFactor[1] = 0.75f;
    settings.baseColorFactor[2] = 0.25f;
    settings.baseColorFactor[3] = 0.8f;
    settings.baseColorImage = imageHandle;
    settings.alphaMode = gk::EModelAlphaMode::Blend;
    settings.alphaCutoff = 0.4f;
    if (!Check(imageHandle.IsValid() && gk::SetModelMaterial(modelHandle, 0, settings) == 0, "valid model material update failed"))
        return 1;
    auto* updated = gk::detail::FindModel(modelHandle);
    if (!Check(updated && updated != oldModel && oldInstance == oldModel && oldModel->textures.Count() == 1 && oldModel->materials.At(0).baseColorFactor[0] == 1.0f && oldModel->materials.At(0).baseColorTextureIndex == 0, "material update changed a queued or shared model snapshot"))
        return 1;
    const auto& updatedMaterial = updated->materials.At(0);
    if (!Check(updatedMaterial.baseColorFactor[0] == 0.5f && updatedMaterial.baseColorFactor[1] == 0.75f && updatedMaterial.baseColorFactor[2] == 0.25f && updatedMaterial.baseColorFactor[3] == 0.8f && updatedMaterial.alphaBlend && !updatedMaterial.alphaMask && updatedMaterial.baseColorTextureIndex == 1 && updatedMaterial.metallicRoughnessTextureIndex == 0 && updated->textures.Count() == 2 && updated->textures.At(0) == legacyImage, "material update lost role-specific texture ownership"))
        return 1;
    if (!Check(updatedMaterial.baseColorSampler.minFilter == gk::detail::ETextureFilter::Linear && updatedMaterial.baseColorSampler.mipFilter == gk::detail::ETextureMipFilter::Linear && updatedMaterial.baseColorSampler.magFilter == gk::detail::ETextureFilter::Nearest && updatedMaterial.baseColorSampler.addressU == gk::detail::ETextureAddressMode::Repeat && updatedMaterial.baseColorSampler.addressV == gk::detail::ETextureAddressMode::MirroredRepeat && updatedMaterial.metallicRoughnessSampler.addressU == gk::detail::ETextureAddressMode::MirroredRepeat && updatedMaterial.metallicRoughnessSampler.magFilter == gk::detail::ETextureFilter::Nearest, "base-color mipmap override changed other sampler settings"))
        return 1;
    auto* retainedImage = updated->textures.At(0);
    if (!Check(gk::DeleteImage(imageHandle) == 0 && retainedImage->rgba.Count() == 4, "material image did not outlive its public handle"))
        return 1;
    const auto* unchanged = updated;
    settings.baseColorFactor[0] = 0.5f;
    if (!Check(gk::SetModelMaterial(modelHandle, 0, settings) != 0 && gk::detail::FindModel(modelHandle) == unchanged, "stale base color image handle changed the registered model"))
        return 1;
    settings.baseColorFactor[0] = 1.01f;
    if (!Check(gk::SetModelMaterial(modelHandle, 0, settings) != 0 && gk::detail::FindModel(modelHandle) == unchanged, "invalid factor changed the registered model"))
        return 1;
    settings.baseColorFactor[0] = 0.5f;
    settings.baseColorImage = {};
    settings.alphaMode = gk::EModelAlphaMode::Mask;
    if (!Check(gk::SetModelMaterial(modelHandle, 0, settings) == 0, "clearing the base image failed"))
        return 1;
    auto* cleared = gk::detail::FindModel(modelHandle);
    if (!Check(cleared && cleared->materials.At(0).baseColorTextureIndex == -1 && cleared->materials.At(0).alphaMask && !cleared->materials.At(0).alphaBlend && cleared->textures.At(0) == legacyImage && cleared->textures.At(1) == nullptr, "cleared image slot was not released while preserving the MR reference"))
        return 1;
    settings.baseColorImage = legacyImageHandle;
    settings.alphaMode = gk::EModelAlphaMode::Opaque;
    if (!Check(gk::SetModelMaterial(modelHandle, 0, settings) == 0, "existing base color image could not be reused"))
        return 1;
    auto* reused = gk::detail::FindModel(modelHandle);
    if (!Check(reused && reused->materials.At(0).baseColorTextureIndex == 0 && reused->textures.Count() == 2, "existing texture slot was not reused"))
        return 1;
    settings.baseColorImage = {};
    if (!Check(gk::SetModelMaterial(modelHandle, 0, settings) == 0 && gk::DeleteImage(legacyImageHandle) == 0, "shared MR image could not be detached from the base role"))
        return 1;
    auto* detached = gk::detail::FindModel(modelHandle);
    if (!Check(detached && detached->materials.At(0).baseColorTextureIndex == -1 && detached->materials.At(0).metallicRoughnessTextureIndex == 0 && detached->textures.At(0) == legacyImage, "detaching a shared image released the still-used MR slot"))
        return 1;
    if (!Check(gk::Retain(&detached->reference), "could not retain the prior material snapshot"))
        return 1;
    const gk::ImageHandle secondImageHandle = gk::LoadImage(secondImageUtf8.c_str());
    auto* secondImage = gk::detail::FindImage(secondImageHandle);
    if (!Check(secondImageHandle.IsValid() && secondImage && gk::Retain(&secondImage->reference) && detached->textures.Count() > 1, "could not prepare a previously stored replacement image"))
        return 1;
    detached->textures.At(1) = secondImage;
    detached->materials.At(0).baseColorTextureIndex = 0;
    detached->materials.At(0).metallicRoughnessTextureIndex = -1;
    settings.baseColorImage = secondImageHandle;
    if (!Check(gk::SetModelMaterial(modelHandle, 0, settings) == 0, "existing image slot could not replace the old base slot"))
        return 1;
    auto* replacedExisting = gk::detail::FindModel(modelHandle);
    if (!Check(replacedExisting && replacedExisting->materials.At(0).baseColorTextureIndex == 1 && replacedExisting->textures.At(0) == nullptr && replacedExisting->textures.At(1) == secondImage && detached->textures.At(0) == legacyImage, "unreferenced old base image slot was retained after selecting an existing slot"))
        return 1;
    if (!Check(gk::DeleteImage(secondImageHandle) == 0, "replacement image handle could not be deleted"))
        return 1;
    const gk::ImageHandle mipDisabledImageHandle = gk::LoadImage(secondImageUtf8.c_str());
    settings.baseColorImage = mipDisabledImageHandle;
    settings.generateMipmaps = false;
    if (!Check(mipDisabledImageHandle.IsValid() && gk::SetModelMaterial(modelHandle, 0, settings) == 0, "disabling base-color mipmaps failed"))
        return 1;
    auto* mipDisabled = gk::detail::FindModel(modelHandle);
    if (!Check(mipDisabled && mipDisabled->materials.At(0).baseColorSampler.minFilter == gk::detail::ETextureFilter::Linear && mipDisabled->materials.At(0).baseColorSampler.mipFilter == gk::detail::ETextureMipFilter::None && mipDisabled->materials.At(0).baseColorSampler.magFilter == gk::detail::ETextureFilter::Nearest && mipDisabled->materials.At(0).baseColorSampler.addressU == gk::detail::ETextureAddressMode::Repeat && mipDisabled->materials.At(0).baseColorSampler.addressV == gk::detail::ETextureAddressMode::MirroredRepeat, "disabling mipmaps changed minification, magnification, or wrap policy"))
        return 1;
    if (!Check(gk::DeleteImage(mipDisabledImageHandle) == 0, "mipmap test image handle could not be deleted"))
        return 1;
    gk::Release(&detached->reference);
    gk::Release(&oldModel->reference);
    if (gk::DeleteModel(instanceHandle) != 0 || gk::DeleteModel(modelHandle) != 0)
        return 1;
    return 0;
}
