#include "../src/resources/Resources.h"

#include <filesystem>
#include <fstream>
#include <stdio.h>
#include <string>

namespace
{

/**
 * 期待値との差を記録し、このfixtureの検査を失敗させる。
 */
bool Fail(gk::String& failure, const char* message)
{
    failure.Assign(message);
    return false;
}

/**
 * 浮動小数のUV値をGLB読み込み誤差内で比較する。
 */
bool Near(float actual, float expected)
{
    return actual >= expected - 0.0001f && actual <= expected + 0.0001f;
}

/**
 * fixtureの実在、GLB magic、version、宣言file sizeを読み込み前に確認する。
 */
bool CheckFixtureFile(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // 検査対象GLBのpath。
    const std::filesystem::path path = directory / filename;
    // fixture本体をbinary modeで開くstream。
    std::ifstream file(path, std::ios::binary);
    // GLB headerを格納する12 byte。
    unsigned char header[12]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || header[0] != 'g' || header[1] != 'l' || header[2] != 'T' || header[3] != 'F')
    {
        failure.Assign("GLB fixture is missing or has an invalid signature: ");
        failure.Append(filename);
        return false;
    }
    // headerに記録されたGLB version。
    const uint32_t version = static_cast<uint32_t>(header[4]) | (static_cast<uint32_t>(header[5]) << 8) | (static_cast<uint32_t>(header[6]) << 16) | (static_cast<uint32_t>(header[7]) << 24);
    // headerに記録された全file byte数。
    const uint32_t declaredSize = static_cast<uint32_t>(header[8]) | (static_cast<uint32_t>(header[9]) << 8) | (static_cast<uint32_t>(header[10]) << 16) | (static_cast<uint32_t>(header[11]) << 24);
    // filesystemから取得した実file byte数。
    std::error_code fileSizeError;
    const uintmax_t actualSize = std::filesystem::file_size(path, fileSizeError);
    if (version != 2 || fileSizeError || actualSize != declaredSize)
    {
        failure.Assign("GLB fixture has an invalid version or declared size: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 正常GLBの頂点、material、embedded imageが指定されたUV setを使うか調べる。
 */
bool CheckLoadedFixture(const std::filesystem::path& directory, const char* filename, const float expectedUv[4][2], gk::String& failure)
{
    // モデル読み込み用のUTF-8パス。
    const std::string path = (directory / filename).u8string();
    // parse failureの理由を保持する文字列。
    gk::String error;
    // handle tableへ登録された読み込み結果。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        failure.Assign("valid GLB texture-coordinate fixture was rejected: ");
        failure.Append(filename);
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }

    // 頂点と材質を調べる借用ポインター。
    gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    // 全主要payloadが揃ったfixtureかを表す検査状態。
    bool valid = model && model->vertices.Count() == 4 && model->indices.Count() == 6 && model->primitives.Count() == 1 && model->materials.Count() == 1 && model->textures.Count() == 1;
    if (!valid)
    {
        failure.Assign("GLB texture-coordinate fixture has incomplete mesh or material resources: ");
        failure.Append(filename);
    }
    if (valid)
    {
        // quadを構成する期待index順。
        const uint32_t expectedIndices[6] = { 0, 1, 2, 0, 2, 3 };
        for (uint32_t index = 0; index < 6; ++index)
        {
            // 現在位置のindex値。
            if (model->indices.At(index) != expectedIndices[index])
            {
                failure.Assign("GLB fixture index order changed: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid)
    {
        // glTF nodeに適用するidentity変換後の頂点位置。
        const float expectedPosition[4][3] = { { -0.9f, -0.65f, 0.0f }, { 0.9f, -0.65f, 0.0f }, { 0.9f, 0.65f, 0.0f }, { -0.9f, 0.65f, 0.0f } };
        for (uint32_t index = 0; index < 4; ++index)
        {
            // selected UVと元meshの位置・法線を確認する頂点。
            const gk::detail::ModelVertex& vertex = model->vertices.At(index);
            if (!Near(vertex.uv[0], expectedUv[index][0]) || !Near(vertex.uv[1], expectedUv[index][1]) || !Near(vertex.position[0], expectedPosition[index][0]) || !Near(vertex.position[1], expectedPosition[index][1]) || !Near(vertex.position[2], expectedPosition[index][2]) || !Near(vertex.normal[0], 0.0f) || !Near(vertex.normal[1], 0.0f) || !Near(vertex.normal[2], -1.0f))
            {
                failure.Assign("GLB material-selected texture coordinates are incorrect: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid)
    {
        // 唯一のprimitiveに属するindex範囲と材質slot。
        const gk::detail::ModelPrimitive& primitive = model->primitives.At(0);
        // PBR factorとtexture slotが設定されたmaterial。
        const gk::detail::ModelMaterial& material = model->materials.At(0);
        // 埋込PNGから読み込まれた参照texture。
        gk::detail::ImageResource* image = model->textures.At(0);
        if (primitive.firstIndex != 0 || primitive.indexCount != 6 || primitive.materialIndex != 0 || material.baseColorTextureIndex != 0 || material.baseColorFactor[0] != 1.0f || material.baseColorFactor[1] != 1.0f || material.baseColorFactor[2] != 1.0f || material.baseColorFactor[3] != 1.0f || material.metallicFactor != 0.0f || material.roughnessFactor != 1.0f || !image || image->width != 64 || image->height != 32 || image->rgba.Count() != 64u * 32u * 4u)
        {
            failure.Assign("GLB material or embedded PNG payload is incorrect: ");
            failure.Append(filename);
            valid = false;
        }
    }
    // fixtureを解放して後続ケースへの影響を防ぐ結果。
    const bool deleted = gk::detail::DeleteModel(handle, error);
    if (!deleted && valid)
    {
        failure.Assign("GLB fixture model could not be deleted: ");
        failure.Append(filename);
        valid = false;
    }
    return valid;
}

/**
 * 選択したUV setが存在しない、壊れているGLBを診断付きで拒否する。
 */
bool CheckRejectedFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // GLBローダーに渡すUTF-8 fixture path。
    const std::string path = (directory / filename).u8string();
    // 形式拒否理由を受け取る文字列。
    gk::String error;
    // 正常handleが誤って発行されないか調べる結果。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (handle.IsValid())
    {
        gk::detail::DeleteModel(handle, error);
        failure.Assign("invalid selected GLB texture coordinates were accepted: ");
        failure.Append(filename);
        return false;
    }
    if (error.Empty())
    {
        failure.Assign("rejected GLB texture coordinates had no diagnostic: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: glb_texture_coordinate_tests <fixture-directory>\n");
        return 2;
    }
    // fixture generatorが出力したGLB directory。
    const std::filesystem::path directory(argv[1]);
    // 各検査から返る診断。
    gk::String failure;
    // 読み込みテスト前に全fixtureが生成済みでGLB2 headerを持つことを確認する一覧。
    const char* fixtureNames[] = { "default.glb", "uv0.glb", "uv1.glb", "uv2.glb", "missing-selected.glb", "negative-selected.glb", "wrong-selected-count.glb", "normalized-uv1.glb", "normalized-byte-uv1.glb", "unnormalized-uv1.glb", "signed-uv1.glb", "float-normalized-uv1.glb", "transformed-selected.glb" };
    for (const char* filename : fixtureNames)
    {
        if (!CheckFixtureFile(directory, filename, failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
    }
    // texCoord省略時とUV0明示時に使う頂点UV。
    const float uv0[4][2] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
    // 水平反転したUV1/UV2の期待値。
    const float uv1[4][2] = { { 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f } };
    if (!CheckLoadedFixture(directory, "default.glb", uv0, failure) || !CheckLoadedFixture(directory, "uv0.glb", uv0, failure) || !CheckLoadedFixture(directory, "uv1.glb", uv1, failure) || !CheckLoadedFixture(directory, "uv2.glb", uv1, failure) || !CheckLoadedFixture(directory, "normalized-uv1.glb", uv1, failure) || !CheckLoadedFixture(directory, "normalized-byte-uv1.glb", uv1, failure) || !CheckRejectedFixture(directory, "missing-selected.glb", failure) || !CheckRejectedFixture(directory, "negative-selected.glb", failure) || !CheckRejectedFixture(directory, "wrong-selected-count.glb", failure) || !CheckRejectedFixture(directory, "unnormalized-uv1.glb", failure) || !CheckRejectedFixture(directory, "signed-uv1.glb", failure) || !CheckRejectedFixture(directory, "float-normalized-uv1.glb", failure) || !CheckRejectedFixture(directory, "transformed-selected.glb", failure))
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }
    return 0;
}
