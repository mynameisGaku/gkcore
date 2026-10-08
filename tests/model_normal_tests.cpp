#include "../src/resources/Resources.h"
#include "../src/render/ModelDrawPlan.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <math.h>
#include <stdio.h>
#include <string>
#include <string.h>

namespace
{

/**
 * GLB読み込み・材質描画計画の検査失敗を記録する。
 */
bool Fail(gk::String& failure, const char* message)
{
    failure.Assign(message);
    return false;
}

/**
 * float値がGLB読み込み誤差の範囲で期待値に近いか調べる。
 */
bool Near(float actual, float expected)
{
    return fabsf(actual - expected) <= 0.0002f;
}

/**
 * normal pixel、scale、接線向きからreference用の単位法線を計算する。
 */
void ReferenceNormal(const uint8_t pixel[4], float scale, float tangentSign, float output[3])
{
    // tangent planeへ展開したnormal pixelのXYZ。
    const float x = (static_cast<float>(pixel[0]) / 255.0f * 2.0f - 1.0f) * scale;
    const float y = (static_cast<float>(pixel[1]) / 255.0f * 2.0f - 1.0f) * scale;
    const float z = static_cast<float>(pixel[2]) / 255.0f * 2.0f - 1.0f;
    // N=(0,0,-1)、T=(1,0,0)から得るbitangentを反映したworld向き。
    output[0] = x;
    output[1] = -y * tangentSign;
    output[2] = -z;
    // GPUへ渡す前の単位長。
    const float length = sqrtf(output[0] * output[0] + output[1] * output[1] + output[2] * output[2]);
    output[0] /= length;
    output[1] /= length;
    output[2] /= length;
}

/**
 * fixture生成時に期待する材質画像・変換設定。
 */
struct FNormalExpectation
{
    // 入力GLBの名前。
    const char* filename;
    // modelが保持するdecoded画像数。
    uint32_t textureCount;
    // normal画像slot。未使用は-1。
    int32_t normalTextureIndex;
    // normal画像へ掛けるscale。
    float normalScale;
    // normal画像がUV1を選ぶか。
    bool normalUsesUv1;
    // 接線の符号付きhandedness。
    float tangentSign;
    // node scaleでx軸反転を適用するか。
    bool mirroredNode;
    // normal画像があるか。
    bool hasNormalTexture;
    // 2つの三角形で接線符号が異なるか。
    bool mixedTangentSigns = false;
};

/**
 * fixtureが存在し、GLB 2 headerと宣言file sizeが正しいことを先に確かめる。
 */
bool CheckFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // 読み込み対象fixtureのpath。
    const std::filesystem::path path = directory / filename;
    // GLB headerをbinary modeで読むstream。
    std::ifstream file(path, std::ios::binary);
    // magic、version、総byte数を含むheader。
    unsigned char header[12]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || header[0] != 'g' || header[1] != 'l' || header[2] != 'T' || header[3] != 'F')
    {
        failure.Assign("normal fixture is missing or has an invalid GLB header: ");
        failure.Append(filename);
        return false;
    }
    // little-endian headerから読むGLB versionと宣言byte数。
    const uint32_t version = static_cast<uint32_t>(header[4]) | (static_cast<uint32_t>(header[5]) << 8) | (static_cast<uint32_t>(header[6]) << 16) | (static_cast<uint32_t>(header[7]) << 24);
    const uint32_t declaredSize = static_cast<uint32_t>(header[8]) | (static_cast<uint32_t>(header[9]) << 8) | (static_cast<uint32_t>(header[10]) << 16) | (static_cast<uint32_t>(header[11]) << 24);
    // 実file sizeを取得する際のerror code。
    std::error_code fileSizeError;
    const uintmax_t actualSize = std::filesystem::file_size(path, fileSizeError);
    if (version != 2 || fileSizeError || actualSize != declaredSize)
    {
        failure.Assign("normal fixture has an invalid GLB version or size: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 1x1 textureのdecoded RGBA pixelが期待値と一致するか調べる。
 */
bool CheckPixel(const gk::detail::ImageResource& image, const uint8_t expected[4])
{
    return image.width == 1 && image.height == 1 && image.rgba.Count() == 4 && image.rgba.At(0) == expected[0] && image.rgba.At(1) == expected[1] && image.rgba.At(2) == expected[2] && image.rgba.At(3) == expected[3];
}

/**
 * normal画像の全四象限から内側pixelを読み、PNG内容を確認する。
 */
bool CheckPatternImage(const gk::detail::ImageResource& image)
{
    // 各象限で期待するRGBA値。
    const uint8_t expected[4][4] = { { 192, 128, 255, 255 }, { 64, 128, 255, 255 }, { 128, 192, 255, 255 }, { 128, 64, 255, 255 } };
    if (image.width != 64 || image.height != 32 || image.rgba.Count() != 64u * 32u * 4u)
        return false;
    // 各象限の内側pixelを確認するloop。
    for (uint32_t quadrant = 0; quadrant < 4; ++quadrant)
    {
        // 対象象限から選ぶpixel位置。
        const uint32_t x = (quadrant % 2) * 32 + 8;
        const uint32_t y = (quadrant / 2) * 16 + 8;
        // 左上原点RGBA配列内のpixel位置。
        const uint32_t offset = (y * image.width + x) * 4;
        for (uint32_t component = 0; component < 4; ++component)
        {
            if (image.rgba.At(offset + component) != expected[quadrant][component])
                return false;
        }
    }
    return true;
}

/**
 * 有効GLBのnormal設定、頂点基底、UV、画像、描画計画を確認する。
 */
bool CheckLoadedFixture(const std::filesystem::path& directory, const FNormalExpectation& expected, gk::String& failure)
{
    // model loaderへ渡すUTF-8 path。
    const std::string path = (directory / expected.filename).u8string();
    // loaderや描画計画から返る診断。
    gk::String error;
    // registryへ登録されたfixture model handle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        failure.Assign("valid normal fixture was rejected: ");
        failure.Append(expected.filename);
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }
    // fixture resourceを借用するpointer。
    gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    // 4 quad fixtureは各primitiveが共有頂点を保持するため64頂点になる。
    const bool patternReference = strcmp(expected.filename, "uv1-reference.glb") == 0;
    const bool mixedSigns = strcmp(expected.filename, "mixed-sign-triangles.glb") == 0;
    const uint32_t expectedPrimitiveCount = patternReference ? 4u : 1u;
    const uint32_t expectedVertexCount = mixedSigns ? 6u : (patternReference ? 64u : expectedPrimitiveCount * 4u);
    bool valid = model && model->vertices.Count() == expectedVertexCount && model->primitives.Count() == expectedPrimitiveCount && model->materials.Count() == 1 && model->textures.Count() == expected.textureCount;
    if (!valid)
    {
        failure.Assign("normal fixture has unexpected geometry or resource counts: ");
        failure.Append(expected.filename);
    }
    if (valid)
    {
        // loaded材質のnormal texture slotとscale。
        const gk::detail::ModelMaterial& material = model->materials.At(0);
        // 3役割の共有画像と数値参照は同じ金属度・粗さになる設定を使う。
        const bool sharedImage = strcmp(expected.filename, "shared-image.glb") == 0 || strcmp(expected.filename, "shared-image-aliases.glb") == 0 || strcmp(expected.filename, "distinct-image-records.glb") == 0 || strcmp(expected.filename, "mixed-role-image-alias.glb") == 0;
        const bool sharedReference = strcmp(expected.filename, "shared-reference.glb") == 0;
        const float metallic = sharedImage || sharedReference ? 1.0f : 0.0f;
        const float roughness = sharedReference ? 128.0f / 255.0f : 1.0f;
        if (!Near(material.metallicFactor, metallic) || !Near(material.roughnessFactor, roughness))
        {
            failure.Assign("normal fixture material factors differ from its geometric reference: ");
            failure.Append(expected.filename);
            valid = false;
        }
        if (material.normalTextureIndex != expected.normalTextureIndex || !Near(material.normalScale, expected.normalScale))
        {
            failure.Assign("normal texture slot or scale was not retained: ");
            failure.Append(expected.filename);
            valid = false;
        }
    }
    if (valid)
    {
        // model頂点のnormal、tangent、独立UVを確認するloop。
        for (uint32_t index = 0; index < model->vertices.Count(); ++index)
        {
            // loaderが変換して保持した現在の頂点。
            const gk::detail::ModelVertex& vertex = model->vertices.At(index);
            // normal textureを使わないreference fixtureの期待法線。
            float expectedNormal[3] = { 0.0f, 0.0f, -1.0f };
            if (!expected.hasNormalTexture)
            {
                const uint8_t normalPixel[4] = { 192, 192, 255, 255 };
                const uint8_t sharedPixel[4] = { 128, 128, 255, 255 };
                const uint8_t patternPixels[4][4] = { { 64, 128, 255, 255 }, { 192, 128, 255, 255 }, { 128, 64, 255, 255 }, { 128, 192, 255, 255 } };
                if (strcmp(expected.filename, "normal-reference.glb") == 0)
                    ReferenceNormal(normalPixel, 1.0f, 1.0f, expectedNormal);
                else if (strcmp(expected.filename, "scale-two-reference.glb") == 0)
                    ReferenceNormal(normalPixel, 2.0f, 1.0f, expectedNormal);
                else if (strcmp(expected.filename, "scale-negative-reference.glb") == 0)
                    ReferenceNormal(normalPixel, -1.0f, 1.0f, expectedNormal);
                else if (strcmp(expected.filename, "mirrored-reference.glb") == 0)
                    ReferenceNormal(normalPixel, 1.0f, -1.0f, expectedNormal);
                else if (strcmp(expected.filename, "shared-reference.glb") == 0)
                    ReferenceNormal(sharedPixel, 1.0f, 1.0f, expectedNormal);
                else if (strcmp(expected.filename, "node-mirror-reference.glb") == 0)
                    ReferenceNormal(normalPixel, 1.0f, 1.0f, expectedNormal);
                else if (patternReference)
                    ReferenceNormal(patternPixels[(index % 16) / 4], 1.0f, 1.0f, expectedNormal);
                if (expected.mirroredNode)
                    expectedNormal[0] = -expectedNormal[0];
            }
            if (!Near(vertex.normal[0], expectedNormal[0]) || !Near(vertex.normal[1], expectedNormal[1]) || !Near(vertex.normal[2], expectedNormal[2]))
            {
                fprintf(stderr, "normal actual=(%.6f, %.6f, %.6f) expected=(%.6f, %.6f, %.6f)\n", vertex.normal[0], vertex.normal[1], vertex.normal[2], expectedNormal[0], expectedNormal[1], expectedNormal[2]);
                failure.Assign("normal fixture world normal is incorrect: ");
                failure.Append(expected.filename);
                valid = false;
                break;
            }
            // base colorは常にUV0、normal画像だけが別setを選ぶ。
            const float expectedBaseUv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
            const uint32_t baseUvIndex = mixedSigns && index >= 3 ? (index == 3 ? 0 : (index == 4 ? 2 : 3)) : index % 4;
            if (!Near(vertex.uv[0], expectedBaseUv[baseUvIndex][0]) || !Near(vertex.uv[1], expectedBaseUv[baseUvIndex][1]))
            {
                failure.Assign("normal texture selection changed base color UV0: ");
                failure.Append(expected.filename);
                valid = false;
                break;
            }
            if (expected.hasNormalTexture)
            {
                // normal mapがある頂点ではexplicitな接線基底が保持される。
                const float expectedTangentX = expected.mirroredNode ? -1.0f : 1.0f;
                const float fixtureTangentSign = expected.mixedTangentSigns && index >= 3 ? -1.0f : expected.tangentSign;
                const float expectedTangentW = fixtureTangentSign * (expected.mirroredNode ? -1.0f : 1.0f);
                if (!Near(vertex.tangent[0], expectedTangentX) || !Near(vertex.tangent[1], 0.0f) || !Near(vertex.tangent[2], 0.0f) || !Near(vertex.tangent[3], expectedTangentW))
                {
                    failure.Assign("normal fixture tangent transform is incorrect: ");
                    failure.Append(expected.filename);
                    valid = false;
                    break;
                }
                // normal textureが選んだUVはbase color UVと独立して格納される。
                const float expectedNormalUv[2] = { expected.normalUsesUv1 ? ((baseUvIndex == 0 || baseUvIndex == 3) ? 1.0f : 0.0f) : vertex.uv[0], vertex.uv[1] };
                if (!Near(vertex.normalUv[0], expectedNormalUv[0]) || !Near(vertex.normalUv[1], expectedNormalUv[1]))
                {
                    failure.Assign("normal fixture did not preserve its selected UV set: ");
                    failure.Append(expected.filename);
                    valid = false;
                    break;
                }
            }
            else
            {
                if (!Near(vertex.normalUv[0], 0.0f) || !Near(vertex.normalUv[1], 0.0f) || !Near(vertex.tangent[0], 0.0f) || !Near(vertex.tangent[1], 0.0f) || !Near(vertex.tangent[2], 0.0f) || !Near(vertex.tangent[3], 0.0f))
                {
                    failure.Assign("normal-map vertex fields changed when no normal texture is present: ");
                    failure.Append(expected.filename);
                    valid = false;
                    break;
                }
            }
        }
    }
    if (valid)
    {
        // 画像slotと頂点材質情報が描画計画へ届くか確認するloop。
        gk::render::ModelDrawPlan plan;
        if (!gk::render::BuildModelDrawPlan(*model, plan, error) || plan.parts.Count() != expectedPrimitiveCount)
        {
            failure.Assign("normal material did not produce a draw plan: ");
            failure.Append(expected.filename);
            valid = false;
        }
        if (valid)
        {
            for (uint32_t index = 0; index < plan.parts.Count(); ++index)
            {
                if (plan.parts.At(index).normalTextureIndex != expected.normalTextureIndex || !Near(plan.parts.At(index).normalScale, expected.normalScale))
                {
                    failure.Assign("normal texture settings did not reach the draw plan: ");
                    failure.Append(expected.filename);
                    valid = false;
                    break;
                }
            }
        }
    }
    // test registryからfixture modelを削除する結果。
    const bool deleted = gk::detail::DeleteModel(handle, error);
    if (!deleted && valid)
    {
        failure.Assign("normal fixture model could not be released: ");
        failure.Append(expected.filename);
        valid = false;
    }
    return valid;
}

/**
 * 不正GLBがloaderに拒否され、理由を返すことを確認する。
 */
bool CheckRejectedFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // loaderへ渡すUTF-8 path。
    const std::string path = (directory / filename).u8string();
    // 拒否理由を受け取る文字列。
    gk::String error;
    // invalid fixtureを登録できるか確認するhandle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (handle.IsValid())
    {
        gk::detail::DeleteModel(handle, error);
        failure.Assign("invalid normal fixture was accepted: ");
        failure.Append(filename);
        return false;
    }
    if (error.Empty())
    {
        failure.Assign("rejected normal fixture had no diagnostic: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 不正なnormal材質でも、既存の描画計画を置き換えないことを確認する。
 */
bool CheckInvalidNormalPlansPreserveOutput()
{
    // 1つの有効triangleを持つ検査model。
    gk::detail::ModelResource model{};
    // 有効なtriangleの3頂点とindexを作るloop。
    for (uint32_t index = 0; index < 3; ++index)
    {
        // 描画計画の範囲検証に使う頂点。
        gk::detail::ModelVertex vertex{};
        vertex.position[0] = static_cast<float>(index);
        vertex.normal[2] = -1.0f;
        model.vertices.Append(vertex);
        model.indices.Append(index);
    }
    // normal texture参照を差し替えて検証する材質。
    gk::detail::ModelMaterial material{};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 1.0f;
    material.baseColorTextureIndex = -1;
    material.normalTextureIndex = 99;
    model.materials.Append(material);
    model.primitives.Append({ 0, 3, 0 });
    // failureで維持される既存planのsentinel。
    gk::render::ModelDrawPlan plan;
    gk::render::ModelPartPlan sentinel{};
    sentinel.firstIndex = 23;
    plan.parts.Append(sentinel);
    // invalid indexを検査する診断文字列。
    gk::String error;
    if (gk::render::BuildModelDrawPlan(model, plan, error) || plan.parts.Count() != 1 || plan.parts.At(0).firstIndex != 23)
    {
        fprintf(stderr, "invalid normal texture index changed the existing draw plan\n");
        return false;
    }
    material.normalTextureIndex = -1;
    material.normalScale = std::numeric_limits<float>::quiet_NaN();
    model.materials.At(0) = material;
    if (gk::render::BuildModelDrawPlan(model, plan, error) || plan.parts.Count() != 1 || plan.parts.At(0).firstIndex != 23)
    {
        fprintf(stderr, "non-finite normal scale changed the existing draw plan\n");
        return false;
    }
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: model_normal_tests <fixture-directory>\n");
        return 2;
    }
    // generator出力を格納したGLB directory。
    const std::filesystem::path directory(argv[1]);
    // fixture検査から返るfailure診断。
    gk::String failure;
    // loader実行前に存在とGLB headerを確認する全34 fixture。
    const char* fixtureNames[] = { "no-normal.glb", "uniform-normal.glb", "normal-reference.glb", "alpha-ignored.glb", "scale-zero.glb", "scale-two.glb", "scale-two-reference.glb", "scale-negative.glb", "scale-negative-reference.glb", "mirrored-tangent.glb", "mirrored-reference.glb", "uv1-pattern.glb", "uv1-reference.glb", "shared-image.glb", "shared-reference.glb", "shared-image-aliases.glb", "distinct-image-records.glb", "mixed-role-image-alias.glb", "node-mirror-normal.glb", "node-mirror-reference.glb", "tiny-node-scale.glb", "mixed-sign-triangles.glb", "missing-tangent.glb", "missing-normal.glb", "missing-normal-uv.glb", "bad-tangent-w.glb", "tangent-w-mismatch.glb", "parallel-tangent.glb", "zero-tangent.glb", "singular-node.glb", "bad-normal-scale.glb", "transform-normal.glb", "alias-missing-uv.glb", "alias-transform.glb" };
    // すべてのpositive/negative fixtureをloaderより先に検証するloop。
    for (const char* filename : fixtureNames)
    {
        if (!CheckFixture(directory, filename, failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
    }
    // positive fixtureごとに期待するslot、scale、UV、接線向き。
    const FNormalExpectation positives[] = {
        { "no-normal.glb", 1, -1, 1.0f, false, 1.0f, false, false }, { "uniform-normal.glb", 2, 1, 1.0f, false, 1.0f, false, true }, { "normal-reference.glb", 1, -1, 1.0f, false, 1.0f, false, false }, { "alpha-ignored.glb", 2, 1, 1.0f, false, 1.0f, false, true }, { "scale-zero.glb", 2, 1, 0.0f, false, 1.0f, false, true }, { "scale-two.glb", 2, 1, 2.0f, false, 1.0f, false, true }, { "scale-two-reference.glb", 1, -1, 1.0f, false, 1.0f, false, false }, { "scale-negative.glb", 2, 1, -1.0f, false, 1.0f, false, true }, { "scale-negative-reference.glb", 1, -1, 1.0f, false, 1.0f, false, false }, { "mirrored-tangent.glb", 2, 1, 1.0f, false, -1.0f, false, true }, { "mirrored-reference.glb", 1, -1, 1.0f, false, 1.0f, false, false }, { "uv1-pattern.glb", 2, 1, 1.0f, true, 1.0f, false, true }, { "uv1-reference.glb", 1, -1, 1.0f, false, 1.0f, false, false }, { "shared-image.glb", 1, 0, 1.0f, false, 1.0f, false, true }, { "shared-reference.glb", 1, -1, 1.0f, false, 1.0f, false, false }, { "shared-image-aliases.glb", 1, 0, 1.0f, true, 1.0f, false, true }, { "distinct-image-records.glb", 2, 1, 1.0f, false, 1.0f, false, true }, { "mixed-role-image-alias.glb", 2, 1, 1.0f, true, 1.0f, false, true }, { "node-mirror-normal.glb", 2, 1, 1.0f, false, 1.0f, true, true }, { "node-mirror-reference.glb", 1, -1, 1.0f, false, 1.0f, true, false }, { "mixed-sign-triangles.glb", 2, 1, 1.0f, false, 1.0f, false, true, true }, { "tiny-node-scale.glb", 2, 1, 1.0f, false, 1.0f, false, true },
    };
    // 22件の正常fixtureを期待値と照合するloop。
    for (const FNormalExpectation& expected : positives)
    {
        if (!CheckLoadedFixture(directory, expected, failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
    }
    // normal画像のalphaが異なってもRGB payloadが保持されるか。
    const std::string alphaPath = (directory / "alpha-ignored.glb").u8string();
    gk::String error;
    const gk::ModelHandle alphaHandle = gk::detail::LoadModel(alphaPath.c_str(), error);
    if (!alphaHandle.IsValid())
    {
        fprintf(stderr, "alpha-ignored fixture could not be loaded: %s\n", error.CStr());
        return 1;
    }
    // alpha zeroでRGBが保たれたnormal画像。
    const gk::detail::ModelResource* alphaModel = gk::detail::FindModel(alphaHandle);
    const uint8_t alphaIgnoredPixel[4] = { 192, 192, 255, 0 };
    const bool alphaValid = alphaModel && alphaModel->textures.Count() == 2 && CheckPixel(*alphaModel->textures.At(1), alphaIgnoredPixel);
    const bool alphaDeleted = gk::detail::DeleteModel(alphaHandle, error);
    if (!alphaValid || !alphaDeleted)
    {
        fprintf(stderr, "normal image alpha handling changed its RGB payload\n");
        return 1;
    }
    // normal画像、UV1 pattern、shared textureのslotを確認するcase。
    const char* imageCases[] = { "uniform-normal.glb", "uv1-pattern.glb", "shared-image.glb", "shared-image-aliases.glb", "distinct-image-records.glb", "mixed-role-image-alias.glb" };
    // 代表PNGのpixel、四象限、共有slotを調べるloop。
    for (const char* filename : imageCases)
    {
        // 対象fixtureのUTF-8 path。
        const std::string imagePath = (directory / filename).u8string();
        // image payload確認用handle。
        const gk::ModelHandle imageHandle = gk::detail::LoadModel(imagePath.c_str(), error);
        if (!imageHandle.IsValid())
        {
            fprintf(stderr, "normal image fixture could not be loaded: %s\n", filename);
            return 1;
        }
        // registryから借用する画像payload。
        const gk::detail::ModelResource* imageModel = gk::detail::FindModel(imageHandle);
        bool imageValid = imageModel != nullptr;
        if (imageValid && strcmp(filename, "uniform-normal.glb") == 0)
        {
            const uint8_t normalPixel[4] = { 192, 192, 255, 255 };
            imageValid = imageModel->textures.Count() == 2 && CheckPixel(*imageModel->textures.At(1), normalPixel);
        }
        if (imageValid && strcmp(filename, "uv1-pattern.glb") == 0)
            imageValid = imageModel->textures.Count() == 2 && CheckPatternImage(*imageModel->textures.At(1));
        if (imageValid && (strcmp(filename, "shared-image.glb") == 0 || strcmp(filename, "shared-image-aliases.glb") == 0))
        {
            const uint8_t sharedPixel[4] = { 128, 128, 255, 255 };
            imageValid = imageModel->textures.Count() == 1 && CheckPixel(*imageModel->textures.At(0), sharedPixel) && imageModel->materials.At(0).baseColorTextureIndex == 0 && imageModel->materials.At(0).metallicRoughnessTextureIndex == 0 && imageModel->materials.At(0).normalTextureIndex == 0;
        }
        if (imageValid && strcmp(filename, "distinct-image-records.glb") == 0)
        {
            const uint8_t sharedPixel[4] = { 128, 128, 255, 255 };
            imageValid = imageModel->textures.Count() == 2 && CheckPixel(*imageModel->textures.At(0), sharedPixel) && CheckPixel(*imageModel->textures.At(1), sharedPixel) && imageModel->textures.At(0) != imageModel->textures.At(1) && imageModel->materials.At(0).baseColorTextureIndex == 0 && imageModel->materials.At(0).metallicRoughnessTextureIndex == 1 && imageModel->materials.At(0).normalTextureIndex == 1;
        }
        if (imageValid && strcmp(filename, "mixed-role-image-alias.glb") == 0)
            imageValid = imageModel->textures.Count() == 2 && imageModel->materials.At(0).baseColorTextureIndex == 0 && imageModel->materials.At(0).metallicRoughnessTextureIndex == 1 && imageModel->materials.At(0).normalTextureIndex == 1;
        if (imageValid && strcmp(filename, "shared-image-aliases.glb") == 0)
            imageValid = imageModel->materials.At(0).metallicRoughnessTextureIndex == imageModel->materials.At(0).normalTextureIndex;
        if (imageValid && (strcmp(filename, "shared-image-aliases.glb") == 0 || strcmp(filename, "mixed-role-image-alias.glb") == 0))
        {
            const float baseUv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
            const float normalUv[4][2] = { { 1, 1 }, { 0, 1 }, { 0, 0 }, { 1, 0 } };
            for (uint32_t vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
            {
                const gk::detail::ModelVertex& vertex = imageModel->vertices.At(vertexIndex);
                if (!Near(vertex.uv[0], baseUv[vertexIndex][0]) || !Near(vertex.uv[1], baseUv[vertexIndex][1]) || !Near(vertex.metallicRoughnessUv[0], baseUv[vertexIndex][0]) || !Near(vertex.metallicRoughnessUv[1], baseUv[vertexIndex][1]) || !Near(vertex.normalUv[0], normalUv[vertexIndex][0]) || !Near(vertex.normalUv[1], normalUv[vertexIndex][1]))
                {
                    imageValid = false;
                    break;
                }
            }
        }
        const bool imageDeleted = gk::detail::DeleteModel(imageHandle, error);
        if (!imageValid || !imageDeleted)
        {
            fprintf(stderr, "normal texture payload or shared image slot is incorrect: %s\n", filename);
            return 1;
        }
    }
    // 必須属性、画像座標、接線基底、scale、未対応変換の拒否を確認するloop。
    const char* rejected[] = { "missing-normal-uv.glb", "bad-tangent-w.glb", "tangent-w-mismatch.glb", "parallel-tangent.glb", "zero-tangent.glb", "singular-node.glb", "bad-normal-scale.glb", "transform-normal.glb", "alias-missing-uv.glb", "alias-transform.glb" };
    // 不正normal入力を拒否するloop。
    for (const char* filename : rejected)
    {
        if (!CheckRejectedFixture(directory, filename, failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
    }
    if (!CheckInvalidNormalPlansPreserveOutput())
        return 1;
    return 0;
}
