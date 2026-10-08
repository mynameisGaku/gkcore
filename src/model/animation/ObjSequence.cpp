// SPDX-License-Identifier: NOASSERTION
#include "ObjSequence.h"
#include "AModelAnimationSource.h"
#include "ModelPose.h"
#include "../Model.h"
#include "../ObjLoader.h"
#include "../../resources/ResourceIO.h"
#include "../../foundation/Memory.h"
#include <math.h>
#include <string.h>

/**
 * OBJ連番を共通animation sourceとして扱う処理。
 */
namespace gk::model
{
namespace
{
/**
 * 連番の頂点データを所有し、隣り合うframeを補間する。
 */
class AObjSequenceSource final : public AModelAnimationSource
{
  public:
    // 全frameを同じ頂点順序で連結したデータ。
    Array<detail::ModelVertex> frames;
    // 1frameの頂点数。
    uint32_t vertexCount = 0;
    // frame数。
    uint32_t frameCount = 0;
    // 1秒あたりのframe数。
    float framesPerSecond = 1.0f;
    // 骨格なしでframeの寄与率をmorph配列に保持する。
    animation::FModelSkeleton skeleton;

    EModelAnimationFormat Format() const override
    {
        return EModelAnimationFormat::ObjSequence;
    }
    const animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton;
    }
    const char* BoneName(uint32_t) const override
    {
        return nullptr;
    }
    const char* MorphName(uint32_t) const override
    {
        return nullptr;
    }
    uint32_t ClipCount() const override
    {
        return 1;
    }
    const char* ClipName(uint32_t clip) const override
    {
        return clip == 0 ? "sequence" : nullptr;
    }
    double ClipDuration(uint32_t clip) const override
    {
        return clip == 0 ? static_cast<double>(frameCount - 1) / framesPerSecond : -1.0;
    }

    bool Sample(uint32_t clip, double seconds, animation::FModelPose& output, String& error) const override
    {
        if (clip != 0 || !isfinite(seconds))
        {
            error.Assign("invalid OBJ sequence clip or time");
            return false;
        }
        animation::FModelPose candidate;
        if (!animation::InitializeModelPose(skeleton, candidate, error))
            return false;
        for (uint32_t i = 0; i < frameCount; ++i)
            candidate.morphWeights.At(i) = 0.0f;
        const double frame = fmax(0.0, fmin(static_cast<double>(frameCount - 1), seconds * framesPerSecond));
        const uint32_t left = static_cast<uint32_t>(floor(frame));
        const uint32_t right = left + 1 < frameCount ? left + 1 : left;
        const float weight = static_cast<float>(frame - left);
        candidate.morphWeights.At(left) = 1.0f - weight;
        candidate.morphWeights.At(right) += weight;
        output.localTransforms.MoveFrom(candidate.localTransforms);
        output.morphWeights.MoveFrom(candidate.morphWeights);
        error.Clear();
        return true;
    }

    bool Deform(const animation::FModelPose& pose, detail::ModelResource& output, String& error) const override
    {
        if (pose.localTransforms.Count() != 0 || pose.morphWeights.Count() != frameCount || output.vertices.Count() != vertexCount || output.indices.Count() != vertexCount)
        {
            error.Assign("OBJ sequence vertex or frame count mismatch");
            return false;
        }
        Array<detail::ModelVertex> candidate;
        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            if (output.indices.At(i) != i)
            {
                error.Assign("OBJ sequence triangle index order mismatch");
                return false;
            }
        }
        if (!candidate.AppendRange(output.vertices.Data(), vertexCount))
        {
            error.Assign("OBJ sequence snapshot allocation failed");
            return false;
        }
        for (uint32_t vertex = 0; vertex < vertexCount; ++vertex)
        {
            detail::ModelVertex& value = candidate.At(vertex);
            if (value.sourceIndex != frames.At(vertex).sourceIndex)
            {
                error.Assign("OBJ sequence source vertex order mismatch");
                return false;
            }
            double position[3]{};
            double normal[3]{};
            for (uint32_t frame = 0; frame < frameCount; ++frame)
            {
                const float weight = pose.morphWeights.At(frame);
                if (!isfinite(weight))
                {
                    error.Assign("OBJ sequence frame weight is not finite");
                    return false;
                }
                const auto& source = frames.At(frame * vertexCount + vertex);
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    position[axis] += static_cast<double>(source.position[axis]) * weight;
                    normal[axis] += static_cast<double>(source.normal[axis]) * weight;
                }
            }
            const double length = sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                value.position[axis] = static_cast<float>(position[axis]);
                value.normal[axis] = length > 0.0 ? static_cast<float>(normal[axis] / length) : 0.0f;
                if (!isfinite(value.position[axis]) || !isfinite(value.normal[axis]))
                {
                    error.Assign("OBJ sequence result exceeds float range");
                    return false;
                }
            }
        }
        output.vertices.MoveFrom(candidate);
        error.Clear();
        return true;
    }
};

