#include <gkcore.h>
#include "core/Context.h"
#include "internal/Backend.hpp"
#include "foundation/Memory.h"
#include "image/Image.h"
#include "model/ModelLoader.h"
#include "text/TextCache.h"
#include "effects/Effects.h"
#include "render/ModelDrawPlan.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace gk::tests
{
bool FoundationContracts();
bool EffectsContract(gk::String& failure);
bool ResourceContract(gk::String& failure);
bool ShaderArtifactContract(gk::String& failure);
bool PostProcessMathContract(gk::String& failure);
}

namespace
{
#define CHECK(condition)                                                                    \
    do                                                                                      \
    {                                                                                       \
        if (!(condition))                                                                   \
        {                                                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                   \
        }                                                                                   \
    } while (0)

struct FKeyMappingExpectation
{
    // 確認対象の公開キー。
    gk::Key key;
    // Windows virtual-keyの独立した期待値。
    uint32_t virtualKey;
};

// 公開キー全体を、MapKey実装に依存しない固定値で照合する。
constexpr FKeyMappingExpectation kFKeyMappingExpectations[] = {
    { gk::Key::Escape, 0x1b }, { gk::Key::ArrowLeft, 0x25 }, { gk::Key::ArrowUp, 0x26 }, { gk::Key::ArrowRight, 0x27 }, { gk::Key::ArrowDown, 0x28 }, { gk::Key::Space, 0x20 }, { gk::Key::Enter, 0x0d }, { gk::Key::Tab, 0x09 }, { gk::Key::Backspace, 0x08 }, { gk::Key::Digit0, 0x30 }, { gk::Key::Digit1, 0x31 }, { gk::Key::Digit2, 0x32 }, { gk::Key::Digit3, 0x33 }, { gk::Key::Digit4, 0x34 }, { gk::Key::Digit5, 0x35 }, { gk::Key::Digit6, 0x36 }, { gk::Key::Digit7, 0x37 }, { gk::Key::Digit8, 0x38 }, { gk::Key::Digit9, 0x39 }, { gk::Key::A, 0x41 }, { gk::Key::B, 0x42 }, { gk::Key::C, 0x43 }, { gk::Key::D, 0x44 }, { gk::Key::E, 0x45 }, { gk::Key::F, 0x46 }, { gk::Key::G, 0x47 }, { gk::Key::H, 0x48 }, { gk::Key::I, 0x49 }, { gk::Key::J, 0x4a }, { gk::Key::K, 0x4b }, { gk::Key::L, 0x4c }, { gk::Key::M, 0x4d }, { gk::Key::N, 0x4e }, { gk::Key::O, 0x4f }, { gk::Key::P, 0x50 }, { gk::Key::Q, 0x51 }, { gk::Key::R, 0x52 }, { gk::Key::S, 0x53 }, { gk::Key::T, 0x54 }, { gk::Key::U, 0x55 }, { gk::Key::V, 0x56 }, { gk::Key::W, 0x57 }, { gk::Key::X, 0x58 }, { gk::Key::Y, 0x59 }, { gk::Key::Z, 0x5a }, { gk::Key::Shift, 0x10 }, { gk::Key::Control, 0x11 },
};
constexpr uint32_t kFKeyMappingExpectationCount = sizeof(kFKeyMappingExpectations) / sizeof(kFKeyMappingExpectations[0]);

struct CaptureState
{
    bool failInitialize = false;
    bool initialized = false;
    bool shutdownCalled = false;
    bool destroyed = false;
    uint32_t initializedWidth = 0;
    uint32_t initializedHeight = 0;
    int eventResult = 0;
    bool hasFocus = true;
    uint32_t downKey = 0x1b;
    uint32_t lastKey = 0;
    // 押下中の状態とは別に保持する直近イベントのキー。
    uint32_t pressedKey = 0xffffffffu;
    // 押下問い合わせが変換した仮想キー。
    uint32_t lastPressedKey = 0;
    uint32_t downMouseButton = 1;
    uint32_t lastMouseButton = 0;
    int32_t mouseX = 12;
    int32_t mouseY = 34;
    uint32_t shaderLoads = 0;
    uint32_t shaderReleases = 0;
    bool uniqueNativeShaderHandles = false;
    uint32_t nextNativeShaderHandle = 1;
    bool failShaderRelease = false;
    uint32_t clientWidth = 0;
    uint32_t clientHeight = 0;
    uint32_t presentedWidth = 0;
    uint32_t drawCount = 0;
    float cameraX[4]{};
    uint32_t shaderIds[4]{};
    uint32_t shaderConstantCounts[4]{};
    float firstShaderConstantX[4]{};
    uint8_t layers[4]{};
    gk::detail::DrawKind kinds[4]{};
    uint8_t drawFlags[4]{};
    float rectOutlineThickness[4]{};
    uint32_t drawColors[4]{};
    bool queuedImageValid = false;
    uint8_t queuedImageRed = 0;
    bool queuedImageCentered = false;
    bool queuedModelValid = false;
    float queuedModelPositions[2]{};
    bool baseColorGlbPlanValid = false;
    uint32_t baseColorGlbModelReferences = 0;
    uint32_t baseColorGlbPrimitiveCount = 0;
    uint32_t baseColorGlbMaterialCount = 0;
    uint32_t baseColorGlbTextureCount = 0;
    uint32_t baseColorGlbTextureWidth = 0;
    uint32_t baseColorGlbTextureHeight = 0;
    uint32_t baseColorGlbFirstIndices[2]{};
    uint32_t baseColorGlbIndexCounts[2]{};
    int32_t baseColorGlbMaterialIndices[2]{};
    int32_t baseColorGlbTextureIndices[2]{};
    float baseColorGlbFactors[2][4]{};
    bool baseColorFbxPlanValid = false;
    uint32_t baseColorFbxModelReferences = 0;
    uint32_t baseColorFbxVertexCount = 0;
    uint32_t baseColorFbxIndexCount = 0;
    uint32_t baseColorFbxPrimitiveCount = 0;
    uint32_t baseColorFbxMaterialCount = 0;
    uint32_t baseColorFbxTextureCount = 0;
    uint32_t baseColorFbxTextureReferences = 0;
    uint32_t baseColorFbxTextureWidth = 0;
    uint32_t baseColorFbxTextureHeight = 0;
    uint32_t baseColorFbxFirstIndices[2]{};
    uint32_t baseColorFbxIndexCounts[2]{};
    int32_t baseColorFbxMaterialIndices[2]{};
    int32_t baseColorFbxTextureIndices[2]{};
    uint32_t postFrameCount = 0;
    gk::ShaderHandle postShaders[8]{};
    uint32_t postConstantCounts[8]{};
    uint32_t postSlots[8][4]{};
    float postValues[8][4]{};
    uint32_t pixelShaderForPostFrame[8]{};
    uint32_t textRasterizations = 0;
    uint32_t textImageWidth = 2;
    uint32_t textImageHeight = 2;
    bool failTextRasterization = false;
    uint32_t rasterizedTextSize = 0;
    uint32_t rasterizedTextColor = 0;
    char rasterizedText[32]{};
    bool textPacketValid = false;
    uint32_t textImageReferences = 0;
    float textX = 0.0f;
    float textY = 0.0f;
    uint32_t textPackets = 0;
    const gk::detail::ImageResource* textImages[4]{};
    bool bloomEnabled = true;
    float exposure = 1.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    bool fxaaEnabled = true;
    uint32_t lightingFrameCount = 0;
    float ambientLight[8]{};
    float directionalDirection[8][3]{};
    float directionalIntensity[8]{};
};

class CaptureBackend final : public gk::detail::Backend
{
  public:
    explicit CaptureBackend(CaptureState& state) : state_(state)
    {
    }
    ~CaptureBackend() override
    {
        state_.destroyed = true;
    }
    bool Initialize(uint32_t width, uint32_t height, uint32_t, gk::String& error) override
    {
        if (state_.failInitialize)
        {
            error.Assign("expected initialization failure");
            return false;
        }
        state_.initializedWidth = width;
        state_.initializedHeight = height;
        state_.initialized = true;
        return true;
    }
    void Shutdown() override
    {
        state_.initialized = false;
        state_.shutdownCalled = true;
    }
    int ProcessMessage() override
    {
        state_.pressedKey = 0xffffffffu;
        return state_.eventResult;
    }
    bool HasInputFocus() const override
    {
        return state_.hasFocus;
    }
    bool SupportsMouseInput() const override
    {
        return true;
    }
    bool IsKeyDown(uint32_t key) const override
    {
        state_.lastKey = key;
        return key == state_.downKey;
    }
    /**
     * 現在の押下中状態に依存せず、記録された短い押下を返す。
     */
    bool WasKeyPressed(uint32_t key) const override
    {
        state_.lastPressedKey = key;
        return key == state_.pressedKey;
    }
    bool IsMouseButtonDown(uint32_t button) const override
    {
        state_.lastMouseButton = button;
        return button == state_.downMouseButton;
    }
    bool GetMousePosition(int32_t& x, int32_t& y) const override
    {
        x = state_.mouseX;
        y = state_.mouseY;
        return true;
    }
    gk::detail::ImageResource* RasterizeText(const char* text, uint32_t pixelSize, uint32_t color, gk::String& error) override
    {
        ++state_.textRasterizations;
        state_.rasterizedTextSize = pixelSize;
        state_.rasterizedTextColor = color;
        uint32_t i = 0;
        while (text && text[i] && i + 1 < sizeof(state_.rasterizedText))
        {
            state_.rasterizedText[i] = text[i];
            ++i;
        }
        state_.rasterizedText[i] = '\0';
        if (state_.failTextRasterization)
        {
            error.Assign("fake font rasterizer failure");
            return nullptr;
        }
        gk::detail::ImageResource* image = gk::detail::CreateImageResource();
        if (!image)
        {
            error.Assign("fake image allocation failure");
            return nullptr;
        }
        image->width = state_.textImageWidth;
        image->height = state_.textImageHeight;
        const uint64_t bytes = static_cast<uint64_t>(image->width) * image->height * 4;
        for (uint64_t byte = 0; byte < bytes; ++byte)
        {
            const uint8_t value = byte % 4 == 3 ? 255 : 255;
            if (!image->rgba.Append(value))
            {
                gk::Release(&image->reference);
                error.Assign("fake image allocation failure");
                return nullptr;
            }
        }
        return image;
    }
    bool GetClientSize(uint32_t& width, uint32_t& height) const override
    {
        width = state_.clientWidth;
        height = state_.clientHeight;
        return width != 0 && height != 0;
    }
    bool Present(const gk::detail::FramePacket& frame, gk::String&) override
    {
        state_.drawCount = frame.draws.Count();
        state_.presentedWidth = frame.width;
        state_.bloomEnabled = frame.bloomEnabled;
        state_.exposure = frame.exposure;
        state_.saturation = frame.saturation;
        state_.contrast = frame.contrast;
        state_.fxaaEnabled = frame.fxaaEnabled;
        if (state_.lightingFrameCount < 8)
        {
            const uint32_t index = state_.lightingFrameCount++;
            state_.ambientLight[index] = frame.lighting.ambientIntensity;
            state_.directionalDirection[index][0] = frame.lighting.direction.x;
            state_.directionalDirection[index][1] = frame.lighting.direction.y;
            state_.directionalDirection[index][2] = frame.lighting.direction.z;
            state_.directionalIntensity[index] = frame.lighting.directionalIntensity;
        }
        const uint32_t postFrame = state_.postFrameCount++;
        if (postFrame < 8)
        {
            state_.postShaders[postFrame] = frame.postEffectShader;
            state_.postConstantCounts[postFrame] = frame.postEffectConstantCount;
            for (uint32_t i = 0; i < frame.postEffectConstantCount && i < 4; ++i)
            {
                state_.postSlots[postFrame][i] = frame.postEffectConstants[i].registerIndex;
                state_.postValues[postFrame][i] = frame.postEffectConstants[i].value.x;
            }
            if (frame.draws.Count())
                state_.pixelShaderForPostFrame[postFrame] = frame.draws.At(0).shader.value;
        }
        for (uint32_t i = 0; i < frame.draws.Count() && i < 4; ++i)
        {
            const gk::detail::DrawPacket& packet = frame.draws.At(i);
            state_.cameraX[i] = packet.cameraPosition.x;
            state_.shaderIds[i] = packet.shader.value;
            state_.shaderConstantCounts[i] = packet.shaderConstantCount;
            state_.layers[i] = packet.layer;
            state_.kinds[i] = packet.kind;
            state_.drawFlags[i] = packet.flags;
            state_.rectOutlineThickness[i] = packet.rectOutlineThickness;
            state_.drawColors[i] = packet.color;
            if (state_.shaderConstantCounts[i])
                state_.firstShaderConstantX[i] = packet.shaderConstants[0].value.x;
        }
        if (frame.draws.Count() && frame.draws.At(0).image)
        {
            const gk::detail::ImageResource& image = *frame.draws.At(0).image;
            state_.queuedImageValid = image.width == 1 && image.height == 1 && image.rgba.Count() == 4;
            if (state_.queuedImageValid)
                state_.queuedImageRed = image.rgba.At(0);
        }
        if (frame.draws.Count() > 1)
            state_.queuedImageCentered = (frame.draws.At(1).flags & gk::detail::DrawImageCentered) != 0;
        for (uint32_t i = 0; i < frame.draws.Count() && i < 4; ++i)
        {
            const gk::detail::DrawPacket& packet = frame.draws.At(i);
            if (packet.kind != gk::detail::DrawKind::Image || !packet.image || packet.image->width != 2 || packet.image->height != 2 || packet.image->rgba.Count() != 16)
                continue;
            const uint32_t textIndex = state_.textPackets++;
            if (textIndex < 4)
                state_.textImages[textIndex] = packet.image;
            state_.textPacketValid = (packet.flags & gk::detail::DrawAlphaBlend) != 0 && packet.layer == static_cast<uint8_t>(gk::DrawLayer::UI);
            state_.textImageReferences = packet.image->reference.references;
            state_.textX = packet.rect[0];
            state_.textY = packet.rect[1];
        }
        uint32_t modelDraw = 0;
        for (uint32_t i = 0; i < frame.draws.Count() && modelDraw < 2; ++i)
        {
            if (frame.draws.At(i).kind != gk::detail::DrawKind::Model)
                continue;
            const gk::detail::DrawPacket& draw = frame.draws.At(i);
            state_.queuedModelValid = draw.model && draw.model->indices.Count() == 3;
            state_.queuedModelPositions[modelDraw++] = draw.modelPosition.x;
            if (draw.model && draw.model->primitives.Count() == 2)
            {
                gk::render::ModelDrawPlan plan;
                gk::String planError;
                if (gk::render::BuildModelDrawPlan(*draw.model, plan, planError) && plan.parts.Count() == 2)
                {
                    state_.baseColorGlbModelReferences = draw.model->reference.references;
                    state_.baseColorGlbPrimitiveCount = draw.model->primitives.Count();
                    state_.baseColorGlbMaterialCount = draw.model->materials.Count();
                    state_.baseColorGlbTextureCount = draw.model->textures.Count();
                    if (state_.baseColorGlbTextureCount && draw.model->textures.At(0))
                    {
                        const gk::detail::ImageResource* texture = draw.model->textures.At(0);
                        state_.baseColorGlbTextureWidth = texture->width;
                        state_.baseColorGlbTextureHeight = texture->height;
                    }
                    for (uint32_t part = 0; part < 2; ++part)
                    {
                        const gk::render::ModelPartPlan& source = plan.parts.At(part);
                        state_.baseColorGlbFirstIndices[part] = source.firstIndex;
                        state_.baseColorGlbIndexCounts[part] = source.indexCount;
                        state_.baseColorGlbMaterialIndices[part] = source.materialIndex;
                        state_.baseColorGlbTextureIndices[part] = source.textureIndex;
                        for (uint32_t component = 0; component < 4; ++component)
                            state_.baseColorGlbFactors[part][component] = source.baseColorFactor[component];
                    }
                    state_.baseColorGlbPlanValid = true;

                    if (draw.model->vertices.Count() >= 4 && draw.model->indices.Count() == 6)
                    {
                        state_.baseColorFbxModelReferences = draw.model->reference.references;
                        state_.baseColorFbxVertexCount = draw.model->vertices.Count();
                        state_.baseColorFbxIndexCount = draw.model->indices.Count();
                        state_.baseColorFbxPrimitiveCount = draw.model->primitives.Count();
                        state_.baseColorFbxMaterialCount = draw.model->materials.Count();
                        state_.baseColorFbxTextureCount = draw.model->textures.Count();
                        if (state_.baseColorFbxTextureCount && draw.model->textures.At(0))
                        {
                            const gk::detail::ImageResource* texture = draw.model->textures.At(0);
                            state_.baseColorFbxTextureReferences = texture->reference.references;
                            state_.baseColorFbxTextureWidth = texture->width;
                            state_.baseColorFbxTextureHeight = texture->height;
                        }
                        for (uint32_t part = 0; part < 2; ++part)
                        {
                            const gk::render::ModelPartPlan& source = plan.parts.At(part);
                            state_.baseColorFbxFirstIndices[part] = source.firstIndex;
                            state_.baseColorFbxIndexCounts[part] = source.indexCount;
                            state_.baseColorFbxMaterialIndices[part] = source.materialIndex;
                            state_.baseColorFbxTextureIndices[part] = source.textureIndex;
                        }
                        state_.baseColorFbxPlanValid = true;
                    }
                }
            }
        }
        return true;
    }
    gk::ShaderHandle LoadPixelShader(const char*, gk::String&) override
    {
        ++state_.shaderLoads;
        if (state_.uniqueNativeShaderHandles)
            return gk::ShaderHandle(state_.nextNativeShaderHandle++);
        return gk::ShaderHandle(1);
    }
    bool ReleasePixelShader(gk::ShaderHandle, gk::String& error) override
    {
        ++state_.shaderReleases;
        if (state_.failShaderRelease)
        {
            error.Assign("expected shader release failure");
            return false;
        }
        return true;
    }

