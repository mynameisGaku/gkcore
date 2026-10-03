#include "../include/gkcore.h"
#include "../src/effects/Effects.h"
#include "../src/effects/Shaders.h"
#include "../src/foundation/String.h"

#include <math.h>

namespace gk::tests {
namespace {
bool Fail(String& failure, const char* message) { failure.Assign(message); return false; }
}

bool EffectsContract(String& failure) {
    gk::effects::Reset();
    gk::effects::Settings value = gk::effects::Current();
    if (!value.bloomEnabled || value.bloomIntensity != 0.15f || value.exposure != 1.0f ||
        !value.toneMappingEnabled || value.layer != DrawLayer::Scene)
        return Fail(failure, "effect defaults are incorrect");
    if (!gk::effects::SetBloomEnabled(false) || !gk::effects::SetBloomIntensity(0.8f) ||
        !gk::effects::SetExposure(1.25f) || !gk::effects::SetToneMappingEnabled(false))
        return Fail(failure, "valid effect settings were rejected");
    value = gk::effects::Current();
    if (value.bloomEnabled || value.bloomIntensity != 0.8f || value.exposure != 1.25f || value.toneMappingEnabled)
        return Fail(failure, "effect settings did not persist");
    if (gk::effects::SetBloomIntensity(-0.01f) || gk::effects::SetBloomIntensity(4.01f) || gk::effects::SetBloomIntensity(NAN))
        return Fail(failure, "invalid bloom intensity was accepted");
    if (gk::effects::SetExposure(0.0f) || gk::effects::SetExposure(16.01f) || gk::effects::SetExposure(INFINITY))
        return Fail(failure, "invalid exposure was accepted");
    value = gk::effects::Current();
    if (value.bloomIntensity != 0.8f || value.exposure != 1.25f)
        return Fail(failure, "rejected effect settings changed active values");
    if (!gk::effects::SetLayer(DrawLayer::UI) || gk::effects::Current().layer != DrawLayer::UI ||
        !gk::effects::SetLayer(DrawLayer::Scene))
        return Fail(failure, "scene/UI layer switching failed");

    ShaderBindings shaders;
    ShaderSnapshot snapshot;
    ShaderHandle first(9);
    if (shaders.ActiveHandle().IsValid() || shaders.RegisterShader(ShaderHandle()) ||
        shaders.SetActiveShader(first))
        return Fail(failure, "invalid shader handle was accepted");
    if (!shaders.RegisterShader(first) || !shaders.SetActiveShader(first) ||
        !shaders.SetConstant(first, 2, Float4{0.5f, 1.0f, 0.0f, 1.0f}))
        return Fail(failure, "loaded shader or finite constant was rejected");
    if (!shaders.Snapshot(snapshot) || snapshot.shaderHandle != first || snapshot.constants.Count() != 1 ||
        snapshot.constants.At(0).registerIndex != 2)
        return Fail(failure, "shader binding snapshot failed");
    if (!shaders.SetConstant(first, 2, Float4{0.25f, 0.5f, 0.75f, 1.0f}) ||
        snapshot.constants.At(0).value.x != 0.5f)
        return Fail(failure, "queued constants changed after later updates");
    if (shaders.SetConstant(first, 64, Float4{0,0,0,0}) ||
        shaders.SetConstant(first, 3, Float4{0,0,INFINITY,0}) ||
        shaders.SetConstant(ShaderHandle(10), 0, Float4{1,1,1,1}))
        return Fail(failure, "invalid shader constant was accepted");
    if (!shaders.DeleteShader(first) || shaders.ActiveHandle().IsValid() || shaders.HasShader(first))
        return Fail(failure, "shader deletion did not reset its active binding");
    if (!shaders.Snapshot(snapshot) || snapshot.shaderHandle.IsValid() || snapshot.constants.Count() != 0)
        return Fail(failure, "reused snapshots retained a deleted shader or its constants");
    if (shaders.DeleteShader(first) || shaders.SetConstant(first, 0, Float4{1,1,1,1}))
        return Fail(failure, "deleted shader handle remained valid");

    ShaderBindings independent;
    ShaderHandle a(1), b(2);
    if (!independent.RegisterShader(a) || !independent.RegisterShader(b) ||
        !independent.SetConstant(b, 0, Float4{2,2,2,2}) || !independent.SetActiveShader(a) ||
        !independent.SetActiveShader(ShaderHandle()) || !independent.SetActiveShader(b) ||
        !independent.Snapshot(snapshot) || snapshot.constants.Count() != 1 ||
        snapshot.constants.At(0).value.x != 2.0f)
        return Fail(failure, "switching through built-in shader lost another shader constants");
    if (!independent.SetActiveShader(a) || !independent.DeleteShader(a) ||
        !independent.SetActiveShader(b) || !independent.Snapshot(snapshot) || snapshot.constants.Count() != 1)
        return Fail(failure, "deleting one shader cleared another shader constants");
    return true;
}
} // namespace gk::tests