/**
 * 連番で変えてはいけない頂点・面・画像座標の順序を検査する。
 */
bool SameTopology(const detail::ModelResource& base, const detail::ModelResource& frame)
{
    if (base.vertices.Count() != frame.vertices.Count() || base.indices.Count() != frame.indices.Count() || base.primitives.Count() != frame.primitives.Count())
        return false;
    for (uint32_t i = 0; i < base.indices.Count(); ++i)
    {
        if (base.indices.At(i) != frame.indices.At(i))
            return false;
    }
    for (uint32_t i = 0; i < base.vertices.Count(); ++i)
    {
        if (base.vertices.At(i).sourceIndex != frame.vertices.At(i).sourceIndex || memcmp(base.vertices.At(i).uv, frame.vertices.At(i).uv, sizeof(float) * 2) != 0)
            return false;
    }
    for (uint32_t i = 0; i < base.primitives.Count(); ++i)
    {
        if (base.primitives.At(i).firstIndex != frame.primitives.At(i).firstIndex || base.primitives.At(i).indexCount != frame.primitives.At(i).indexCount)
            return false;
    }
    return true;
}
}

FModelAnimationAsset* LoadObjSequence(const char* const* paths, uint32_t count, float fps, detail::ModelResource*& base, String& error)
{
    base = nullptr;
    if (!paths || count == 0 || count > 4096 || !isfinite(fps) || fps <= 0.0f)
    {
        error.Assign("invalid OBJ sequence paths, frame count, or fps");
        return nullptr;
    }
    AObjSequenceSource* source = nullptr;
    try
    {
        source = new AObjSequenceSource;
    }
    catch (...)
    {
        error.Assign("OBJ sequence source allocation failed");
        return nullptr;
    }
    source->frameCount = count;
    source->framesPerSecond = fps;
    bool success = true;
    for (uint32_t i = 0; i < count && success; ++i)
    {
        uint8_t* bytes = nullptr;
        uint32_t size = 0;
        detail::ModelResource* frame = nullptr;
        success = detail::ReadResourceFile(paths[i], 64u * 1024u * 1024u, bytes, size, error);
        if (success)
            frame = detail::CreateModelResource();
        if (success && !frame)
        {
            error.Assign("OBJ sequence model allocation failed");
            success = false;
        }
        if (success)
            success = detail::LoadObjPayload(bytes, size, *frame, error);
        Deallocate(bytes);
        if (success && !base)
        {
            base = frame;
            frame = nullptr;
            source->vertexCount = base->vertices.Count();
            const uint64_t total = static_cast<uint64_t>(source->vertexCount) * count;
            if (total > (256u * 1024u * 1024u) / sizeof(detail::ModelVertex) || !source->frames.Reserve(static_cast<uint32_t>(total)))
            {
                error.Assign("OBJ sequence exceeds the 256 MiB vertex budget");
                success = false;
            }
        }
        const auto* current = frame ? frame : base;
        if (success && !SameTopology(*base, *current))
        {
            error.Assign("OBJ sequence topology or texture coordinate order differs");
            success = false;
        }
        if (success && (!source->frames.AppendRange(current->vertices.Data(), current->vertices.Count()) || !source->skeleton.restMorphWeights.Append(i == 0 ? 1.0f : 0.0f)))
        {
            error.Assign("OBJ sequence frame allocation failed");
            success = false;
        }
        if (frame)
            Release(&frame->reference);
    }
    FModelAnimationAsset* asset = nullptr;
    if (success)
        asset = CreateModelAnimationAsset(source, error);
    else
        delete source;
    if (!asset && base)
    {
        Release(&base->reference);
        base = nullptr;
    }
    return asset;
}
}