  private:
    CaptureState& state_;
};

class TemporaryDirectory
{
  public:
    TemporaryDirectory()
    {
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        const uint64_t stamp = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        for (uint32_t i = 0; i < 64; ++i)
        {
            path_ = base / ("gkcore-core-test-" + std::to_string(stamp) + "-" + std::to_string(i));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error))
                return;
        }
        path_.clear();
    }
    ~TemporaryDirectory()
    {
        if (!path_.empty())
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
    }
    bool IsValid() const
    {
        return !path_.empty();
    }
    std::filesystem::path File(const char* name) const
    {
        return path_ / std::filesystem::u8path(name);
    }

  private:
    std::filesystem::path path_;
};

void Put32(unsigned char* bytes, uint32_t offset, uint32_t value)
{
    bytes[offset] = static_cast<unsigned char>(value);
    bytes[offset + 1] = static_cast<unsigned char>(value >> 8);
    bytes[offset + 2] = static_cast<unsigned char>(value >> 16);
    bytes[offset + 3] = static_cast<unsigned char>(value >> 24);
}

bool WriteOnePixelBmp(const std::filesystem::path& path)
{
    unsigned char bytes[58]{};
    bytes[0] = 'B';
    bytes[1] = 'M';
    Put32(bytes, 2, sizeof(bytes));
    Put32(bytes, 10, 54);
    Put32(bytes, 14, 40);
    Put32(bytes, 18, 1);
    Put32(bytes, 22, 1);
    bytes[26] = 1;
    bytes[28] = 24;
    Put32(bytes, 34, 4);
    bytes[54] = 30;
    bytes[55] = 20;
    bytes[56] = 10;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    return output.good();
}

