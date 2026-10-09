// SPDX-License-Identifier: NOASSERTION
#include "model/ModelLoader.h"
#include "model/animation/GlbAnimation.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/ModelPose.h"
#include "resources/Resources.h"

#include <filesystem>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

/**
 * 値がfixtureの許容誤差内で一致するか調べる。
 */
bool Near(float actual, float expected)
{
    return fabsf(actual - expected) <= 0.0002f;
}

/**
 * 失敗理由を保存する。
 */
bool Fail(gk::String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * skeleton内で指定node名のindexを探す。
 */
int32_t FindBone(const gk::model::AModelAnimationSource& source, const char* name)
{
    for (uint32_t bone = 0; bone < source.Skeleton().parents.Count(); ++bone)
    {
        const char* candidate = source.BoneName(bone);
        if (candidate && strcmp(candidate, name) == 0)
            return static_cast<int32_t>(bone);
    }
    return -1;
}

/**
 * GLB fileをtest所有byte列へ読み込む。
 */
bool ReadBytes(const std::filesystem::path& path, uint8_t*& bytes, uint32_t& size, gk::String& error)
{
    FILE* file = fopen(path.u8string().c_str(), "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0)
    {
        if (file)
            fclose(file);
        return Fail(error, "could not open GLB animation fixture");
    }
    const long length = ftell(file);
    if (length <= 0 || static_cast<unsigned long>(length) > UINT32_MAX || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return Fail(error, "GLB animation fixture has an invalid size");
    }
    bytes = new uint8_t[static_cast<size_t>(length)];
    if (fread(bytes, 1, static_cast<size_t>(length), file) != static_cast<size_t>(length))
    {
        delete[] bytes;
        bytes = nullptr;
        fclose(file);
        return Fail(error, "could not read GLB animation fixture");
    }
    fclose(file);
    size = static_cast<uint32_t>(length);
    return true;
}

/**
 * 同じGLB assetからclip姿勢を評価し、node TRSとmorphを確認する。
 */
bool CheckMixedInterpolation(const std::filesystem::path& directory, gk::String& error)
{
    const std::string path = (directory / "glb-animation-full.glb").u8string();
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        fprintf(stderr, "GLB animation fixture failed to load: %s\r\n", error.CStr());
        return false;
    }
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    if (!model || !model->animation || !model->animation->source)
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "animated GLB did not retain its animation source");
    }
    const gk::model::AModelAnimationSource& source = *model->animation->source;
    const gk::model::animation::FModelSkeleton& skeleton = source.Skeleton();
    if (source.ClipCount() != 1 || !source.ClipName(0) || source.ClipDuration(0) != 2.0 || skeleton.parents.Count() != 4 || skeleton.restLocalTransforms.Count() != 4 || skeleton.restMorphWeights.Count() != 1 || !Near(skeleton.restMorphWeights.At(0), 0.0f) || !source.MorphName(0) || strcmp(source.MorphName(0), "mesh_node/lift") != 0)
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "GLB animation source lost hierarchy, duration, or rest morph data");
    }
    const int32_t rootBone = FindBone(source, "joint_root");
    const int32_t childBone = FindBone(source, "joint_child");
    if (rootBone < 0 || childBone < 0 || skeleton.parents.At(static_cast<uint32_t>(childBone)) != rootBone)
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "GLB skeleton did not preserve the named joint hierarchy");
    }
    gk::model::animation::FModelPose pose;
    if (!source.Sample(0, 1.0, pose, error))
    {
        gk::detail::DeleteModel(handle, error);
        return false;
    }
    const auto& rootTransform = pose.localTransforms.At(static_cast<uint32_t>(rootBone));
    const auto& childTransform = pose.localTransforms.At(static_cast<uint32_t>(childBone));
    if (!Near(rootTransform.position[0], 1.0f) || !Near(childTransform.scale[0], 2.0f) || !Near(childTransform.rotation[2], 0.0f) || !Near(childTransform.rotation[3], 1.0f) || pose.morphWeights.Count() != 1 || !Near(pose.morphWeights.At(0), 0.5f))
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "GLB LINEAR, STEP, or CUBICSPLINE sampling returned an unexpected pose");
    }
    if (!source.Sample(0, 2.0, pose, error) || !Near(pose.localTransforms.At(static_cast<uint32_t>(childBone)).rotation[2], 1.0f) || !Near(pose.localTransforms.At(static_cast<uint32_t>(childBone)).rotation[3], 0.0f))
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "GLB STEP sampling did not select the final key");
    }
    if (!source.Sample(0, 1.0, pose, error))
    {
        gk::detail::DeleteModel(handle, error);
        return false;
    }
    gk::detail::ModelResource deformed{};
    if (!deformed.vertices.AppendRange(model->vertices.Data(), model->vertices.Count()) || !deformed.indices.AppendRange(model->indices.Data(), model->indices.Count()) || !deformed.primitives.AppendRange(model->primitives.Data(), model->primitives.Count()) || !deformed.materials.AppendRange(model->materials.Data(), model->materials.Count()) || !source.Deform(pose, deformed, error))
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "GLB skin and morph deformation failed");
    }
    uint32_t sourceZeroCopies = 0;
    for (uint32_t vertex = 0; vertex < deformed.vertices.Count(); ++vertex)
    {
        const auto& value = deformed.vertices.At(vertex);
        const float length = sqrtf(value.normal[0] * value.normal[0] + value.normal[1] * value.normal[1] + value.normal[2] * value.normal[2]);
        if (!Near(length, 1.0f))
        {
            fprintf(stderr, "generated normal vertex %u = %.6f %.6f %.6f\r\n", vertex, value.normal[0], value.normal[1], value.normal[2]);
            gk::detail::DeleteModel(handle, error);
            return Fail(error, "GLB deformation lost a generated vertex normal");
        }
    }
    for (uint32_t vertex = 0; vertex < deformed.vertices.Count(); ++vertex)
        if (deformed.vertices.At(vertex).sourceIndex == 0)
        {
            ++sourceZeroCopies;
            if (!Near(deformed.vertices.At(vertex).position[0], 1.0f) || !Near(deformed.vertices.At(vertex).position[1], 0.375f))
            {
                fprintf(stderr, "split source %u copy %u got %.6f %.6f expected 1.0 0.375\r\n", vertex, sourceZeroCopies, deformed.vertices.At(vertex).position[0], deformed.vertices.At(vertex).position[1]);
                gk::detail::DeleteModel(handle, error);
                return Fail(error, "GLB deformation lost a split vertex source mapping");
            }
        }
    if (deformed.vertices.Count() != model->vertices.Count() || sourceZeroCopies < 2 || !Near(deformed.vertices.At(0).position[0], 1.0f) || !Near(deformed.vertices.At(0).position[1], 0.375f))
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "GLB skin/morph deformation did not produce the independent reference position");
    }
    gk::detail::DeleteModel(handle, error);
    return true;
}

