#ifndef GKCORE_TESTS_SUPPORT_FRAMECAPTURE_H
#define GKCORE_TESTS_SUPPORT_FRAMECAPTURE_H

#include <Graphics/Interfaces/IGraphics.h>

#include "../../src/foundation/String.h"

/**
 * 開発テスト用のDirect3D 12画素読み戻し機能をまとめる。
 */
namespace gk::render::test_support
{

/**
 * 最終swapchain画像の読み戻しresourceと出力先を所有する。
 */
class FFrameCapture
{
  public:
    /**
     * 読み戻しresourceを解放する。
     */
    ~FFrameCapture();

    /**
     * GKCORE_TEST_CAPTURE_PATHを一度読み、出力先を保持する。
     */
    bool ReadRequest(String& error);

    /**
     * 描画commandへ最終render targetのcopyを記録する。
     */
    bool Record(Renderer* renderer, Cmd* command, RenderTarget* target, String& error);

    /**
     * fence完了後にRGB画素をP6 PPMへ保存する。
     */
    bool Complete(Renderer* renderer, Fence* fence, String& error);

    /**
     * renderer破棄前に所有resourceを解放する。
     */
    void Reset();

  private:
    ID3D12Resource* readback_ = nullptr; // GPUから読み戻すresource。
    String outputPath_;                  // 一回だけ使う出力先。
    uint64_t readbackSize_ = 0;          // 行paddingを含む読み戻し領域の大きさ。
    uint32_t width_ = 0;                 // キャプチャ画像の幅。
    uint32_t height_ = 0;                // キャプチャ画像の高さ。
    uint32_t rowPitch_ = 0;              // GPU readback buffer上の1行のbyte数。
    bool blueFirst_ = false;             // BGRA resourceならRGB出力時に赤と青を入れ替える。
    bool requestChecked_ = false;        // 環境変数の読み取りを済ませたか。
};

}

#endif
