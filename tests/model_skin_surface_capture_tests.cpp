// SPDX-License-Identifier: NOASSERTION
#include "tests/support/ModelSkinSurfaceCapture.h"
#include "foundation/Memory.h"

#include <stdio.h>
#include <string.h>
#include <limits>
#include <stdlib.h>

namespace
{

using namespace gk;
using namespace gk::testing;
using namespace gk::model::animation;

/**
 * 描画corner順とunique位置順が異なる、身体と衣服の最小入力。
 */
struct FFixture
{
    FModelGpuSkinningGeometry geometry;
    Array<FModelSparseVertexMap> map;
    Array<uint32_t> indices;
    FModelSparsePoseGeometry reference;
    Array<FModelSkinPointCorrection> corrections;
    Array<uint32_t> cloth;
    Array<FModelSecondaryMotionCollisionShape> shapes;

    /**
     * faceのfanと異なる描画三角形を用意し、実index対応を検査する。
     */
    bool Initialize()
    {
        for (uint32_t id = 0u; id < 6u; ++id)
        {
            if (!geometry.positions.Append({ { static_cast<double>(id), 0.0, 0.0 } }) || !reference.positions.Append({ { static_cast<float>(id), 0.0f, 0.0f, 1.0f } }))
            {
                return false;
            }
        }
        FModelGpuSkinningGeometry::FSegment body{};
        body.positionCount = 4u;
        body.nodeIndex = 3u;
        FModelGpuSkinningGeometry::FSegment garment{};
        garment.firstPosition = 4u;
        garment.positionCount = 2u;
        garment.nodeIndex = 7u;
        const FModelSparseVertexMap values[6] = { { 3u, 0u }, { 0u, 0u }, { 2u, 0u }, { 1u, 0u }, { 4u, 0u }, { 5u, 0u } };
        const uint32_t triangles[9] = { 0u, 1u, 2u, 3u, 1u, 2u, 4u, 5u, 4u };
        const FModelSkinPointCorrection correctionValues[2] = { { 2u, { 1.00000006, 2.0, 3.0 } }, { 4u, { 2.00000013, 4.0, 5.0 } } };
        const FModelSecondaryMotionCollisionShape shape = { {}, {}, 0.08f };
        return geometry.segments.Append(body) && geometry.segments.Append(garment) && geometry.faces.Append({ 0u, 4u }) && map.AppendRange(values, 6u) && indices.AppendRange(triangles, 9u) && corrections.AppendRange(correctionValues, 2u) && cloth.Append(4u) && cloth.Append(5u) && shapes.Append(shape);
    }