/**
 * source bytesを呼び出し元が変更・解放した後もclipを評価できるか調べる。
 */
bool CheckOwnedBytes(const std::filesystem::path& directory, gk::String& error)
{
    uint8_t* bytes = nullptr;
    uint32_t size = 0;
    if (!ReadBytes(directory / "glb-animation-full.glb", bytes, size, error))
        return false;
    gk::model::FModelAnimationAsset* asset = gk::model::LoadGlbAnimation(bytes, size, error);
    memset(bytes, 0, size);
    delete[] bytes;
    if (!asset || !asset->source)
    {
        fprintf(stderr, "standalone animation load error: %s\r\n", error.CStr());
        if (asset)
            gk::Release(&asset->reference);
        return Fail(error, "standalone GLB animation loading failed");
    }
    gk::model::animation::FModelPose pose;
    const int32_t rootBone = FindBone(*asset->source, "joint_root");
    const bool sampled = rootBone >= 0 && asset->source->Sample(0, 1.0, pose, error) && pose.localTransforms.Count() == 4 && Near(pose.localTransforms.At(static_cast<uint32_t>(rootBone)).position[0], 1.0f);
    gk::Release(&asset->reference);
    return sampled || Fail(error, "standalone animation source borrowed released GLB bytes");
}

/**
 * skin属性付きmeshをskin有無のnodeで共有したとき両instanceを別々に変形する。
 */
bool CheckMixedSkinInstances(const std::filesystem::path& directory, gk::String& error)
{
    const std::string path = (directory / "glb-animation-mixed-skin-instances.glb").u8string();
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
        return Fail(error, "shared skinned and unskinned mesh fixture failed to load");
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    if (!model || !model->animation || !model->animation->source || model->vertices.Count() != 12)
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "shared mesh instances lost their corner ranges");
    }
    const gk::model::AModelAnimationSource& source = *model->animation->source;
    gk::model::animation::FModelPose pose;
    gk::detail::ModelResource deformed{};
    const bool valid = source.Sample(0, 1.0, pose, error) && deformed.vertices.AppendRange(model->vertices.Data(), model->vertices.Count()) && deformed.indices.AppendRange(model->indices.Data(), model->indices.Count()) && deformed.primitives.AppendRange(model->primitives.Data(), model->primitives.Count()) && deformed.materials.AppendRange(model->materials.Data(), model->materials.Count()) && source.Deform(pose, deformed, error) && Near(deformed.vertices.At(6).position[0], 3.0f) && Near(deformed.vertices.At(7).position[0], 4.0f);
    gk::detail::DeleteModel(handle, error);
    return valid || Fail(error, "unskinned mesh instance did not use its node transform");
}