bool WriteTriangleObj(const std::filesystem::path& path)
{
    std::ofstream output(path);
    output << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    return output.good();
}

bool TestColorAndDimensions()
{
    CHECK(gk::ColorRGB(-1, 300, 42) == 0x00ff2aU);
    CHECK(gk::SetWindowSize(640, 480) == 0);
    CHECK(gk::SetWindowSize(16384, 16384) == -1);
    CHECK(gk::GetLastErrorMessage()[0] != '\0');
    CHECK(gk::SetWindowSize(0xffffffffu, 0xffffffffu) == -1);
    CHECK(gk::SetWindowSize(640, 480) == 0);
    return true;
}

bool TestDefaultWindowDimensions()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    CHECK(capture.initializedWidth == 1280);
    CHECK(capture.initializedHeight == 720);
    gk::Shutdown();
    return true;
}

/**
 * Verifies rectangle outline validation and captures its frame-local state.
 */
bool TestRectangleOutlineContracts()
{
    CaptureState capture;
    capture.uniqueNativeShaderHandles = true;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::DrawRectOutline(0, 0, 10, 10, 0xffffff) == -1);
    CHECK(gk::Init() == 0);
    CHECK(gk::DrawRectOutline(0, 0, 10, 10, 0xffffff) == -1);

    const gk::ShaderHandle pixelShader = gk::LoadPixelShader("outline-pixel.frag");
    const gk::ShaderHandle postShader = gk::LoadPixelShader("outline-post.frag");
    CHECK(pixelShader.IsValid() && postShader.IsValid());
    CHECK(gk::SetPixelShader(pixelShader) == 0);
    CHECK(gk::SetShaderFloat4(pixelShader, 4, { 44, 0, 0, 1 }) == 0);
    CHECK(gk::SetPostEffectShader(postShader) == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::SetDrawLayer(gk::DrawLayer::Scene) == 0);
    CHECK(gk::DrawRect(0, 0, 5, 5, 0x010203, true) == 0);
    CHECK(gk::DrawRectOutline(10, 20, 30, 40, 0x123456, 2.5f) == 0);
    CHECK(gk::SetShaderFloat4(pixelShader, 4, { 55, 0, 0, 1 }) == 0);
    CHECK(gk::DrawRect(50, 60, 70, 80, 0x654321, false) == 0);
    CHECK(gk::SetDrawLayer(gk::DrawLayer::UI) == 0);
    CHECK(gk::DrawRectOutline(1, 2, 3, 4, 0xffffff, 1000000.0f) == 0);

    CHECK(gk::DrawRectOutline(NAN, 0, 1, 1, 0xffffff) == -1);
    CHECK(gk::DrawRectOutline(0, INFINITY, 1, 1, 0xffffff) == -1);
    CHECK(gk::DrawRectOutline(0, 0, 0, 1, 0xffffff) == -1);
    CHECK(gk::DrawRectOutline(0, 0, 1, -1, 0xffffff) == -1);
    CHECK(gk::DrawRectOutline(3.4028234e38f, 0, 3.4028234e38f, 1, 0xffffff) == -1);
    CHECK(gk::DrawRectOutline(0, 0, 1, 1, 0xffffff, 0.0f) == -1);
    CHECK(gk::DrawRectOutline(0, 0, 1, 1, 0xffffff, -1.0f) == -1);
    CHECK(gk::DrawRectOutline(0, 0, 1, 1, 0xffffff, NAN) == -1);
    CHECK(gk::DrawRectOutline(0, 0, 1, 1, 0xffffff, INFINITY) == -1);

    CHECK(gk::Present() == 0);
    CHECK(capture.drawCount == 4);
    CHECK(capture.kinds[0] == gk::detail::DrawKind::Rect && capture.kinds[1] == gk::detail::DrawKind::Rect && capture.kinds[2] == gk::detail::DrawKind::Rect && capture.kinds[3] == gk::detail::DrawKind::Rect);
    CHECK(capture.drawFlags[0] == gk::detail::DrawFilled && capture.drawFlags[1] == 0 && capture.drawFlags[2] == 0 && capture.drawFlags[3] == 0);
    CHECK(capture.rectOutlineThickness[0] == 1.0f && capture.rectOutlineThickness[1] == 2.5f && capture.rectOutlineThickness[2] == 1.0f && capture.rectOutlineThickness[3] == 1000000.0f);
    CHECK(capture.drawColors[0] == 0x010203 && capture.drawColors[1] == 0x123456 && capture.drawColors[2] == 0x654321 && capture.drawColors[3] == 0xffffff);
    CHECK(capture.layers[0] == static_cast<uint8_t>(gk::DrawLayer::Scene) && capture.layers[1] == static_cast<uint8_t>(gk::DrawLayer::Scene) && capture.layers[2] == static_cast<uint8_t>(gk::DrawLayer::Scene) && capture.layers[3] == static_cast<uint8_t>(gk::DrawLayer::UI));
    CHECK(capture.shaderIds[0] == 1 && capture.shaderIds[1] == 1 && capture.shaderIds[2] == 1 && capture.shaderIds[3] == 1);
    CHECK(capture.shaderConstantCounts[0] == 1 && capture.shaderConstantCounts[1] == 1 && capture.shaderConstantCounts[2] == 1 && capture.shaderConstantCounts[3] == 1);
    CHECK(capture.firstShaderConstantX[0] == 44.0f && capture.firstShaderConstantX[1] == 44.0f && capture.firstShaderConstantX[2] == 55.0f && capture.firstShaderConstantX[3] == 55.0f);
    CHECK(capture.postShaders[0].value == 2);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::DrawRectOutline(5, 6, 7, 8, 0xabcdef) == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.drawCount == 1 && capture.rectOutlineThickness[0] == 1.0f);
    CHECK(capture.drawFlags[0] == 0 && capture.postShaders[1].value == 2);
    CHECK(gk::SetPostEffectShader({}) == 0);
    CHECK(gk::DeleteShader(pixelShader) == 0);
    CHECK(gk::DeleteShader(postShader) == 0);
    gk::Shutdown();
    return true;
}

