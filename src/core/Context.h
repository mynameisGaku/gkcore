// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_CORE_CONTEXT_H
#define GKCORE_CORE_CONTEXT_H

#include "../internal/Backend.hpp"
#include "../foundation/Array.h"
#include "../foundation/String.h"
#include "../effects/Shaders.h"
#include <gkcore.h>

/**
 * model animationが所有する再生状態の型。
 */
namespace gk::model
{
struct FModelPlayback;
}

/**
 * process全体で使うAPI状態と内部helper。
 */
namespace gk::detail
{

/**
 * 各model drawへ複製するinstance値。
 */
struct ModelTransform
{
    // このinstanceの公開handle。
    ModelHandle handle;
    // model world位置。
    Vec3 position;
    // modelのXYZ回転radian。
    Vec3 rotation;
    // model各軸の拡大率。
    Vec3 scale;
    // instance固有の再生状態。最終削除時に解放する。
    model::FModelPlayback* playback = nullptr;
};

/**
 * 安定したAPI shader handleを現在のbackend番号へ対応付ける。
 */
struct ShaderNativeRecord
{
    // アプリへ公開する安定handle。
    ShaderHandle publicHandle;
    // 現在のbackendが返したhandle。
    ShaderHandle backendHandle;
};

/**
 * 公開APIから共有するprocess状態。public APIにはcontext objectを持たせない。
 */
struct Context
{
    // 現在のwindow幅。
    uint32_t width = 1280;
    // 現在のwindow高さ。
    uint32_t height = 720;
    // 表示色深度。
    uint32_t colorDepth = 32;
    // Init前にwindow寸法が設定されたか。
    bool windowConfigured = true;
    // backend初期化済みか。
    bool initialized = false;
    // frameをBeginFrameしたままか。
    bool frameOpen = false;
    // 描画と入力を行うplatform backend。
    Backend* backend = nullptr;
    // 最後の公開API診断。
    String error;
    // 診断領域を確保できない場合の固定文字列。
    const char* emergencyError = nullptr;
    // 現在予約中の描画packet。
    FramePacket frame;
    // 現在のcamera位置。
    Vec3 cameraPosition{ 0.0f, 0.0f, -5.0f };
    // cameraの注視点。
    Vec3 cameraTarget{ 0.0f, 0.0f, 0.0f };
    // model handleごとのinstance値。
    Array<ModelTransform> modelTransforms;
    // 現在選択中custom shaderと定数。
    ShaderBindings shaders;
    // 次frameから使うpost-effect shader。
    ShaderHandle postEffectShader{};
    // public shader handleとbackend handleの対応表。
    Array<ShaderNativeRecord> nativeShaders;
    // 次に発行するshader handle番号。
    uint32_t nextShaderHandle = 1;
};

/**
 * processで共有する唯一のAPI状態を返す。
 */
Context& GetContext();

/**
 * 診断文字列を保存し、公開APIの失敗値を返す。
 */
int SetError(const char* message);

/**
 * API呼び出し成功後に診断文字列を消去する。
 */
void ClearError();

/**
 * graphics計算へ渡す前にvector全成分の有限性を確認する。
 */
bool IsFinite(Vec3 value);

/**
 * 有効なmodel handleの変換状態を返す。見つからない場合はnull。
 */
ModelTransform* FindModelTransform(ModelHandle handle);

/**
 * public handleに対応するbackend shader番号を返す。
 */
ShaderHandle FindBackendShader(ShaderHandle handle);

/**
 * shutdownまたはtest backend交換時にmodelごとの値を破棄する。
 */
void ClearModelTransforms();

/**
 * 現在のdraw listが保持する参照を解放する。
 */
void ClearFrameDraws();

} // gk::detail namespace終端

#endif