/**
 * 壊れた時間・output accessorを持つclipを原子的に拒否する。
 */
bool CheckMalformedFixtures(const std::filesystem::path& directory, gk::String& error)
{
    const char* names[] = { "glb-animation-bad-time.glb", "glb-animation-bad-output.glb" };
    for (const char* name : names)
    {
        uint8_t* bytes = nullptr;
        uint32_t size = 0;
        if (!ReadBytes(directory / name, bytes, size, error))
            return false;
        gk::model::FModelAnimationAsset* asset = gk::model::LoadGlbAnimation(bytes, size, error);
        delete[] bytes;
        if (asset)
        {
            gk::Release(&asset->reference);
            return Fail(error, "malformed GLB animation fixture unexpectedly loaded");
        }
        if (error.Empty())
            return Fail(error, "malformed GLB animation was rejected without a diagnostic");
    }
    return true;
}

/**
 * NORMALなしのPOSITION morphがflat normalを更新し、TANGENT deltaを無視する。
 */
bool CheckGeneratedNormalMorph(const std::filesystem::path& directory, gk::String& error)
{
    const std::string path = (directory / "glb-animation-generated-normal-morph.glb").u8string();
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        fprintf(stderr, "generated-normal morph fixture load error: %s\r\n", error.CStr());
        return Fail(error, "generated-normal morph fixture failed to load");
    }
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    if (!model || !model->animation || !model->animation->source)
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "generated-normal morph source was not retained");
    }
    const gk::model::AModelAnimationSource& source = *model->animation->source;
    gk::model::animation::FModelPose pose;
    const auto checkFrame = [&](const float expectedNormal[3], bool checkTangent) -> bool
    {
        gk::detail::ModelResource deformed{};
        if (!deformed.vertices.AppendRange(model->vertices.Data(), model->vertices.Count()) || !deformed.indices.AppendRange(model->indices.Data(), model->indices.Count()) || !deformed.primitives.AppendRange(model->primitives.Data(), model->primitives.Count()) || !deformed.materials.AppendRange(model->materials.Data(), model->materials.Count()) || !source.Deform(pose, deformed, error))
            return false;
        const gk::detail::ModelVertex& vertex = deformed.vertices.At(2);
        bool matches = Near(vertex.normal[0], expectedNormal[0]) && Near(vertex.normal[1], expectedNormal[1]) && Near(vertex.normal[2], expectedNormal[2]);
        if (matches && checkTangent)
            matches = Near(vertex.tangent[0], 1.0f) && Near(vertex.tangent[1], 0.0f) && Near(vertex.tangent[2], 0.0f) && Near(vertex.tangent[3], 1.0f);
        return matches;
    };
    bool valid = source.Sample(0, 1.0, pose, error) && pose.morphWeights.Count() == 2 && Near(pose.morphWeights.At(0), 1.0f) && Near(pose.morphWeights.At(1), 0.0f);
    const float inverseRootTwo = 0.70710678f;
    const float fullTargetNormal[3] = { 0.0f, -inverseRootTwo, inverseRootTwo };
    valid = valid && checkFrame(fullTargetNormal, true);
    valid = valid && source.Sample(0, 0.5, pose, error);
    const float halfTargetNormal[3] = { 0.0f, -0.38268343f, 0.92387953f };
    valid = valid && checkFrame(halfTargetNormal, false);
    pose.morphWeights.At(0) = 0.25f;
    pose.morphWeights.At(1) = 0.25f;
    const float blendedX = -0.25f * inverseRootTwo;
    const float blendedY = -0.25f * inverseRootTwo;
    const float blendedZ = 0.5f + 0.5f * inverseRootTwo;
    const float blendedLength = sqrtf(blendedX * blendedX + blendedY * blendedY + blendedZ * blendedZ);
    const float twoTargetNormal[3] = { blendedX / blendedLength, blendedY / blendedLength, blendedZ / blendedLength };
    valid = valid && checkFrame(twoTargetNormal, false);
    gk::detail::DeleteModel(handle, error);
    if (!valid)
        return Fail(error, "morphed flat normal or generated tangent differs from the analytic frame");
    return true;
}

/**
 * 明示法線を保ったまま、morph位置と法線から不足接線を生成する。
 */