bool TestFrameStateAndMixedDraws()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::SetBloomEnabled(false) == 0);
    CHECK(gk::SetExposure(2.0f) == 0);
    CHECK(gk::SetSaturation(0.5f) == 0);
    CHECK(gk::SetContrast(1.5f) == 0);
    CHECK(gk::SetFxaaEnabled(false) == 0);
    CHECK(gk::Init() == 0);
    CHECK(capture.initializedWidth == 640 && capture.initializedHeight == 480);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::SetSaturation(0.75f) == 0);
    CHECK(gk::SetContrast(0.75f) == 0);
    CHECK(gk::SetFxaaEnabled(true) == 0);
    CHECK(gk::SetCamera({ 2.0f, 0.0f, -5.0f }, { 0.0f, 0.0f, 0.0f }) == 0);
    CHECK(gk::DrawRect(1, 2, 30, 40, gk::ColorRGB(4, 5, 6)) == 0);
    CHECK(gk::SetDrawLayer(gk::DrawLayer::UI) == 0);
    CHECK(gk::DrawTriangle3D({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, 0x00ffffff) == 0);
    CHECK(gk::SetDrawLayer(gk::DrawLayer::Scene) == 0);
    CHECK(gk::SetCamera({ 3.0f, 0.0f, -5.0f }, { 0.0f, 0.0f, 0.0f }) == 0);
    CHECK(gk::DrawTriangle3D({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, 0x00ffffff) == 0);
    CHECK(gk::SetBloomEnabled(true) == 0);
    CHECK(gk::SetExposure(4.0f) == 0);
    CHECK(gk::SetCamera({ 0.0f, 2.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }) == -1);
    CHECK(gk::DrawRect(NAN, 0, 1, 1, 0) == -1);
    CHECK(gk::DrawRect(3.4028234e38f, 0, 3.4028234e38f, 1, 0) == -1);
    CHECK(gk::Present() == 0);
    CHECK(capture.drawCount == 3);
    CHECK(capture.cameraX[0] == 2.0f && capture.cameraX[1] == 2.0f && capture.cameraX[2] == 3.0f);
    CHECK(capture.layers[0] == static_cast<uint8_t>(gk::DrawLayer::Scene));
    CHECK(capture.layers[1] == static_cast<uint8_t>(gk::DrawLayer::UI));
    CHECK(capture.layers[2] == static_cast<uint8_t>(gk::DrawLayer::Scene));
    CHECK(!capture.bloomEnabled && capture.exposure == 2.0f);
    CHECK(capture.saturation == 0.5f && capture.contrast == 1.5f && !capture.fxaaEnabled);
    CHECK(gk::ProcessEvents());
    CHECK(gk::IsKeyDown(gk::Key::Escape));
    capture.clientWidth = 800;
    capture.clientHeight = 600;
    CHECK(gk::ProcessEvents());
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.presentedWidth == 800);
    CHECK(capture.bloomEnabled && capture.exposure == 4.0f);
    CHECK(capture.saturation == 0.75f && capture.contrast == 0.75f && capture.fxaaEnabled);
    CHECK(gk::SetSaturation(NAN) == -1);
    CHECK(gk::SetContrast(2.1f) == -1);
    CHECK(gk::GetLastErrorMessage()[0] != '\0');
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.saturation == 0.75f && capture.contrast == 0.75f);
    capture.eventResult = -1;
    CHECK(!gk::ProcessEvents());
    CHECK(gk::GetLastErrorMessage()[0] == '\0');
    gk::Shutdown();
    CHECK(gk::effects::Current().saturation == 1.0f);
    CHECK(gk::effects::Current().contrast == 1.0f && gk::effects::Current().fxaaEnabled);
    CHECK(capture.destroyed);
    return true;
}

bool TestInitializationRollback()
{
    CaptureState capture;
    capture.failInitialize = true;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == -1);
    CHECK(!capture.initialized);
    CHECK(capture.shutdownCalled);
    CHECK(capture.destroyed);
    CHECK(gk::GetLastErrorMessage()[0] != '\0');
    gk::Shutdown();
    return true;
}

/**
 * 短い押下のキー変換、非消費の問い合わせ、focusと初期化の境界を確認する。
 */
bool TestKeyPressedContracts()
{
    // 公開APIへ入力状態を返すテスト用backendの記録。
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    for (uint32_t index = 0; index < kFKeyMappingExpectationCount; ++index)
    {
        const FKeyMappingExpectation& expected = kFKeyMappingExpectations[index];
        const FKeyMappingExpectation& other = kFKeyMappingExpectations[(index + 1) % kFKeyMappingExpectationCount];
        capture.pressedKey = expected.virtualKey;
        CHECK(gk::WasKeyPressed(expected.key));
        CHECK(capture.lastPressedKey == expected.virtualKey);
        capture.pressedKey = other.virtualKey;
        CHECK(!gk::WasKeyPressed(expected.key));
        CHECK(capture.lastPressedKey == expected.virtualKey);
    }
    capture.pressedKey = 0x20;
    CHECK(gk::WasKeyPressed(gk::Key::Space));
    CHECK(gk::WasKeyPressed(gk::Key::Space));
    const uint32_t lastPressedKey = capture.lastPressedKey;
    CHECK(!gk::WasKeyPressed(static_cast<gk::Key>(255)));
    CHECK(gk::GetLastErrorMessage()[0] != '\0');
    CHECK(capture.lastPressedKey == lastPressedKey);
    capture.hasFocus = false;
    capture.lastKey = 0xabcdef01u;
    capture.lastPressedKey = 0xabcdef02u;
    CHECK(!gk::WasKeyPressed(gk::Key::Space));
    CHECK(capture.lastPressedKey == 0xabcdef02u);
    CHECK(!gk::IsKeyDown(gk::Key::Space));
    CHECK(capture.lastKey == 0xabcdef01u);
    CHECK(gk::GetLastErrorMessage()[0] == '\0');
    gk::Shutdown();
    CHECK(!gk::WasKeyPressed(gk::Key::Space));
    CHECK(gk::GetLastErrorMessage()[0] != '\0');
    CHECK(!gk::IsKeyDown(gk::Key::Space));
    CHECK(gk::GetLastErrorMessage()[0] != '\0');
    CHECK(capture.lastPressedKey == 0xabcdef02u);
    CHECK(capture.lastKey == 0xabcdef01u);
    return true;
}

