// SPDX-License-Identifier: NOASSERTION
#include "model/animation/HumanoidBoneMap.h"
#include <gkcore/HumanoidBoneTypes.h>

#include <filesystem>
#include <chrono>
#include <fstream>
#include <stdio.h>
#include <string>
#include <vector>

namespace
{

/**
 * 骨名、書き換え可否、骨格数を固定して返すテストsource。
 */
class FHumanoidBoneMapTestSource final : public gk::model::AModelAnimationSource
{
  public:
    gk::model::animation::FModelSkeleton skeleton;
    std::vector<std::string> names;
    std::vector<bool> writable;

    gk::model::EModelAnimationFormat Format() const override
    {
        return gk::model::EModelAnimationFormat::Glb;
    }
    const gk::model::animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton;
    }
    const char* BoneName(uint32_t bone) const override
    {
        return bone < names.size() ? names[bone].c_str() : nullptr;
    }
    bool BoneWritable(uint32_t bone) const override
    {
        return bone < writable.size() && writable[bone];
    }
    const char* MorphName(uint32_t) const override
    {
        return nullptr;
    }
    uint32_t ClipCount() const override
    {
        return 0;
    }
    const char* ClipName(uint32_t) const override
    {
        return nullptr;
    }
    double ClipDuration(uint32_t) const override
    {
        return -1.0;
    }
    bool Sample(uint32_t, double, gk::model::animation::FModelPose&, gk::String& error) const override
    {
        error.Assign("test source has no clips");
        return false;
    }
    bool Deform(const gk::model::animation::FModelPose&, gk::detail::ModelResource&, gk::String& error) const override
    {
        error.Assign("test source has no geometry");
        return false;
    }
};

/**
 * 任意のUTF-8骨名と親配列をfixtureへ設定する。
 */
bool SetupSource(FHumanoidBoneMapTestSource& source, const std::vector<std::string>& names)
{
    source.names = names;
    source.writable.assign(names.size(), true);
    for (uint32_t i = 0; i < names.size(); ++i)
    {
        const int32_t parent = i == 0 ? -1 : static_cast<int32_t>(i - 1);
        if (!source.skeleton.parents.Append(parent))
            return false;
    }
    return true;
}

/**
 * 内容を一時ファイルへ書き、失敗した場合は空のパスを返す。
 */
std::filesystem::path WriteTempFile(const std::string& contents)
{
    static uint32_t sequence = 0;
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("forgedx_humanoid_map_" + std::to_string(timestamp) + "_" + std::to_string(++sequence) + ".txt");
    std::ofstream file(path, std::ios::binary);
    if (!file || !file.write(contents.data(), static_cast<std::streamsize>(contents.size())))
        return {};
    return path;
}

/**
 * 生成したテスト用一時ファイルを削除する。
 */
void RemoveTempFile(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
}

/**
 * BOM、CRLF、コメント、空行、名前両端の空白を受け入れて全置換する。
 */
bool TestUtf8RowsAndWholeReplacement()
{
    FHumanoidBoneMapTestSource source;
    if (!SetupSource(source, { "腰 中心", "左腕", "頭" }))
        return false;
    const std::string text = "\xEF\xBB\xBF# 人型役割\r\nHips\t  腰 中心 \t\r\n\r\nLeftUpperArm\t左腕\r\n";
    const std::filesystem::path path = WriteTempFile(text);
    gk::Array<uint16_t> output;
    gk::String error;
    const bool seeded = output.Append(99);
    const bool loaded = seeded && gk::model::LoadHumanoidBoneMap(path.u8string().c_str(), source, output, error);
    RemoveTempFile(path);
    using Bone = gk::EHumanoidBone;
    return loaded && output.Count() == 3 && output.At(0) == static_cast<uint16_t>(Bone::Hips) && output.At(1) == static_cast<uint16_t>(Bone::LeftUpperArm) && output.At(2) == 0 && error.Empty();
}

/**
 * 重複役割、重複骨、未知名、曖昧名、固定骨を拒否し出力を保つ。
 */
bool TestInvalidRowsAreAtomicFailures()
{
    const std::vector<std::string> names = { "腰", "同名", "同名", "固定" };
    const std::vector<std::string> cases = { "Hips\t腰\nHips\t頭\n", "Hips\t腰\nSpine\t腰\n", "Hips\t存在しない\n", "Hips\t同名\n", "Hips\t固定\n", "UnknownRole\t腰\n", "# only comment\n", " Hips\t腰\n" };
    for (uint32_t i = 0; i < cases.size(); ++i)
    {
        FHumanoidBoneMapTestSource source;
        if (!SetupSource(source, names))
            return false;
        source.writable[3] = false;
        const std::filesystem::path path = WriteTempFile(cases[i]);
        if (path.empty())
            return false;
        gk::Array<uint16_t> output;
        gk::String error;
        const uint16_t sentinel[] = { 41, 42 };
        const bool seeded = output.AppendRange(sentinel, 2);
        const bool loaded = seeded && gk::model::LoadHumanoidBoneMap(path.u8string().c_str(), source, output, error);
        RemoveTempFile(path);
        if (loaded || output.Count() != 2 || output.At(0) != 41 || output.At(1) != 42 || error.Empty())
        {
            fprintf(stderr, "invalid map case %u did not fail atomically\n", i);
            return false;
        }
    }
    return true;
}

/**
 * 上限超過、UTF-8不正、空骨格、無効pathを拒否する。
 */
bool TestInvalidInputs()
{
    FHumanoidBoneMapTestSource source;
    if (!SetupSource(source, { "腰" }))
        return false;
    std::string oversized(64u * 1024u + 1u, ' ');
    const std::filesystem::path oversizedPath = WriteTempFile(oversized);
    const std::filesystem::path invalidUtf8Path = WriteTempFile(std::string("Hips\t\xC0\xAF\n", 8));
    gk::Array<uint16_t> output;
    gk::String error;
    if (!output.Append(73))
        return false;
    const bool oversizedResult = gk::model::LoadHumanoidBoneMap(oversizedPath.u8string().c_str(), source, output, error);
    const bool oversizedAtomic = !oversizedResult && output.Count() == 1 && output.At(0) == 73 && !error.Empty();
    error.Clear();
    const bool utf8Result = gk::model::LoadHumanoidBoneMap(invalidUtf8Path.u8string().c_str(), source, output, error);
    const bool utf8Atomic = !utf8Result && output.Count() == 1 && output.At(0) == 73 && !error.Empty();
    RemoveTempFile(oversizedPath);
    RemoveTempFile(invalidUtf8Path);
    if (!oversizedAtomic || !utf8Atomic)
        return false;
    FHumanoidBoneMapTestSource emptySource;
    const std::filesystem::path validPath = WriteTempFile("Hips\t腰\n");
    error.Clear();
    const bool emptyResult = gk::model::LoadHumanoidBoneMap(validPath.u8string().c_str(), emptySource, output, error);
    RemoveTempFile(validPath);
    error.Clear();
    const bool nullPathResult = gk::model::LoadHumanoidBoneMap(nullptr, source, output, error);
    return !emptyResult && !nullPathResult && output.Count() == 1 && output.At(0) == 73;
}

}

int main()
{
    if (!TestUtf8RowsAndWholeReplacement() || !TestInvalidRowsAreAtomicFailures() || !TestInvalidInputs())
        return 1;
    return 0;
}
