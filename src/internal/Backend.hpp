#pragma once

#include "../foundation/Array.h"
#include "../foundation/String.h"
#include "../foundation/RefCount.h"
#include "../resources/Resources.h"
#include "../effects/Shaders.h"
#include <gkcore.h>

/**
 * Internal packets and platform boundary shared by the framework core and renderer.
 */
namespace gk::detail {

/**
 * Describes the primitive operation represented by a draw packet.
 */
enum class DrawKind : uint8_t { Rect, Image, Triangle3D, Model };

/**
 * Bit values stored in DrawPacket::flags.
 */
enum DrawFlags : uint8_t {
    DrawFilled = 1u,
    DrawAlphaBlend = 2u,
    DrawImageCentered = 4u
};

/**
 * A POD draw command. Transform, camera, effect, and shader state are captured
 * when the API queues the command. Resource pointers own intrusive references.
 */
struct DrawPacket {
    DrawKind kind;
    uint8_t layer;
    uint8_t flags;
    uint8_t shaderConstantCount;
    uint8_t reserved;
    uint64_t sequence;
    uint32_t resource;
    ShaderHandle shader;
    ImageResource* image;
    ModelResource* model;
    Vec3 points[3];
    Vec3 cameraPosition;
    Vec3 cameraTarget;
    Vec3 modelPosition;
    Vec3 modelRotation;
    Vec3 modelScale;
    float rotation;
    float scaleX;
    float scaleY;
    float rect[4];
    uint32_t color;
    gk::ShaderConstant shaderConstants[64];
};

/**
 * One frame of ordered draw packets and the effects sampled at BeginFrame.
 */
struct FramePacket {
    uint32_t width;
    uint32_t height;
    Vec3 cameraPosition;
    Vec3 cameraTarget;
    bool bloomEnabled;
    float bloomIntensity;
    float exposure;
    bool toneMappingEnabled;
    float saturation = 1.0f;
    float contrast = 1.0f;
    bool fxaaEnabled = true;
    ShaderHandle postEffectShader{};
    uint32_t postEffectConstantCount = 0;
    ShaderConstant postEffectConstants[64]{};
    Array<DrawPacket> draws;
};

/**
 * Platform boundary for event processing and native rendering.
 */
class Backend {
public:
    /**
     * Provides polymorphic cleanup for platform and test renderer adapters.
     */
    virtual ~Backend() {}
    /**
     * Creates the native window and renderer state needed for drawing.
     */
    virtual bool Initialize(uint32_t width, uint32_t height, uint32_t colorDepth, String& error) = 0;
    /**
     * Releases adapter-owned window and renderer state after queued work is safe.
     */
    virtual void Shutdown() = 0;
    /**
     * Returns 0 to continue, -1 on normal close, or below -1 on failure.
     */
    virtual int ProcessMessage() = 0;
    /**
     * Queries a supported key using its mapped native key value.
     */
    virtual bool IsKeyDown(uint32_t platformKeyCode) const = 0;
    /**
     * Reports whether keyboard and mouse input currently belongs to the window.
     */
    virtual bool HasInputFocus() const { return false; }
    /**
     * Reports whether the platform adapter implements mouse button and position queries.
     */
    virtual bool SupportsMouseInput() const { return false; }
    /**
     * Reads a platform mouse button only while the owned window is focused.
     */
    virtual bool IsMouseButtonDown(uint32_t) const { return false; }
    /**
     * Reads client-relative pointer coordinates; returns false when unavailable.
     */
    virtual bool GetMousePosition(int32_t&, int32_t&) const { return false; }
    /**
     * Renders the captured frame and reports backend failures through error.
     */
    virtual bool Present(const FramePacket& frame, String& error) = 0;
    /**
     * Rasterizes UTF-8 text into a caller-owned RGBA image. A renderer without
     * a system-font adapter reports failure instead of silently dropping text.
     */
    virtual ImageResource* RasterizeText(const char*, uint32_t, uint32_t, String& error) {
        error.Assign("text rasterization is unsupported by this renderer");
        return nullptr;
    }
    /**
     * Returns current client dimensions when the platform backend tracks resizing.
     */
    virtual bool GetClientSize(uint32_t&, uint32_t&) const { return false; }
    /**
     * Loads a backend-compatible pixel shader or reports why it is unavailable.
     */
    virtual ShaderHandle LoadPixelShader(const char* path, String& error) = 0;
    /**
     * Releases a backend shader handle and its dependent state.
     */
    virtual bool ReleasePixelShader(ShaderHandle shader, String& error) = 0;

};

/**
 * Creates the platform renderer; the caller owns the returned pointer.
 */
Backend* CreateNativeBackend();

#ifdef GKCORE_TESTING
/**
 * Installs a development test backend and resets all API state.
 */
void SetBackendForTesting(Backend* backend);
#endif

} // namespace gk::detail