bool TestInputKeyAndMouseMappings()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    for (uint32_t index = 0; index < kFKeyMappingExpectationCount; ++index)
    {
        const FKeyMappingExpectation& expected = kFKeyMappingExpectations[index];
        const FKeyMappingExpectation& other = kFKeyMappingExpectations[(index + 1) % kFKeyMappingExpectationCount];
        capture.downKey = expected.virtualKey;
        CHECK(gk::IsKeyDown(expected.key));
        CHECK(capture.lastKey == expected.virtualKey);
        capture.downKey = other.virtualKey;
        CHECK(!gk::IsKeyDown(expected.key));
        CHECK(capture.lastKey == expected.virtualKey);
    }
    const uint32_t lastMappedKey = capture.lastKey;
    CHECK(!gk::IsKeyDown(static_cast<gk::Key>(255)));
    CHECK(gk::GetLastErrorMessage()[0] != '\0');
    CHECK(capture.lastKey == lastMappedKey);
    CHECK(!gk::IsKeyDown(gk::Key::Tab));
    CHECK(gk::GetLastErrorMessage()[0] == '\0');

    CHECK(gk::IsMouseButtonDown(gk::MouseButton::Left));
    CHECK(capture.lastMouseButton == 1);
    CHECK(!gk::IsMouseButtonDown(gk::MouseButton::Right));
    CHECK(capture.lastMouseButton == 2);
    capture.downMouseButton = 4;
    CHECK(gk::IsMouseButtonDown(gk::MouseButton::Middle));
    CHECK(capture.lastMouseButton == 4);
    CHECK(!gk::IsMouseButtonDown(static_cast<gk::MouseButton>(255)));
    CHECK(gk::GetLastErrorMessage()[0] != '\0');
    int32_t x = -1;
    int32_t y = -1;
    CHECK(gk::GetMousePosition(x, y));
    CHECK(x == 12 && y == 34);

    capture.hasFocus = false;
    capture.lastKey = 0xabcdef01u;
    capture.lastPressedKey = 0xabcdef02u;
    CHECK(!gk::IsKeyDown(gk::Key::A));
    CHECK(capture.lastKey == 0xabcdef01u);
    CHECK(!gk::WasKeyPressed(gk::Key::A));
    CHECK(capture.lastPressedKey == 0xabcdef02u);
    const uint32_t lastButtonBeforeFocusLoss = capture.lastMouseButton;
    CHECK(!gk::IsMouseButtonDown(gk::MouseButton::Left));
    CHECK(capture.lastMouseButton == lastButtonBeforeFocusLoss);
    x = -1;
    y = -1;
    CHECK(!gk::GetMousePosition(x, y));
    CHECK(x == 0 && y == 0);
    CHECK(gk::GetLastErrorMessage()[0] == '\0');
    gk::Shutdown();
    return true;
}

bool TestJapaneseTextDrawContracts()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::SetDrawLayer(gk::DrawLayer::UI) == 0);
    CHECK(gk::DrawString(1.0f, 2.0f, nullptr, 0xffffff) == -1);
    const char overlong[] = "\xc0\xaf";
    const char continuation[] = "\x80";
    const char truncated[] = "\xe3\x81";
    const char surrogate[] = "\xed\xa0\x80";
    const char tooLarge[] = "\xf4\x90\x80\x80";
    CHECK(gk::DrawString(1.0f, 2.0f, overlong, 0xffffff) == -1);
    CHECK(gk::DrawString(1.0f, 2.0f, continuation, 0xffffff) == -1);
    CHECK(gk::DrawString(1.0f, 2.0f, truncated, 0xffffff) == -1);
    CHECK(gk::DrawString(1.0f, 2.0f, surrogate, 0xffffff) == -1);
    CHECK(gk::DrawString(1.0f, 2.0f, tooLarge, 0xffffff) == -1);
    CHECK(capture.textRasterizations == 0);
    CHECK(gk::DrawString(1.0f, 2.0f, "text", 0xffffff, 0) == -1);
    CHECK(gk::DrawString(1.0f, 2.0f, "text", 0xffffff, 257) == -1);
    CHECK(gk::DrawString(INFINITY, 2.0f, "text", 0xffffff) == -1);
    char tooLong[4100];
    for (uint32_t i = 0; i + 1 < sizeof(tooLong); ++i)
        tooLong[i] = 'x';
    tooLong[sizeof(tooLong) - 1] = '\0';
    CHECK(gk::DrawString(1.0f, 2.0f, tooLong, 0xffffff) == -1);
    CHECK(capture.textRasterizations == 0);
    CHECK(gk::DrawString(1.0f, 2.0f, "", 0xffffff) == 0);
    CHECK(capture.textRasterizations == 0);
    CHECK(gk::DrawString(24.0f, 36.0f, "日本語", 0x003366ccu, 20) == 0);
    CHECK(capture.textRasterizations == 1);
    CHECK(capture.rasterizedTextSize == 20 && capture.rasterizedTextColor == 0x003366ccu);
    CHECK(capture.rasterizedText[0] == '\xe6' && capture.rasterizedText[1] == '\x97');
    CHECK(gk::DrawString(80.0f, 96.0f, "日本語", 0x003366ccu, 20) == 0);
    CHECK(capture.textRasterizations == 1);
    CHECK(gk::Present() == 0);
    CHECK(capture.textPacketValid);
    CHECK(capture.textPackets == 2);
    CHECK(capture.textImages[0] == capture.textImages[1]);
    CHECK(capture.textImageReferences == 3);
    CHECK(capture.textX == 80.0f && capture.textY == 96.0f);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::SetDrawLayer(gk::DrawLayer::UI) == 0);
    CHECK(gk::DrawString(120.0f, 140.0f, "日本語", 0x003366ccu, 20) == 0);
    CHECK(capture.textRasterizations == 1);
    capture.failTextRasterization = true;
    CHECK(gk::DrawString(0.0f, 0.0f, "fail", 0xffffff) == -1);
    CHECK(gk::Present() == 0);
    CHECK(capture.textPacketValid);
    CHECK(capture.textPackets == 3);
    CHECK(capture.textImages[0] == capture.textImages[1] && capture.textImages[1] == capture.textImages[2]);
    CHECK(capture.textImageReferences == 2);
    CHECK(capture.textX == 120.0f && capture.textY == 140.0f);
    CHECK(gk::GetLastErrorMessage()[0] == '\0');
    gk::Shutdown();
    CHECK(gk::detail::TextImageCacheEntryCount() == 0);
    CHECK(gk::detail::TextImageCacheByteCount() == 0);
    return true;
}

