#pragma once

#include <stdint.h>
#include <gkcore/Handle.h>

#if defined(_WIN32) && defined(GKCORE_SHARED)
#if defined(GKCORE_BUILDING_LIBRARY)
#define GKCORE_API __declspec(dllexport)
#else
#define GKCORE_API __declspec(dllimport)
#endif
#else
#define GKCORE_API
#endif

#ifdef LoadImage
// Windows.hの同名マクロが公開関数名を変えないようにする。
#undef LoadImage
#endif

/**
 * Simple global API and value types for application window and draw state.
 */
namespace gk
{

/**
 * A three-component value for positions, rotations, directions, and scales.
 */
struct Vec3
{
    float x, y, z;
};

/**
 * Four floating-point components, used for shader constants.
 */
struct Float4
{
    float x, y, z, w;
};

/**
 * Escape, arrows, common editing keys, Shift/Control, digits, and Latin letters.
 */
enum class Key : uint8_t
{
    Escape = 1,
    ArrowLeft,
    ArrowUp,
    ArrowRight,
    ArrowDown,
    Space,
    Enter,
    Tab,
    Backspace,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Shift,
    Control
};

/**
 * Mouse buttons supported by the input query API.
 */
enum class MouseButton : uint8_t
{
    Left = 0,
    Right,
    Middle
};

/**
 * Selects whether queued draws receive scene post-processing.
 */
enum class DrawLayer : uint8_t
{
    Scene = 0,
    UI = 1
};

/**
 * Initializes the window and renderer. Returns 0 on success and -1 on failure.
 */
GKCORE_API int Init();

/**
 * Shuts down the renderer and invalidates all resource handles.
 */
GKCORE_API void Shutdown();

/**
 * Configures the window before Init. Invalid dimensions return -1.
 */
GKCORE_API int SetWindowSize(uint32_t width, uint32_t height);

/**
 * Processes platform events. Returns true while the application should run.
 */
GKCORE_API bool ProcessEvents();

/**
 * Opens a frame for drawing. Returns 0 on success and -1 on failure.
 */
GKCORE_API int BeginFrame();

/**
 * Submits and presents the open frame. Returns 0 on success and -1 on failure.
 */
GKCORE_API int Present();

/**
 * Packs clamped 8-bit RGB values as 0xRRGGBB.
 */
GKCORE_API uint32_t ColorRGB(int red, int green, int blue);

/**
 * Returns whether the selected framework key is currently down.
 * Query from the application thread; unfocused windows report false.
 */
GKCORE_API bool IsKeyDown(Key key);

/**
 * Returns whether a mouse button is down while the application window is focused.
 * Query from the application thread.
 * Unsupported backend input reports false and sets a diagnostic.
 */
GKCORE_API bool IsMouseButtonDown(MouseButton button);

/**
 * Reads client-area mouse coordinates while the application window is focused.
 * Query from the application thread.
 * Returns false and writes zero coordinates when the window is unfocused,
 * uninitialized, or the backend cannot provide pointer coordinates.
 */
GKCORE_API bool GetMousePosition(int32_t& x, int32_t& y);

/**
 * Loads an image and returns an invalid handle on failure.
 */
GKCORE_API ImageHandle LoadImage(const char* utf8Path);

/**
 * Queues an image at a top-left pixel coordinate.
 */
GKCORE_API int DrawImage(ImageHandle image, float x, float y, bool alphaBlend = true);

/**
 * Queues a centered image; angle is in radians and scale must be positive.
 */
GKCORE_API int DrawImageRotated(ImageHandle image, float centerX, float centerY, float scale, float angleRadians, bool alphaBlend = true);

/**
 * Releases an image handle; queued draws retain its pixels through Present.
 */
GKCORE_API int DeleteImage(ImageHandle image);

/**
 * Loads a static OBJ, GLB 2.0, or FBX model from a UTF-8 path. Returns an invalid handle on failure.
 */
GKCORE_API ModelHandle LoadModel(const char* utf8Path);

/**
 * Queues a model draw with the transform values set at this call.
 */
GKCORE_API int DrawModel(ModelHandle model);

/**
 * Sets a model's world-space position for subsequent DrawModel calls.
 */
GKCORE_API int SetModelPosition(ModelHandle model, Vec3 position);

/**
 * Sets XYZ Euler rotation in radians for subsequent DrawModel calls.
 */
GKCORE_API int SetModelRotation(ModelHandle model, Vec3 rotationRadians);

/**
 * Sets nonzero scale on all model axes for subsequent DrawModel calls.
 */
GKCORE_API int SetModelScale(ModelHandle model, Vec3 scale);

/**
 * Releases a model handle; queued draws retain its geometry through Present.
 */
GKCORE_API int DeleteModel(ModelHandle model);

/**
 * Sets a finite camera position and target using a Y-up coordinate system.
 */
GKCORE_API int SetCamera(Vec3 position, Vec3 target);

/**
 * Queues a screen-space rectangle in the selected draw layer. With filled=false,
 * queues a one-pixel stroke just inside the rectangle bounds.
 */
GKCORE_API int DrawRect(float x, float y, float width, float height, uint32_t color, bool filled = true);

/**
 * Queues an inward screen-space stroke. Thickness must be finite and positive;
 * a thickness at least half the smaller dimension covers the whole rectangle.
 */
GKCORE_API int DrawRectOutline(float x, float y, float width, float height, uint32_t color, float thickness = 1.0f);

/**
 * Queues UTF-8 text using the platform's default system font. The default
 * pixel size is 24; accepted sizes are 1 through 256. Repeated strings with
 * the same color and size share a bounded main-thread image cache. Text uses
 * the current draw layer and remains alive through Present.
 */
GKCORE_API int DrawString(float x, float y, const char* utf8Text, uint32_t color, uint32_t pixelSize = 24);

/**
 * Queues a world-space triangle for 3D drawing.
 */
GKCORE_API int DrawTriangle3D(Vec3 a, Vec3 b, Vec3 c, uint32_t color, bool filled = true);

/**
 * Returns a pointer to the latest diagnostic string.
 */
GKCORE_API const char* GetLastErrorMessage();

/**
 * Enables or disables bloom for subsequent frames.
 */
GKCORE_API int SetBloomEnabled(bool enabled);

/**
 * Sets bloom strength to a finite value in [0, 4].
 */
GKCORE_API int SetBloomIntensity(float intensity);

/**
 * Sets exposure to a finite value in (0, 16].
 */
GKCORE_API int SetExposure(float exposure);

/**
 * Sets scene saturation to a finite factor in [0, 2]; 1 preserves saturation.
 */
GKCORE_API int SetSaturation(float factor);

/**
 * Sets scene contrast to a finite factor in [0, 2]; 1 preserves contrast.
 */
GKCORE_API int SetContrast(float factor);

/**
 * Enables or disables FXAA for subsequent frames; it is enabled by default.
 */
GKCORE_API int SetFxaaEnabled(bool enabled);

/**
 * Enables or disables tone mapping for subsequent frames.
 */
GKCORE_API int SetToneMappingEnabled(bool enabled);

/**
 * Selects whether subsequent draw commands belong to scene or UI.
 */
GKCORE_API int SetDrawLayer(DrawLayer layer);

/**
 * Sets built-in model ambient light to a finite value in [0, 4]. Defaults to 0.2.
 * The value is sampled by BeginFrame and restored to its default by Shutdown.
 */
GKCORE_API int SetAmbientLight(float intensity);

/**
 * Sets built-in model directional light using a finite nonzero world-space travel direction,
 * normalized when stored, and intensity in [0, 16]. Intensity defaults to 3. BeginFrame
 * samples the values and Shutdown restores their defaults.
 */
GKCORE_API int SetDirectionalLight(Vec3 direction, float intensity = 3.0f);

/**
 * Loads a compiled pixel shader and returns an invalid handle on failure.
 */
GKCORE_API ShaderHandle LoadPixelShader(const char* compiledPath);

/**
 * Selects a custom shader; an invalid handle selects the built-in shader.
 */
GKCORE_API int SetPixelShader(ShaderHandle shader);

/**
 * Selects a loaded pixel shader for the post-process pass; an invalid handle disables it.
 * Requires Init. The selection is copied at BeginFrame, and a stale handle leaves it unchanged.
 */
GKCORE_API int SetPostEffectShader(ShaderHandle shader);

/**
 * Releases a shader handle; fails while an open frame uses it, so call after Present.
 */
GKCORE_API int DeleteShader(ShaderHandle shader);

/**
 * Sets a finite four-float value in a custom shader constant slot.
 */
GKCORE_API int SetShaderFloat4(ShaderHandle shader, uint32_t slot, Float4 value);

}