bool CheckGeneratedTangentMorph(const std::filesystem::path& directory, gk::String& error)
{
    const std::string path = (directory / "glb-animation-generated-tangent-morph.glb").u8string();
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
        return Fail(error, "generated-tangent morph fixture failed to load");
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    if (!model || !model->animation || !model->animation->source)
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "generated-tangent morph source was not retained");
    }
    const gk::model::AModelAnimationSource& source = *model->animation->source;
    gk::model::animation::FModelPose pose;
    const auto checkFrame = [&](const float expectedNormal[3], const float expectedTangent[3]) -> bool
    {
        gk::detail::ModelResource deformed{};
        if (!deformed.vertices.AppendRange(model->vertices.Data(), model->vertices.Count()) || !deformed.indices.AppendRange(model->indices.Data(), model->indices.Count()) || !deformed.primitives.AppendRange(model->primitives.Data(), model->primitives.Count()) || !deformed.materials.AppendRange(model->materials.Data(), model->materials.Count()) || !source.Deform(pose, deformed, error))
            return false;
        const gk::detail::ModelVertex& vertex = deformed.vertices.At(2);
        return Near(vertex.normal[0], expectedNormal[0]) && Near(vertex.normal[1], expectedNormal[1]) && Near(vertex.normal[2], expectedNormal[2]) && Near(vertex.tangent[0], expectedTangent[0]) && Near(vertex.tangent[1], expectedTangent[1]) && Near(vertex.tangent[2], expectedTangent[2]) && Near(vertex.tangent[3], 1.0f);
    };
    const float baseNormal[3] = { 0.0f, 0.0f, 1.0f };
    const float baseTangent[3] = { 1.0f, 0.0f, 0.0f };
    bool valid = source.Sample(0, 0.0, pose, error) && checkFrame(baseNormal, baseTangent);
    const float targetNormal[3] = { 1.0f, 0.0f, 0.0f };
    const float targetTangent[3] = { 0.0f, 1.0f, 0.0f };
    valid = valid && source.Sample(0, 1.0, pose, error) && checkFrame(targetNormal, targetTangent);
    const float halfNormal[3] = { 0.70710678f, 0.0f, 0.70710678f };
    const float halfTangent[3] = { 0.40824829f, 0.81649658f, -0.40824829f };
    valid = valid && source.Sample(0, 0.5, pose, error) && checkFrame(halfNormal, halfTangent);
    gk::detail::DeleteModel(handle, error);
    return valid || Fail(error, "morphed generated tangent differs from its analytic frame");
}

/**
 * morph targetにない属性はそのtargetの差分なしとして扱う。
 */
bool CheckOptionalMorphAttributes(const std::filesystem::path& directory, gk::String& error)
{
    const std::string path = (directory / "glb-animation-optional-morph-attributes.glb").u8string();
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
        return Fail(error, "optional morph attribute fixture failed to load");
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    if (!model || !model->animation || !model->animation->source)
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(error, "optional morph attribute source was not retained");
    }
    const gk::model::AModelAnimationSource& source = *model->animation->source;
    gk::model::animation::FModelPose pose;
    const auto checkNormal = [&](float y, float z) -> bool
    {
        gk::detail::ModelResource deformed{};
        if (!deformed.vertices.AppendRange(model->vertices.Data(), model->vertices.Count()) || !deformed.indices.AppendRange(model->indices.Data(), model->indices.Count()) || !deformed.primitives.AppendRange(model->primitives.Data(), model->primitives.Count()) || !deformed.materials.AppendRange(model->materials.Data(), model->materials.Count()) || !source.Deform(pose, deformed, error))
            return false;
        return Near(deformed.vertices.At(0).normal[1], y) && Near(deformed.vertices.At(0).normal[2], z);
    };
    bool valid = source.Sample(0, 1.0, pose, error) && pose.morphWeights.Count() == 2;
    pose.morphWeights.At(0) = 1.0f;
    pose.morphWeights.At(1) = 0.0f;
    valid = valid && checkNormal(0.0f, 1.0f);
    pose.morphWeights.At(0) = 0.0f;
    pose.morphWeights.At(1) = 1.0f;
    valid = valid && checkNormal(0.70710678f, 0.70710678f);
    gk::detail::DeleteModel(handle, error);
    return valid || Fail(error, "omitted morph attribute did not contribute zero delta");
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: model_glb_animation_tests <fixture-dir>\r\n");
        return 2;
    }
    gk::String error;
    const std::filesystem::path directory = std::filesystem::u8path(argv[1]);
    if (!CheckGeneratedNormalMorph(directory, error) || !CheckGeneratedTangentMorph(directory, error) || !CheckOptionalMorphAttributes(directory, error) || !CheckMixedInterpolation(directory, error) || !CheckMixedSkinInstances(directory, error) || !CheckOwnedBytes(directory, error) || !CheckMalformedFixtures(directory, error))
    {
        fprintf(stderr, "%s\r\n", error.CStr());
        return 1;
    }
    return 0;
}
