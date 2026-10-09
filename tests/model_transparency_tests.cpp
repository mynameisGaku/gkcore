#include "render/ModelTransparency.h"
#include "render/ModelDrawPlan.h"
#include "internal/Backend.hpp"

#include <math.h>
#include <stdio.h>

namespace
{

using namespace gk;
using namespace gk::detail;
using namespace gk::render;

/**
 * 条件を満たさない場合にcase名を表示する。
 */
bool Check(bool condition, const char* label)
{
    if (condition)
    {
        return true;
    }
    fprintf(stderr, "model transparency test failed: %s\n", label);
    return false;
}

/**
 * 指定奥行きへモデルlocalの三角形を追加する。
 */
void AddTriangle(ModelResource& model, float z)
{
    const uint32_t first = model.vertices.Count();
    ModelVertex vertices[3]{};
    vertices[1].position[0] = 1.0f;
    vertices[2].position[1] = 1.0f;
    for (uint32_t vertex = 0; vertex < 3; ++vertex)
    {
        vertices[vertex].position[2] = z;
        model.vertices.Append(vertices[vertex]);
        model.indices.Append(first + vertex);
    }
}

/**
 * 透明材質primitiveだけを持つ有効な描画modelを作る。
 */
void AddTransparentMaterial(ModelResource& model)
{
    ModelMaterial material{};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 0.5f;
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 1.0f;
    material.baseColorTextureIndex = -1;
    material.alphaBlend = true;
    model.materials.Append(material);
}

/**
 * camera方向、draw順、primitive順の透明triangle sortingを確認する。
 */
bool TestSceneSortAndCameraDirection()
{
    ModelResource firstModel{};
    AddTransparentMaterial(firstModel);
    AddTriangle(firstModel, 1.0f);
    AddTriangle(firstModel, 5.0f);
    AddTriangle(firstModel, 5.0f);
    firstModel.primitives.Append({ 0, 3, 0 });
    firstModel.primitives.Append({ 3, 3, 0 });
    firstModel.primitives.Append({ 6, 3, 0 });

    ModelResource secondModel{};
    AddTransparentMaterial(secondModel);
    AddTriangle(secondModel, 5.0f);
    secondModel.primitives.Append({ 0, 3, 0 });

    FramePacket frame{};
    frame.width = 640;
    frame.height = 480;
    frame.cameraTarget = Vec3{ 0.0f, 0.0f, 1.0f };
    DrawPacket first{};
    first.kind = DrawKind::Model;
    first.layer = 0;
    first.sequence = 20;
    first.model = &firstModel;
    first.cameraPosition = Vec3{ 0.0f, 0.0f, -3.0f };
    first.cameraTarget = Vec3{ 0.0f, 0.0f, 0.0f };
    first.modelScale = Vec3{ 1.0f, 1.0f, 1.0f };
    DrawPacket second{};
    second.kind = DrawKind::Model;
    second.layer = 0;
    second.sequence = 10;
    second.model = &secondModel;
    second.cameraPosition = first.cameraPosition;
    second.cameraTarget = first.cameraTarget;
    second.modelScale = Vec3{ 1.0f, 1.0f, 1.0f };
    DrawPacket ui{};
    ui.kind = DrawKind::Model;
    ui.layer = 1;
    ui.sequence = 0;
    ui.model = &secondModel;
    ui.cameraPosition = first.cameraPosition;
    ui.cameraTarget = first.cameraTarget;
    ui.modelPosition = Vec3{ 0.0f, 0.0f, 100.0f };
    ui.modelScale = Vec3{ 1.0f, 1.0f, 1.0f };
    if (!Check(frame.draws.Append(first) && frame.draws.Append(second) && frame.draws.Append(ui), "frame fixture allocation"))
    {
        return false;
    }

    Array<FModelTransparencyDraw> plan;
    String error;
    if (!Check(BuildModelTransparencyPlan(frame, 8, plan, error), "valid scene plan builds"))
    {
        fprintf(stderr, "%s\n", error.CStr());
        return false;
    }
    if (!Check(plan.Count() == 4, "UI draw is excluded and every scene triangle is retained"))
    {
        return false;
    }
    if (!Check(plan.At(0).drawIndex == 1 && plan.At(0).partIndex == 0 && plan.At(0).firstIndex == 0 && plan.At(1).drawIndex == 0 && plan.At(1).partIndex == 1 && plan.At(1).firstIndex == 3 && plan.At(2).drawIndex == 0 && plan.At(2).partIndex == 2 && plan.At(2).firstIndex == 6 && plan.At(3).drawIndex == 0 && plan.At(3).partIndex == 0 && plan.At(3).firstIndex == 0, "farther triangles and exact-depth stable ties use expected order"))
    {
        return false;
    }

    ModelDrawPlan::FRange cachedRanges[3]{};
    Array<ModelPartPlan> cachedParts;
    ModelDrawPlan firstPlan;
    ModelDrawPlan secondPlan;
    if (!Check(BuildModelDrawPlan(firstModel, firstPlan, error) && BuildModelDrawPlan(secondModel, secondPlan, error), "cached draw plan fixture builds"))
        return false;
    cachedRanges[0] = { 0, firstPlan.parts.Count() };
    if (!Check(cachedParts.AppendRange(firstPlan.parts.Data(), firstPlan.parts.Count()), "cached first draw plan appends"))
        return false;
    cachedRanges[1] = { cachedParts.Count(), secondPlan.parts.Count() };
    if (!Check(cachedParts.AppendRange(secondPlan.parts.Data(), secondPlan.parts.Count()), "cached second draw plan appends"))
        return false;
    cachedRanges[2] = { cachedParts.Count(), secondPlan.parts.Count() };
    if (!Check(cachedParts.AppendRange(secondPlan.parts.Data(), secondPlan.parts.Count()), "cached UI draw plan appends"))
        return false;
    Array<FModelTransparencyDraw> cachedPlan;
    if (!Check(BuildModelTransparencyPlan(frame, 8, cachedPlan, error, cachedRanges, 3, cachedParts.Data(), cachedParts.Count()), "cached draw plan builds transparent order"))
        return false;
    if (!Check(cachedPlan.Count() == plan.Count(), "cached and direct transparent plan counts match"))
        return false;
    for (uint32_t index = 0; index < plan.Count(); ++index)
        if (!Check(cachedPlan.At(index).drawIndex == plan.At(index).drawIndex && cachedPlan.At(index).partIndex == plan.At(index).partIndex && cachedPlan.At(index).firstIndex == plan.At(index).firstIndex && cachedPlan.At(index).depth == plan.At(index).depth, "cached draw plans preserve transparent ordering"))
            return false;

    frame.draws.At(0).cameraPosition = Vec3{ 0.0f, 0.0f, 6.0f };
    frame.draws.At(0).cameraTarget = Vec3{ 0.0f, 0.0f, 0.0f };
    frame.draws.At(1).cameraPosition = frame.draws.At(0).cameraPosition;
    frame.draws.At(1).cameraTarget = frame.draws.At(0).cameraTarget;
    plan.Reset();
    if (!Check(BuildModelTransparencyPlan(frame, 8, plan, error), "reverse camera plan builds"))
    {
        fprintf(stderr, "%s\n", error.CStr());
        return false;
    }
    return Check(plan.At(0).drawIndex == 0 && plan.At(0).partIndex == 0 && plan.At(1).drawIndex == 1 && plan.At(1).partIndex == 0 && plan.At(2).drawIndex == 0 && plan.At(2).partIndex == 1 && plan.At(3).drawIndex == 0 && plan.At(3).partIndex == 2, "camera direction reverses the far-to-near order");
}

/**
 * Scene命令、custom shader、camera切替で透明sort群を分割し、UIは無視する。
 */
bool TestSceneSortBarriers()
{
    ModelResource model{};
    AddTransparentMaterial(model);
    AddTriangle(model, 1.0f);
    model.primitives.Append({ 0, 3, 0 });

    FramePacket frame{};
    frame.width = 640;
    frame.height = 480;
    DrawPacket first{};
    first.kind = DrawKind::Model;
    first.layer = 0;
    first.sequence = 1;
    first.model = &model;
    first.cameraPosition = Vec3{ 0.0f, 0.0f, -3.0f };
    first.cameraTarget = Vec3{ 0.0f, 0.0f, 0.0f };
    first.modelScale = Vec3{ 1.0f, 1.0f, 1.0f };
    DrawPacket rect{};
    rect.kind = DrawKind::Rect;
    rect.layer = 0;
    rect.sequence = 2;
    DrawPacket second = first;
    second.sequence = 4;
    DrawPacket ui{};
    ui.kind = DrawKind::Model;
    ui.layer = 1;
    ui.sequence = 5;
    ui.model = &model;
    ui.cameraPosition = first.cameraPosition;
    ui.cameraTarget = first.cameraTarget;
    ui.modelPosition = Vec3{ 0.0f, 0.0f, 100.0f };
    ui.modelScale = Vec3{ 1.0f, 1.0f, 1.0f };
    DrawPacket third = first;
    third.sequence = 6;
    DrawPacket custom = first;
    custom.sequence = 7;
    custom.shader = ShaderHandle(1);
    DrawPacket fourth = first;
    fourth.sequence = 8;
    DrawPacket changedCamera = first;
    changedCamera.sequence = 9;
    changedCamera.cameraPosition = Vec3{ 0.0f, 0.0f, -4.0f };
    if (!Check(frame.draws.Append(first) && frame.draws.Append(rect) && frame.draws.Append(second) && frame.draws.Append(ui) && frame.draws.Append(third) && frame.draws.Append(custom) && frame.draws.Append(fourth) && frame.draws.Append(changedCamera), "barrier frame fixture allocation"))
    {
        return false;
    }

    Array<FModelTransparencyDraw> plan;
    String error;
    if (!Check(BuildModelTransparencyPlan(frame, 8, plan, error), "barrier plan builds"))
    {
        fprintf(stderr, "%s\n", error.CStr());
        return false;
    }
    if (!Check(plan.Count() == 5, "UI and custom shader models are excluded from standard transparency"))
    {
        return false;
    }
    return Check(plan.At(0).drawIndex == 0 && plan.At(0).barrierIndex == 1 && plan.At(1).drawIndex == 2 && plan.At(1).barrierIndex == 5 && plan.At(2).drawIndex == 4 && plan.At(2).barrierIndex == 5 && plan.At(3).drawIndex == 6 && plan.At(3).barrierIndex == 7 && plan.At(4).drawIndex == 7 && plan.At(4).barrierIndex == 8, "Scene commands, custom shaders, and camera changes delimit groups while UI does not");
}

/**
 * triangle上限と破損draw時に既存出力を保つ。
 */
bool TestFailurePreservesOutput()
{
    ModelResource model{};
    AddTransparentMaterial(model);
    AddTriangle(model, 2.0f);
    model.primitives.Append({ 0, 3, 0 });
    FramePacket frame{};
    frame.width = 640;
    frame.height = 480;
    frame.cameraTarget = Vec3{ 0.0f, 0.0f, 1.0f };
    DrawPacket draw{};
    draw.kind = DrawKind::Model;
    draw.layer = 0;
    draw.model = &model;
    draw.cameraTarget = frame.cameraTarget;
    draw.modelScale = Vec3{ 1.0f, 1.0f, 1.0f };
    if (!Check(frame.draws.Append(draw), "failure frame fixture allocation"))
    {
        return false;
    }
    Array<FModelTransparencyDraw> output;
    output.Append({ 7, 8, 9, 10, 11, 12.0 });
    String error;
    if (!Check(!BuildModelTransparencyPlan(frame, 0, output, error), "zero triangle limit is rejected"))
    {
        return false;
    }
    if (!Check(output.Count() == 1 && output.At(0).drawIndex == 7 && output.At(0).firstIndex == 9 && error.Length() != 0, "limit failure preserves prior plan and gives a diagnostic"))
    {
        return false;
    }
    ModelDrawPlan::FRange invalidCachedRange{ 2, 1 };
    ModelPartPlan cachedPart{};
    cachedPart.alphaBlend = true;
    if (!Check(!BuildModelTransparencyPlan(frame, 4, output, error, &invalidCachedRange, 1, &cachedPart, 1), "invalid cached draw plan range is rejected"))
        return false;
    if (!Check(output.Count() == 1 && output.At(0).drawIndex == 7 && error.Length() != 0, "cached draw plan failure preserves prior output"))
        return false;
    error.Clear();
    model.primitives.At(0).firstIndex = 3;
    if (!Check(!BuildModelTransparencyPlan(frame, 4, output, error), "invalid transparent primitive range is rejected"))
    {
        return false;
    }
    return Check(output.Count() == 1 && output.At(0).drawIndex == 7 && output.At(0).firstIndex == 9 && error.Length() != 0, "invalid model failure preserves prior plan and gives a diagnostic");
}

}

int main()
{
    return TestSceneSortAndCameraDirection() && TestSceneSortBarriers() && TestFailurePreservesOutput() ? 0 : 1;
}
