#include "Draw3D.h"

#include "../core/Context.h"
#include "../core/Frame.h"
#include "../resources/Resources.h"
#include "../model/animation/ModelSnapshot.h"

#include <float.h>
#include <math.h>

/**
 * 3D描画とmodel instance変換を扱う公開API実装。
 */
namespace gk
{
namespace
{
/**
 * handleが有効でmodel resourceが存在するか確認する。
 */
bool Valid(ModelHandle handle)
{
    return handle.IsValid() && detail::FindModel(handle) != nullptr;
}

}

int SetCamera(Vec3 position, Vec3 target)
{
    if (!detail::IsFinite(position) || !detail::IsFinite(target))
        return detail::SetError("camera values must be finite");
    const double dx = static_cast<double>(target.x) - position.x;
    const double dy = static_cast<double>(target.y) - position.y;
    const double dz = static_cast<double>(target.z) - position.z;
    const double distance = sqrt(dx * dx + dy * dy + dz * dz);
    if (!(distance > 0.0) || !isfinite(distance))
        return detail::SetError("camera position and target must differ");
    if (fabs(dx) > FLT_MAX || fabs(dy) > FLT_MAX || fabs(dz) > FLT_MAX)
        return detail::SetError("camera direction exceeds float range");
    if (sqrt(dx * dx + dz * dz) <= distance * 1e-6)
        return detail::SetError("camera direction cannot be parallel to the Y-up axis");
    detail::Context& context = detail::GetContext();
    context.cameraPosition = position;
    context.cameraTarget = target;
    detail::ClearError();
    return 0;
}

int DrawTriangle3D(Vec3 a, Vec3 b, Vec3 c, uint32_t color, bool filled)
{
    if (!detail::IsFinite(a) || !detail::IsFinite(b) || !detail::IsFinite(c))
        return detail::SetError("triangle vertices must be finite");
    detail::DrawPacket packet{};
    packet.kind = detail::DrawKind::Triangle3D;
    packet.flags = filled ? static_cast<uint8_t>(detail::DrawFilled) : 0u;
    packet.points[0] = a;
    packet.points[1] = b;
    packet.points[2] = c;
    packet.color = color;
    return detail::QueueDraw(packet);
}

int DrawModel(ModelHandle model)
{
    if (!Valid(model))
        return detail::SetError("invalid model handle");
    detail::ModelTransform* transform = detail::FindModelTransform(model);
    if (!transform)
        return detail::SetError("model transform state is unavailable");
    detail::DrawPacket packet{};
    packet.kind = detail::DrawKind::Model;
    packet.resource = model.value;
    String error;
    packet.model = model::EvaluateModelSnapshot(*detail::FindModel(model), transform->playback, error);
    if (!packet.model)
        return detail::SetError(error.CStr());
    packet.color = 0x00ffffffu;
    packet.modelPosition = transform->position;
    packet.modelRotation = transform->rotation;
    packet.modelScale = transform->scale;
    const int result = detail::QueueDraw(packet);
    Release(&packet.model->reference);
    return result;
}

int SetModelPosition(ModelHandle model, Vec3 position)
{
    if (!Valid(model))
        return detail::SetError("invalid model handle");
    if (!detail::IsFinite(position))
        return detail::SetError("model position must be finite");
    detail::ModelTransform* transform = detail::FindModelTransform(model);
    if (!transform)
        return detail::SetError("model transform state is unavailable");
    transform->position = position;
    detail::ClearError();
    return 0;
}

int SetModelRotation(ModelHandle model, Vec3 rotationRadians)
{
    if (!Valid(model))
        return detail::SetError("invalid model handle");
    if (!detail::IsFinite(rotationRadians))
        return detail::SetError("model rotation must be finite");
    detail::ModelTransform* transform = detail::FindModelTransform(model);
    if (!transform)
        return detail::SetError("model transform state is unavailable");
    transform->rotation = rotationRadians;
    detail::ClearError();
    return 0;
}

int SetModelScale(ModelHandle model, Vec3 scale)
{
    if (!Valid(model))
        return detail::SetError("invalid model handle");
    if (!detail::IsFinite(scale) || scale.x == 0.0f || scale.y == 0.0f || scale.z == 0.0f)
        return detail::SetError("model scale must be finite and nonzero on every axis");
    detail::ModelTransform* transform = detail::FindModelTransform(model);
    if (!transform)
        return detail::SetError("model transform state is unavailable");
    transform->scale = scale;
    detail::ClearError();
    return 0;
}

} // gk namespace終端