bool TestTextCacheMemoryBound()
{
    CaptureState capture;
    capture.textImageWidth = 2048;
    capture.textImageHeight = 512;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    CHECK(gk::BeginFrame() == 0);
    char label[32];
    for (uint32_t i = 0; i < 5; ++i)
    {
        snprintf(label, sizeof(label), "cache-%u", i);
        CHECK(gk::DrawString(0.0f, static_cast<float>(i * 32), label, 0xffffff, 16) == 0);
    }
    CHECK(capture.textRasterizations == 5);
    CHECK(gk::detail::TextImageCacheEntryCount() == 4);
    CHECK(gk::detail::TextImageCacheByteCount() == 16ull * 1024ull * 1024ull);
    CHECK(gk::Present() == 0);
    gk::Shutdown();
    CHECK(gk::detail::TextImageCacheEntryCount() == 0);
    CHECK(gk::detail::TextImageCacheByteCount() == 0);
    return true;
}

bool TestTextCacheEntryBound()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    CHECK(gk::BeginFrame() == 0);
    char label[32];
    for (uint32_t i = 0; i < 70; ++i)
    {
        snprintf(label, sizeof(label), "entry-%u", i);
        CHECK(gk::DrawString(0.0f, 0.0f, label, 0xffffff, 16) == 0);
    }
    CHECK(capture.textRasterizations == 70);
    CHECK(gk::detail::TextImageCacheEntryCount() == 64);
    CHECK(gk::detail::TextImageCacheByteCount() == 64ull * 16ull);
    CHECK(gk::Present() == 0);
    gk::Shutdown();
    CHECK(gk::detail::TextImageCacheEntryCount() == 0);
    CHECK(gk::detail::TextImageCacheByteCount() == 0);
    return true;
}

bool TestQueuedImageSurvivesDeletion()
{
    TemporaryDirectory temporary;
    CHECK(temporary.IsValid());
    const std::filesystem::path imagePath = temporary.File(u8"画像.bmp");
    CHECK(WriteOnePixelBmp(imagePath));
    const std::filesystem::path modelPath = temporary.File("triangle.obj");
    CHECK(WriteTriangleObj(modelPath));
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    const std::string path = imagePath.u8string();
    const std::string modelFile = modelPath.u8string();
    const gk::ImageHandle image = gk::LoadImage(path.c_str());
    const gk::ModelHandle model = gk::LoadModel(modelFile.c_str());
    CHECK(image.IsValid());
    CHECK(model.IsValid());
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::DrawImage(image, 4.0f, 5.0f) == 0);
    CHECK(gk::DrawImageRotated(image, 12.0f, 14.0f, 2.0f, 0.75f) == 0);
    CHECK(gk::SetModelPosition(model, { 2.0f, 0.0f, 0.0f }) == 0);
    CHECK(gk::SetModelRotation(model, { 0.0f, 0.25f, 0.0f }) == 0);
    CHECK(gk::SetModelScale(model, { 2.0f, 2.0f, 2.0f }) == 0);
    CHECK(gk::DrawModel(model) == 0);
    CHECK(gk::SetModelPosition(model, { 3.0f, 0.0f, 0.0f }) == 0);
    CHECK(gk::DrawModel(model) == 0);
    CHECK(gk::DeleteImage(image) == 0);
    CHECK(gk::DeleteModel(model) == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.drawCount == 4);
    CHECK(capture.queuedImageValid && capture.queuedImageRed == 10);
    CHECK(capture.queuedImageCentered);
    CHECK(capture.queuedModelValid);
    CHECK(capture.queuedModelPositions[0] == 2.0f && capture.queuedModelPositions[1] == 3.0f);
    gk::Shutdown();
    return true;
}

bool TestCheckedInBaseColorGlbSurvivesPublicDeletion()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    const std::filesystem::path assetPath = std::filesystem::path(GKCORE_TEST_SOURCE_DIR) / "tests/assets/models/gkcore_basecolor.glb";
    const std::string utf8Path = assetPath.u8string();
    const gk::ModelHandle model = gk::LoadModel(utf8Path.c_str());
    CHECK(model.IsValid());
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::DrawModel(model) == 0);
    CHECK(gk::DeleteModel(model) == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.drawCount == 1);
    CHECK(capture.baseColorGlbPlanValid);
    CHECK(capture.baseColorGlbModelReferences == 1);
    CHECK(capture.baseColorGlbPrimitiveCount == 2);
    CHECK(capture.baseColorGlbMaterialCount == 2);
    CHECK(capture.baseColorGlbTextureCount == 1);
    CHECK(capture.baseColorGlbTextureWidth > 0 && capture.baseColorGlbTextureHeight > 0);
    CHECK(capture.baseColorGlbFirstIndices[0] == 0 && capture.baseColorGlbIndexCounts[0] == 3);
    CHECK(capture.baseColorGlbFirstIndices[1] == 3 && capture.baseColorGlbIndexCounts[1] == 3);
    CHECK(capture.baseColorGlbMaterialIndices[0] == 0 && capture.baseColorGlbMaterialIndices[1] == 1);
    CHECK(capture.baseColorGlbTextureIndices[0] == 0 && capture.baseColorGlbTextureIndices[1] == -1);
    CHECK(capture.baseColorGlbFactors[0][0] == 0.8f && capture.baseColorGlbFactors[0][1] == 0.65f && capture.baseColorGlbFactors[0][2] == 0.35f && capture.baseColorGlbFactors[0][3] == 1.0f);
    CHECK(capture.baseColorGlbFactors[1][0] == 0.25f && capture.baseColorGlbFactors[1][1] == 0.7f && capture.baseColorGlbFactors[1][2] == 0.4f && capture.baseColorGlbFactors[1][3] == 0.9f);
    gk::Shutdown();
    return true;
}

/**
 * Loads one checked-in FBX through the public API and checks its retained draw payload.
 */
bool TestCheckedInFbxSurvivesPublicDeletion(const char* filename)
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    const std::filesystem::path assetPath = std::filesystem::path(GKCORE_TEST_SOURCE_DIR) / "tests/assets/models" / filename;
    const std::string utf8Path = assetPath.u8string();
    const gk::ModelHandle model = gk::LoadModel(utf8Path.c_str());
    CHECK(model.IsValid());
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::DrawModel(model) == 0);
    CHECK(gk::DeleteModel(model) == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.drawCount == 1);
    CHECK(capture.baseColorFbxPlanValid);
    CHECK(capture.baseColorFbxModelReferences == 1);
    CHECK(capture.baseColorFbxVertexCount >= 4 && capture.baseColorFbxIndexCount == 6);
    CHECK(capture.baseColorFbxPrimitiveCount == 2 && capture.baseColorFbxMaterialCount == 2);
    CHECK(capture.baseColorFbxTextureCount == 1 && capture.baseColorFbxTextureReferences == 1);
    CHECK(capture.baseColorFbxTextureWidth == 1 && capture.baseColorFbxTextureHeight == 1);
    CHECK(capture.baseColorFbxFirstIndices[0] == 0 && capture.baseColorFbxIndexCounts[0] == 3);
    CHECK(capture.baseColorFbxFirstIndices[1] == 3 && capture.baseColorFbxIndexCounts[1] == 3);
    CHECK(capture.baseColorFbxMaterialIndices[0] == 0 && capture.baseColorFbxMaterialIndices[1] == 1);
    CHECK(capture.baseColorFbxTextureIndices[0] == 0);
    gk::Shutdown();
    return true;
}

/**
 * Exercises importer cleanup at successive allocator failure points and a clean retry.
 */
