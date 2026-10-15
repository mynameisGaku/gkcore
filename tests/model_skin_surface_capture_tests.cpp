// SPDX-License-Identifier: NOASSERTION
#include "tests/support/ModelSkinSurfaceCapture.h"
#include "foundation/Memory.h"

#include <stdio.h>
#include <string.h>
#include <limits>

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

}

int main()
{
    const bool mapping = TestDrawMappingAndCorrections();
    const bool multiple = TestMultipleBodySegments();
    const bool invalid = TestInvalidAndAtomic();
    const bool allocation = TestAllocationAtomicity();
    return mapping && multiple && invalid && allocation ? 0 : 1;
}
