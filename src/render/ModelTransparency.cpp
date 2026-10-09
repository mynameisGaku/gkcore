// SPDX-License-Identifier: NOASSERTION
#include "render/ModelTransparency.h"
#include "render/WorldGeometry.h"
#include "render/ModelDrawPlan.h"
#include <stdlib.h>
namespace gk::render
{
namespace
{
/**
 * 奥行きが大きい順と、同距離の予約順を決定する。
 */
int CompareTriangle(const void* left, const void* right)
{
    const auto& a = *static_cast<const FModelTransparencyDraw*>(left);
    const auto& b = *static_cast<const FModelTransparencyDraw*>(right);
    if (a.barrierIndex != b.barrierIndex)
        return a.barrierIndex < b.barrierIndex ? -1 : 1;
    if (a.depth != b.depth)
        return a.depth > b.depth ? -1 : 1;
    if (a.sequence != b.sequence)
        return a.sequence < b.sequence ? -1 : 1;
    if (a.drawIndex != b.drawIndex)
        return a.drawIndex < b.drawIndex ? -1 : 1;
    if (a.partIndex != b.partIndex)
        return a.partIndex < b.partIndex ? -1 : 1;
    if (a.firstIndex != b.firstIndex)
        return a.firstIndex < b.firstIndex ? -1 : 1;
    return 0;
}

}

bool BuildModelTransparencyPlan(const detail::FramePacket& frame, uint32_t triangleLimit, Array<FModelTransparencyDraw>& output, String& error, const ModelDrawPlan::FRange* drawPlanRanges, uint32_t drawPlanRangeCount, const ModelPartPlan* drawPlanParts, uint32_t drawPlanPartCount)
{
    const bool useCachedPlans = drawPlanRanges != nullptr;
    if ((useCachedPlans && ((drawPlanPartCount && !drawPlanParts) || drawPlanRangeCount != frame.draws.Count())) || (!useCachedPlans && (drawPlanRangeCount || drawPlanParts || drawPlanPartCount)))
    {
        error.Assign("invalid cached model draw plan ranges");
        return false;
    }
    Array<FModelTransparencyDraw> candidate;
    // 現在の同一cameraモデル群の終端。各群につき1回だけ前方を調べる。
    uint32_t groupEnd = 0;
    for (uint32_t drawIndex = 0; drawIndex < frame.draws.Count(); ++drawIndex)
    {
        const auto& draw = frame.draws.At(drawIndex);
        if (draw.layer > 1)
        {
            error.Assign("invalid transparency draw layer");
            return false;
        }
        if (draw.layer != 0 || draw.kind != detail::DrawKind::Model)
            continue;
        if (!draw.model)
        {
            error.Assign("transparent model resource is missing");
            return false;
        }
        if (draw.shader.IsValid())
            continue;
        if (drawIndex >= groupEnd)
        {
            groupEnd = frame.draws.Count();
            for (uint32_t nextIndex = drawIndex + 1; nextIndex < frame.draws.Count(); ++nextIndex)
            {
                const auto& next = frame.draws.At(nextIndex);
                if (next.layer != 0)
                    continue;
                const bool sameCamera = next.cameraPosition.x == draw.cameraPosition.x && next.cameraPosition.y == draw.cameraPosition.y && next.cameraPosition.z == draw.cameraPosition.z && next.cameraTarget.x == draw.cameraTarget.x && next.cameraTarget.y == draw.cameraTarget.y && next.cameraTarget.z == draw.cameraTarget.z;
                if (next.kind != detail::DrawKind::Model || next.shader.IsValid() || !sameCamera)
                {
                    groupEnd = nextIndex;
                    break;
                }
            }
        }
        ModelDrawPlan plan;
        const ModelPartPlan* parts = nullptr;
        uint32_t partCount = 0;
        if (useCachedPlans)
        {
            const ModelDrawPlan::FRange& range = drawPlanRanges[drawIndex];
            if (range.firstPart > drawPlanPartCount || range.partCount > drawPlanPartCount - range.firstPart)
            {
                error.Assign("cached model draw plan range is invalid");
                return false;
            }
            parts = range.partCount ? drawPlanParts + range.firstPart : nullptr;
            partCount = range.partCount;
        }
        else
        {
            if (!BuildModelDrawPlan(*draw.model, plan, error))
                return false;
            parts = plan.parts.Data();
            partCount = plan.parts.Count();
        }
        FWorldGeometryContext geometryContext{};
        bool contextBuilt = false;
        for (uint32_t partIndex = 0; partIndex < partCount; ++partIndex)
        {
            const auto& part = parts[partIndex];
            if (!part.alphaBlend)
                continue;
            if (!contextBuilt)
            {
                if (!BuildWorldGeometryContext(draw, true, geometryContext, error))
                    return false;
                contextBuilt = true;
            }
            if (part.indexCount / 3 > triangleLimit - candidate.Count())
            {
                error.Assign("transparent triangle count exceeds its limit");
                return false;
            }
            for (uint32_t offset = 0; offset < part.indexCount; offset += 3)
            {
                FModelTransparencyDraw triangle{ drawIndex, partIndex, part.firstIndex + offset, groupEnd, draw.sequence, 0.0 };
                if (!ModelTriangleViewDepth(draw, geometryContext, triangle.firstIndex, triangle.depth, error) || !candidate.Append(triangle))
                {
                    if (error.Empty())
                        error.Assign("transparent triangle allocation failed");
                    return false;
                }
            }
        }
    }
    if (candidate.Count() > 1)
        qsort(candidate.Data(), candidate.Count(), sizeof(FModelTransparencyDraw), CompareTriangle);
    output.MoveFrom(candidate);
    error.Clear();
    return true;
}
}