bool TestFbxImportAllocationFailuresRecover()
{
    const std::filesystem::path assetPath = std::filesystem::path(GKCORE_TEST_SOURCE_DIR) / "tests/assets/models/gkcore_ascii.fbx";
    const std::string utf8Path = assetPath.u8string();
    uint32_t failedAttempts = 0;
    bool reachedSuccess = false;
    for (uint32_t allocation = 0; allocation < 256; ++allocation)
    {
        gk::String error;
        CHECK(error.Assign("preallocated FBX import diagnostic"));
        gk::SetAllocationFailureAfterForTesting(allocation);
        gk::detail::ModelResource* model = gk::detail::LoadModelPayload(utf8Path.c_str(), error);
        gk::ResetAllocationFailureForTesting();
        if (model)
        {
            gk::Release(&model->reference);
            reachedSuccess = true;
            break;
        }
        ++failedAttempts;
    }
    CHECK(failedAttempts > 0 && reachedSuccess);
    gk::String recoveryError;
    gk::detail::ModelResource* recovered = gk::detail::LoadModelPayload(utf8Path.c_str(), recoveryError);
    CHECK(recovered != nullptr);
    CHECK(recovered->indices.Count() == 6 && recovered->textures.Count() == 1);
    gk::Release(&recovered->reference);
    return true;
}

bool TestShaderLifetimesAndSnapshots()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    gk::detail::Context& context = gk::detail::GetContext();
    CHECK(context.nativeShaders.Reserve(1));
    gk::SetAllocationFailureAfterForTesting(0);
    const gk::ShaderHandle failed = gk::LoadPixelShader("allocation-failure.bin");
    gk::ResetAllocationFailureForTesting();
    CHECK(!failed.IsValid());
    CHECK(gk::GetLastErrorMessage()[0] != '\0');
    CHECK(context.nativeShaders.Count() == 0);
    CHECK(capture.shaderReleases == 1);

    const gk::ShaderHandle first = gk::LoadPixelShader("first.bin");
    const gk::ShaderHandle second = gk::LoadPixelShader("second.bin");
    CHECK(first.IsValid() && second.IsValid() && first != second);
    CHECK(capture.shaderLoads == 3);
    CHECK(gk::SetShaderFloat4(second, 0, { 3, 4, 5, 6 }) == 0);
    CHECK(gk::SetPixelShader(first) == 0);
    CHECK(gk::SetPixelShader({}) == 0);
    CHECK(gk::SetPixelShader(second) == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::DrawRect(1, 1, 4, 4, 0xffffff) == 0);
    CHECK(gk::DeleteShader(second) == -1);
    CHECK(gk::Present() == 0);
    CHECK(capture.shaderIds[0] == 1 && capture.shaderConstantCounts[0] == 1);
    CHECK(capture.firstShaderConstantX[0] == 3.0f);
    capture.failShaderRelease = true;
    CHECK(gk::DeleteShader(second) == -1);
    CHECK(gk::SetPixelShader(second) == 0);
    capture.failShaderRelease = false;
    CHECK(gk::DeleteShader(first) == 0);
    CHECK(gk::SetPixelShader(first) == -1);
    gk::Shutdown();

    CaptureState nextCapture;
    gk::detail::SetBackendForTesting(new CaptureBackend(nextCapture));
    CHECK(gk::Init() == 0);
    CHECK(gk::SetPixelShader(second) == -1);
    const gk::ShaderHandle next = gk::LoadPixelShader("again.bin");
    CHECK(next.IsValid() && next != second);
    gk::Shutdown();
    return true;
}

bool TestShaderSnapshotsAcrossDrawKinds()
{
    TemporaryDirectory temporary;
    CHECK(temporary.IsValid());
    const std::filesystem::path imagePath = temporary.File("shader-pixel.bmp");
    CHECK(WriteOnePixelBmp(imagePath));
    const std::filesystem::path modelPath = temporary.File("shader-triangle.obj");
    CHECK(WriteTriangleObj(modelPath));
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    const std::string imageFile = imagePath.u8string();
    const std::string modelFile = modelPath.u8string();
    const gk::ImageHandle image = gk::LoadImage(imageFile.c_str());
    const gk::ModelHandle model = gk::LoadModel(modelFile.c_str());
    CHECK(image.IsValid() && model.IsValid());
    const gk::ShaderHandle shader = gk::LoadPixelShader("sprite-compatible.bin");
    CHECK(shader.IsValid());
    CHECK(gk::SetPixelShader(shader) == 0);
    CHECK(gk::SetShaderFloat4(shader, 0, { 0.5f, 0.0f, 0.0f, 1.0f }) == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::DrawImage(image, 2.0f, 3.0f) == 0);
    CHECK(gk::SetShaderFloat4(shader, 0, { 1.0f, 0.0f, 0.0f, 1.0f }) == 0);
    CHECK(gk::DrawTriangle3D({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, 0xffffff) == 0);
    CHECK(gk::SetShaderFloat4(shader, 0, { 2.0f, 0.0f, 0.0f, 1.0f }) == 0);
    CHECK(gk::DrawModel(model) == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.drawCount == 3);
    CHECK(capture.shaderIds[0] == 1 && capture.shaderIds[1] == 1 && capture.shaderIds[2] == 1);
    CHECK(capture.shaderConstantCounts[0] == 1 && capture.shaderConstantCounts[1] == 1 && capture.shaderConstantCounts[2] == 1);
    CHECK(capture.firstShaderConstantX[0] == 0.5f);
    CHECK(capture.firstShaderConstantX[1] == 1.0f);
    CHECK(capture.firstShaderConstantX[2] == 2.0f);
    CHECK(gk::DeleteImage(image) == 0);
    CHECK(gk::DeleteShader(shader) == 0);
    gk::Shutdown();
    return true;
}

/**
 * Verifies independent draw/post selections, frame timing, and shader deletion lifetime.
 */
bool TestPostEffectShaderSnapshotsAndLifecycle()
{
    CaptureState capture;
    capture.uniqueNativeShaderHandles = true;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::SetPostEffectShader({}) == -1);
    CHECK(gk::Init() == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::Present() == 0);
    CHECK(!capture.postShaders[0].IsValid() && capture.postConstantCounts[0] == 0);

    const gk::ShaderHandle pixelShader = gk::LoadPixelShader("pixel-stage.bin");
    const gk::ShaderHandle postShader = gk::LoadPixelShader("post-effect.bin");
    CHECK(pixelShader.IsValid() && postShader.IsValid());
    CHECK(gk::SetPixelShader(pixelShader) == 0);
    CHECK(gk::SetShaderFloat4(pixelShader, 3, { 31, 0, 0, 1 }) == 0);
    CHECK(gk::SetShaderFloat4(postShader, 7, { 7, 0, 0, 1 }) == 0);
    CHECK(gk::SetShaderFloat4(postShader, 1, { 1, 0, 0, 1 }) == 0);
    CHECK(gk::SetPostEffectShader(postShader) == 0);

    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::SetShaderFloat4(postShader, 7, { 70, 0, 0, 1 }) == 0);
    CHECK(gk::SetPostEffectShader({}) == 0);
    CHECK(gk::DeleteShader(postShader) == -1);
    CHECK(gk::DrawRect(0, 0, 8, 8, 0xffffff) == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.postShaders[1].value == 2);
    CHECK(capture.postConstantCounts[1] == 2);
    CHECK(capture.postSlots[1][0] == 1 && capture.postSlots[1][1] == 7);
    CHECK(capture.postValues[1][0] == 1.0f && capture.postValues[1][1] == 7.0f);
    CHECK(capture.pixelShaderForPostFrame[1] == 1);
    CHECK(capture.firstShaderConstantX[0] == 31.0f);

    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::SetPostEffectShader(postShader) == 0);
    CHECK(gk::Present() == 0);
    CHECK(!capture.postShaders[2].IsValid() && capture.postConstantCounts[2] == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.postShaders[3].value == 2 && capture.postConstantCounts[3] == 2);
    CHECK(capture.postValues[3][1] == 70.0f);
    CHECK(gk::DeleteShader(postShader) == 0);
    CHECK(gk::SetPostEffectShader(pixelShader) == 0);
    CHECK(gk::SetPostEffectShader(postShader) == -1);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.postShaders[4].value == 1 && capture.postConstantCounts[4] == 1);
    gk::Shutdown();

    CaptureState nextCapture;
    nextCapture.uniqueNativeShaderHandles = true;
    gk::detail::SetBackendForTesting(new CaptureBackend(nextCapture));
    CHECK(gk::Init() == 0);
    CHECK(gk::SetPostEffectShader(postShader) == -1);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::Present() == 0);
    CHECK(!nextCapture.postShaders[0].IsValid());
    gk::Shutdown();
    return true;
}