    /**
     * 実描画対応表と補正を通す。
     */
    bool Build(FModelSkinSurfaceCapture& output, String& error)
    {
        return BuildModelSkinSurfaceCapture(geometry, map, indices, 3u, "Yumeka_Body", reference, corrections, cloth, shapes, output, error);
    }
};

/**
 * 検査名と失敗理由を出す。
 */
bool Check(bool passed, const char* message)
{
    if (!passed)
    {
        fprintf(stderr, "%s\n", message);
    }
    return passed;
}

/**
 * face fanを使わず、実draw indexのwindingとfloat補正値を保存する。
 */
bool TestDrawMappingAndCorrections()
{
    FFixture fixture;
    FModelSkinSurfaceCapture output;
    String error;
    if (!fixture.Initialize() || !fixture.Build(output, error))
    {
        fprintf(stderr, "surface mapping failed: %s\n", error.CStr());
        return false;
    }
    const uint32_t expected[6] = { 3u, 0u, 2u, 1u, 0u, 2u };
    bool passed = Check(output.bodyNode == 3u && strcmp(output.bodyMeshName.CStr(), "Yumeka_Body") == 0 && output.bodySegmentIndices.Count() == 1u && output.bodyPoints.Count() == 4u && output.clothPoints.Count() == 2u && output.trianglePositionIds.Count() == 6u && memcmp(output.trianglePositionIds.Data(), expected, sizeof(expected)) == 0, "surface did not preserve actual draw triangle map");
    passed = Check(output.bodyPoints.At(2u).position[0] == static_cast<float>(1.00000006) && output.clothPoints.At(0u).position[0] == static_cast<float>(2.00000013) && output.bodyPoints.At(2u).positionId == 2u && output.clothPoints.At(0u).positionId == 4u && output.collisionShapes.At(0u).radius == 0.08f, "surface did not preserve rendered float correction or raw shape radius") && passed;
    String json;
    passed = Check(EncodeModelSkinSurfaceCapture(108u, output, json, error) && strstr(json.CStr(), "\"type\":\"gkcore_model_skin_surface_capture\"") && strstr(json.CStr(), "\"frame\":108") && strstr(json.CStr(), "\"trianglePositionIds\":[3,0,2,1,0,2]") && strstr(json.CStr(), "\"clothPointRadius\":0") && json.Length() > 0u && json.CStr()[json.Length() - 1u] == '\n', "surface JSON schema or final newline is invalid") && passed;
    output.bodyMeshName.Assign("body\"\\\n");
    passed = Check(EncodeModelSkinSurfaceCapture(0u, output, json, error) && strstr(json.CStr(), R"(body\"\\\u000a)"), "surface mesh name was not JSON escaped") && passed;
    return passed;
}

/**
 * 同じ身体nodeに属する複数segmentと、別IDの同じ位置をすべて保持する。
 */
bool TestMultipleBodySegments()
{
    FFixture fixture;
    FModelSkinSurfaceCapture output;
    String error;
    if (!fixture.Initialize())
    {
        return false;
    }
    fixture.geometry.segments.At(0u).positionCount = 2u;
    FModelGpuSkinningGeometry::FSegment split{};
    split.firstPosition = 2u;
    split.positionCount = 2u;
    split.nodeIndex = 3u;
    fixture.geometry.segments.Append(split);
    fixture.reference.positions.At(3u) = fixture.reference.positions.At(0u);
    return Check(fixture.Build(output, error) && output.bodySegmentIndices.Count() == 2u && output.bodyPoints.Count() == 4u && output.bodyPoints.At(0u).positionId != output.bodyPoints.At(3u).positionId && output.bodyPoints.At(0u).position[0] == output.bodyPoints.At(3u).position[0], "surface dropped a matching body segment or welded distinct source IDs");
}

/**
 * 不正入力で既存snapshotを変更しない。
 */
bool CheckRejected(FFixture& fixture, const char* label)
{
    FModelSkinSurfaceCapture output;
    output.bodyNode = 91u;
    output.bodyMeshName.Assign("preserved");
    output.bodyPoints.Append({ 77u, { 1.0f, 2.0f, 3.0f } });
    String error;
    return Check(!fixture.Build(output, error) && !error.Empty() && output.bodyNode == 91u && strcmp(output.bodyMeshName.CStr(), "preserved") == 0 && output.bodyPoints.Count() == 1u && output.bodyPoints.At(0u).positionId == 77u, label);
}

/**
 * 範囲、mesh混在、非有限、空の診断を既存出力から分離する。
 */
bool TestInvalidAndAtomic()
{
    bool passed = true;
    for (uint32_t kind = 0u; kind < 12u; ++kind)
    {
        FFixture f;
        if (!f.Initialize())
        {
            return false;
        }
        switch (kind)
        {
        case 0u:
            f.indices.At(0u) = 99u;
            break;
        case 1u:
            f.map.At(0u).positionIndex = 99u;
            break;
        case 2u:
            f.geometry.segments.At(0u).firstPosition = UINT32_MAX;
            break;
        case 3u:
            f.indices.At(2u) = 4u;
            break;
        case 4u:
            f.reference.positions.At(2u).value[0] = std::numeric_limits<float>::quiet_NaN();
            break;
        case 5u:
            f.corrections.At(1u).position[0] = std::numeric_limits<double>::infinity();
            break;
        case 6u:
            f.corrections.At(1u).position[0] = std::numeric_limits<double>::max();
            break;
        case 7u:
            f.cloth.At(0u) = 99u;
            break;
        case 8u:
            f.geometry.segments.At(0u).nodeIndex = 8u;
            break;
        case 9u:
            f.indices.Clear();
            break;
        case 10u:
            f.cloth.Clear();
            break;
        case 11u:
            f.shapes.At(0u).radius = -1.0f;
            break;
        }
        passed = CheckRejected(f, "invalid surface input changed output or was accepted") && passed;
    }
    return passed;
}

/**
 * 確保失敗の全境界でsnapshotとJSON文字列を保つ。
 */
bool TestAllocationAtomicity()
{
    FFixture f;
    if (!f.Initialize())
    {
        return false;
    }
    uint32_t failures = 0u;
    bool succeeded = false;
    for (uint32_t limit = 0u; limit < 128u; ++limit)
    {
        FModelSkinSurfaceCapture output;
        output.bodyNode = 91u;
        output.bodyMeshName.Assign("preserved");
        output.bodyPoints.Append({ 77u, { 1.0f, 2.0f, 3.0f } });
        String error;
        SetAllocationFailureAfterForTesting(limit);
        const bool built = f.Build(output, error);
        ResetAllocationFailureForTesting();
        if (built)
        {
            succeeded = true;
            break;
        }
        ++failures;
        if (!Check(output.bodyNode == 91u && strcmp(output.bodyMeshName.CStr(), "preserved") == 0 && output.bodyPoints.Count() == 1u && output.bodyPoints.At(0u).positionId == 77u, "surface allocation failure changed output"))
        {
            return false;
        }
    }
    FModelSkinSurfaceCapture snapshot;
    String error;
    if (!succeeded || failures == 0u || !f.Build(snapshot, error))
    {
        return false;
    }
    for (uint32_t limit = 0u; limit < 128u; ++limit)
    {
        String json;
        json.Assign("preserved");
        SetAllocationFailureAfterForTesting(limit);
        const bool encoded = EncodeModelSkinSurfaceCapture(0u, snapshot, json, error);
        ResetAllocationFailureForTesting();
        if (encoded)
        {
            return true;
        }
        if (!Check(strcmp(json.CStr(), "preserved") == 0, "surface encoding failure changed output"))
        {
            return false;
        }
    }
    return false;
}

/**
 * 実モデルと同じ339boneで、全要素と特殊な有限floatの保存を検査する。
 */
bool TestBaseMatrixEncoding()
{
    Array<const char*> names;
    Array<float> matrices;
    const float values[16] = { 1.0f, -0.0f, 0.1f, -0.25f, std::numeric_limits<float>::max(), std::numeric_limits<float>::min(), std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::max(), 1.00000011920928955078125f, 123.456f, -9.0f, 0.0f, 4.0f, 5.0f, 6.0f, 1.0f };
    for (uint32_t bone = 0u; bone < 339u; ++bone)
    {
        if (!names.Append(bone == 0u ? "" : (bone == 338u ? "Breast_1_L\"\\\n\t\001" : "bone")) || !matrices.AppendRange(values, 16u))
        {
            return false;
        }
        // 隣のboneの行列を再利用する誤りも検出する。
        matrices.At(bone * 16u + 12u) = static_cast<float>(bone);
    }
    String output;
    String error;
    output.Assign("preserved");
    if (!Check(EncodeModelSkinSurfaceBaseMatrices(54u, names, matrices, output, error), "全bone基準行列の正例をJSONLへ変換できません"))
    {
        return false;
    }
    const char* cursor = output.CStr();
    for (uint32_t bone = 0u; bone < names.Count(); ++bone)
    {
        char prefix[192]{};
        snprintf(prefix, sizeof(prefix), "{\"type\":\"gkcore_model_skin_surface_base_matrix\",\"frame\":54,\"bone\":%u,\"boneCount\":339,\"boneName\":", bone);
        if (!Check(strncmp(cursor, prefix, strlen(prefix)) == 0, "基準行列のframe・bone番号・全bone数が一致しません"))
        {
            return false;
        }
        if (bone == 0u && !Check(strncmp(cursor + strlen(prefix), "\"\",", 3u) == 0, "名前の無いbone0を空文字のまま保存できません"))
        {
            return false;
        }
        const char* end = strchr(cursor, '\n');
        const char* data = strstr(cursor, "\"previousBaseMatrix\":[");
        const char* metadata = strstr(cursor, "\"matrixLayout\":\"column-major\",\"matrixSource\":\"previousBaseMatrices from the current completed update; pre-secondary base; model space before instance SRT\"");
        if (!Check(end && data && data < end && metadata && metadata < end, "基準行列の配列または座標系metadataがありません"))
        {
            return false;
        }
        data += strlen("\"previousBaseMatrix\":[");
        for (uint32_t element = 0u; element < 16u; ++element)
        {
            char* next = nullptr;
            const float parsed = static_cast<float>(strtod(data, &next));
            if (!Check(next != data && memcmp(&parsed, matrices.Data() + bone * 16u + element, sizeof(float)) == 0 && *next == (element == 15u ? ']' : ','), "有限floatのbitまたはcolumn-majorの順番が変わりました"))
            {
                return false;
            }
            data = next + 1;
        }
        if (!Check(data[0] == '}' && data[1] == '\n', "基準行列JSONLの末尾が不正です"))
        {
            return false;
        }
        cursor = end + 1;
    }
    return Check(*cursor == '\0' && error.Empty() && strstr(output.CStr(), R"(Breast_1_L\"\\\u000a\u0009\u0001)"), "全bone数または名前のJSON escapeが一致しません");
}

/**
 * 最後のboneを含む入力全体を検査し、不正値で既存の出力を変更しない。
 */
bool TestBaseMatrixInvalidAtomicity()
{
    bool passed = true;
    for (uint32_t kind = 0u; kind < 8u; ++kind)
    {
        Array<const char*> names;
        Array<float> matrices;
        names.Append("first");
        names.Append("last");
        for (uint32_t element = 0u; element < 32u; ++element)
        {
            matrices.Append(1.0f);
        }
        switch (kind)
        {
        case 0u:
            names.Clear();
            matrices.Clear();
            break;
        case 1u:
            names.At(1u) = nullptr;
            break;
        case 2u:
            matrices.Clear();
            break;
        case 3u:
            names.Append("excess");
            break;
        case 4u:
            matrices.Append(0.0f);
            break;
        case 5u:
            matrices.At(31u) = std::numeric_limits<float>::quiet_NaN();
            break;
        case 6u:
            matrices.At(31u) = std::numeric_limits<float>::infinity();
            break;
        case 7u:
            matrices.At(31u) = -std::numeric_limits<float>::infinity();
            break;
        }
        String output;
        String error;
        output.Assign("preserved");
        passed = Check(!EncodeModelSkinSurfaceBaseMatrices(0u, names, matrices, output, error) && !error.Empty() && strcmp(output.CStr(), "preserved") == 0, "不正な基準行列を受理したか既存JSONを書き換えました") && passed;
        passed = Check(!EncodeModelSkinSurfaceBaseMatrices(0u, names, matrices, output, output) && strcmp(output.CStr(), "preserved") == 0, "基準行列の出力とerrorの同一指定を拒否できません") && passed;
    }
    return passed;
}

/**
 * JSON候補が途中までできた後を含む全確保境界で、出力を保持する。
 */
bool TestBaseMatrixAllocationAtomicity()
{
    Array<const char*> names;
    Array<float> matrices;
    for (uint32_t bone = 0u; bone < 3u; ++bone)
    {
        if (!names.Append("bone"))
        {
            return false;
        }
        for (uint32_t element = 0u; element < 16u; ++element)
        {
            matrices.Append(static_cast<float>(element));
        }
    }
    uint32_t failures = 0u;
    for (uint32_t limit = 0u; limit < 128u; ++limit)
    {
        String output;
        String error;
        output.Assign("preserved");
        error.Assign("diagnostic capacity reserved before failure injection; this storage is longer than any exporter error message");
        SetAllocationFailureAfterForTesting(limit);
        const bool encoded = EncodeModelSkinSurfaceBaseMatrices(20u, names, matrices, output, error);
        ResetAllocationFailureForTesting();
        if (encoded)
        {
            return Check(failures > 1u && error.Empty(), "基準行列JSONの途中確保失敗を検査できません");
        }
        ++failures;
        if (!Check(strcmp(output.CStr(), "preserved") == 0 && !error.Empty(), "基準行列JSONの確保失敗が既存出力またはerrorを壊しました"))
        {
            return false;
        }
    }
    return false;
}

}

int main()
{
    const bool mapping = TestDrawMappingAndCorrections();
    const bool multiple = TestMultipleBodySegments();
    const bool invalid = TestInvalidAndAtomic();
    const bool allocation = TestAllocationAtomicity();
    const bool baseMatrices = TestBaseMatrixEncoding();
    const bool baseInvalid = TestBaseMatrixInvalidAtomicity();
    const bool baseAllocation = TestBaseMatrixAllocationAtomicity();
    return mapping && multiple && invalid && allocation && baseMatrices && baseInvalid && baseAllocation ? 0 : 1;
}