/**
 * Verifies that a failed post-constant snapshot leaves BeginFrame retryable.
 */
bool TestPostEffectBeginFrameSnapshotIsTransactional()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    const gk::ShaderHandle postShader = gk::LoadPixelShader("post-effect-oom.bin");
    CHECK(postShader.IsValid());
    CHECK(gk::SetShaderFloat4(postShader, 5, { 5, 0, 0, 1 }) == 0);
    CHECK(gk::SetPostEffectShader(postShader) == 0);

    gk::SetAllocationFailureAfterForTesting(0);
    const int failedBegin = gk::BeginFrame();
    gk::ResetAllocationFailureForTesting();
    CHECK(failedBegin == -1);
    CHECK(gk::Present() == -1);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.postFrameCount == 1);
    CHECK(capture.postShaders[0].value == 1);
    CHECK(capture.postConstantCounts[0] == 1 && capture.postSlots[0][0] == 5);
    gk::Shutdown();
    return true;
}

/**
 * Verifies that lighting settings are sampled once at BeginFrame.
 */
bool TestLightingBeginFrameSnapshot()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::SetAmbientLight(-0.1f) == -1);
    CHECK(gk::GetLastErrorMessage() && gk::GetLastErrorMessage()[0] != '\0');
    CHECK(gk::SetAmbientLight(0.35f) == 0);
    CHECK(gk::GetLastErrorMessage() && gk::GetLastErrorMessage()[0] == '\0');
    CHECK(gk::SetDirectionalLight({ 3.0f, -4.0f, 0.0f }, 6.0f) == 0);
    CHECK(gk::Init() == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::SetAmbientLight(0.75f) == 0);
    CHECK(gk::SetDirectionalLight({ 0.0f, 0.0f, -2.0f }, 9.0f) == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.lightingFrameCount == 1);
    CHECK(capture.ambientLight[0] == 0.35f);
    CHECK(fabsf(capture.directionalDirection[0][0] - 0.6f) < 1e-6f);
    CHECK(fabsf(capture.directionalDirection[0][1] + 0.8f) < 1e-6f);
    CHECK(capture.directionalDirection[0][2] == 0.0f && capture.directionalIntensity[0] == 6.0f);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::Present() == 0);
    CHECK(capture.lightingFrameCount == 2 && capture.ambientLight[1] == 0.75f);
    CHECK(capture.directionalDirection[1][0] == 0.0f && capture.directionalDirection[1][1] == 0.0f);
    CHECK(capture.directionalDirection[1][2] == -1.0f && capture.directionalIntensity[1] == 9.0f);
    gk::Shutdown();

    CaptureState resetCapture;
    gk::detail::SetBackendForTesting(new CaptureBackend(resetCapture));
    CHECK(gk::Init() == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::Present() == 0);
    CHECK(resetCapture.lightingFrameCount == 1 && resetCapture.ambientLight[0] == 0.2f);
    CHECK(fabsf(resetCapture.directionalDirection[0][0] + 0.4082483f) < 1e-6f);
    CHECK(fabsf(resetCapture.directionalDirection[0][1] + 0.8164966f) < 1e-6f);
    CHECK(fabsf(resetCapture.directionalDirection[0][2] - 0.4082483f) < 1e-6f);
    CHECK(resetCapture.directionalIntensity[0] == 3.0f);
    gk::Shutdown();
    return true;
}
/**
 * 画面更新への同期はframe開始時に固定し、次frameと終了後の初期値を区別する。
 */
bool TestVSyncFrameSnapshots()
{
    CaptureState capture;
    gk::detail::SetBackendForTesting(new CaptureBackend(capture));
    CHECK(gk::Init() == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::detail::GetContext().frame.vSyncEnabled);
    CHECK(gk::SetVSyncEnabled(false) == 0);
    CHECK(gk::detail::GetContext().frame.vSyncEnabled);
    CHECK(gk::Present() == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(!gk::detail::GetContext().frame.vSyncEnabled);
    CHECK(gk::SetVSyncEnabled(true) == 0);
    CHECK(!gk::detail::GetContext().frame.vSyncEnabled);
    CHECK(gk::Present() == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::detail::GetContext().frame.vSyncEnabled);
    CHECK(gk::Present() == 0);
    CHECK(gk::SetVSyncEnabled(false) == 0);
    gk::Shutdown();
    CaptureState nextCapture;
    gk::detail::SetBackendForTesting(new CaptureBackend(nextCapture));
    CHECK(gk::Init() == 0);
    CHECK(gk::BeginFrame() == 0);
    CHECK(gk::detail::GetContext().frame.vSyncEnabled);
    CHECK(gk::Present() == 0);
    gk::Shutdown();
    return true;
}

}

int main()
{
    int failures = 0;
    if (!gk::tests::FoundationContracts())
    {
        fprintf(stderr, "foundation contract failed\n");
        ++failures;
    }
    gk::String effectFailure;
    if (!gk::tests::EffectsContract(effectFailure))
    {
        fprintf(stderr, "effects contract failed: %s\n", effectFailure.CStr());
        ++failures;
    }
    gk::String resourceFailure;
    if (!gk::tests::ResourceContract(resourceFailure))
    {
        fprintf(stderr, "resource contract failed: %s\n", resourceFailure.CStr());
        ++failures;
    }
    gk::String shaderFailure;
    if (!gk::tests::ShaderArtifactContract(shaderFailure))
    {
        fprintf(stderr, "shader artifact contract failed: %s\n", shaderFailure.CStr());
        ++failures;
    }
    gk::String postProcessFailure;
    if (!gk::tests::PostProcessMathContract(postProcessFailure))
    {
        fprintf(stderr, "post-process math contract failed: %s\n", postProcessFailure.CStr());
        ++failures;
    }
    if (!TestVSyncFrameSnapshots())
        ++failures;
    if (!TestDefaultWindowDimensions())
        ++failures;
    if (!TestColorAndDimensions())
        ++failures;
    if (!TestFrameStateAndMixedDraws())
        ++failures;
    if (!TestInitializationRollback())
        ++failures;
    if (!TestInputKeyAndMouseMappings())
        ++failures;
    if (!TestKeyPressedContracts())
        ++failures;
    if (!TestJapaneseTextDrawContracts())
        ++failures;
    if (!TestTextCacheMemoryBound())
        ++failures;
    if (!TestTextCacheEntryBound())
        ++failures;
    if (!TestQueuedImageSurvivesDeletion())
        ++failures;
    if (!TestCheckedInBaseColorGlbSurvivesPublicDeletion())
        ++failures;
    if (!TestCheckedInFbxSurvivesPublicDeletion("gkcore_ascii.fbx"))
        ++failures;
    if (!TestCheckedInFbxSurvivesPublicDeletion("gkcore_binary.fbx"))
        ++failures;
    if (!TestFbxImportAllocationFailuresRecover())
        ++failures;
    if (!TestShaderLifetimesAndSnapshots())
        ++failures;
    if (!TestShaderSnapshotsAcrossDrawKinds())
        ++failures;
    if (!TestPostEffectShaderSnapshotsAndLifecycle())
        ++failures;
    if (!TestPostEffectBeginFrameSnapshotIsTransactional())
        ++failures;
    if (!TestLightingBeginFrameSnapshot())
        ++failures;
    if (!TestRectangleOutlineContracts())
        ++failures;
    return failures == 0 ? 0 : 1;
}
