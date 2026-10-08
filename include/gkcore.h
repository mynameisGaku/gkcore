// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_PUBLIC_GKCORE_H
#define GKCORE_PUBLIC_GKCORE_H

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
 * ウィンドウ、描画、入力に使う公開APIと値型。
 */
namespace gk
{

/**
 * 位置、回転、方向、拡大率に使う3成分の値。
 */
struct Vec3
{
    // X軸の値。
    float x, y, z;
};

/**
 * シェーダー定数などに使う4つの浮動小数点値。
 */
struct Float4
{
    // shaderへ渡す順序付き4値。
    float x, y, z, w;
};

/**
 * Escape、矢印、編集キー、Shift/Control、数字、英字のキー。
 */
enum class Key : uint8_t
{
    // Escape。
    Escape = 1,
    // 左矢印。
    ArrowLeft,
    // 上矢印。
    ArrowUp,
    // 右矢印。
    ArrowRight,
    // 下矢印。
    ArrowDown,
    // Space。
    Space,
    // Enter。
    Enter,
    // Tab。
    Tab,
    // Backspace。
    Backspace,
    // 数字0。
    Digit0,
    // 数字1。
    Digit1,
    // 数字2。
    Digit2,
    // 数字3。
    Digit3,
    // 数字4。
    Digit4,
    // 数字5。
    Digit5,
    // 数字6。
    Digit6,
    // 数字7。
    Digit7,
    // 数字8。
    Digit8,
    // 数字9。
    Digit9,
    // 英字A。
    A,
    // 英字B。
    B,
    // 英字C。
    C,
    // 英字D。
    D,
    // 英字E。
    E,
    // 英字F。
    F,
    // 英字G。
    G,
    // 英字H。
    H,
    // 英字I。
    I,
    // 英字J。
    J,
    // 英字K。
    K,
    // 英字L。
    L,
    // 英字M。
    M,
    // 英字N。
    N,
    // 英字O。
    O,
    // 英字P。
    P,
    // 英字Q。
    Q,
    // 英字R。
    R,
    // 英字S。
    S,
    // 英字T。
    T,
    // 英字U。
    U,
    // 英字V。
    V,
    // 英字W。
    W,
    // 英字X。
    X,
    // 英字Y。
    Y,
    // 英字Z。
    Z,
    // Shift。
    Shift,
    // Control。
    Control
};

/**
 * 入力問い合わせAPIで扱うマウスボタン。
 */
enum class MouseButton : uint8_t
{
    // 左ボタン。
    Left = 0,
    // 右ボタン。
    Right,
    // 中ボタン。
    Middle
};

/**
 * 描画予約へシーン用の後処理を適用するか選ぶ層。
 */
enum class DrawLayer : uint8_t
{
    // 後処理対象のシーン。
    Scene = 0,
    // 後処理を適用しないUI。
    UI = 1
};

/**
 * ウィンドウと描画機能を初期化する。成功時は0、失敗時は-1。
 */
GKCORE_API int Init();

/**
 * 描画機能を終了し、登録済みのresource handleをすべて無効にする。
 */
GKCORE_API void Shutdown();

/**
 * Init前にウィンドウ寸法を設定する。不正な寸法では-1を返す。
 */
GKCORE_API int SetWindowSize(uint32_t width, uint32_t height);

/**
 * OSイベントを処理し、アプリを続ける間はtrueを返す。
 */
GKCORE_API bool ProcessEvents();

/**
 * 描画frameを開始する。成功時は0、失敗時は-1を返す。
 */
GKCORE_API int BeginFrame();

/**
 * 開いているframeを描画して表示する。成功時は0、失敗時は-1を返す。
 */
GKCORE_API int Present();

/**
 * 0から255へ制限したRGB値を0xRRGGBB形式へまとめる。
 */
GKCORE_API uint32_t ColorRGB(int red, int green, int blue);

/**
 * 指定キーが押されているか返す。アプリのメインthreadから問い合わせる。
 * windowにfocusがない場合はfalseを返す。
 */
GKCORE_API bool IsKeyDown(Key key);

/**
 * 直近のProcessEventsでキーが押された場合にtrueを返す。
 * アプリのメインスレッドから問い合わせる。
 * 同じ処理内で押して離したキーも保持し、次のProcessEventsで消去する。
 * 問い合わせでは値を消費しない。非focus、未初期化、不正なキーはfalseを返す。
 */
GKCORE_API bool WasKeyPressed(Key key);

/**
 * windowにfocusがある間、指定ボタンが押されているか返す。
 * アプリのメインthreadから問い合わせる。backend非対応時はfalseと診断を返す。
 */
GKCORE_API bool IsMouseButtonDown(MouseButton button);

/**
 * window内のマウス座標をアプリのメインthreadから読み取る。
 * 非focus、未初期化、座標取得非対応時はfalseを返し、両座標を0にする。
 */
GKCORE_API bool GetMousePosition(int32_t& x, int32_t& y);

/**
 * 画像を読み込む。失敗時は無効handleを返す。
 */
GKCORE_API ImageHandle LoadImage(const char* utf8Path);

/**
 * 画像を左上pixel座標へ描画予約する。
 */
GKCORE_API int DrawImage(ImageHandle image, float x, float y, bool alphaBlend = true);

/**
 * 画像を中心座標へ描画予約する。角度はradian、拡大率は正の値を使う。
 */
GKCORE_API int DrawImageRotated(ImageHandle image, float centerX, float centerY, float scale, float angleRadians, bool alphaBlend = true);

/**
 * 画像handleを解放する。予約済み描画はPresentまで画素を保持する。
 */
GKCORE_API int DeleteImage(ImageHandle image);

/**
 * UTF-8 pathから静的OBJ、GLB 2.0、FBXを読み込む。失敗時は無効handleを返す。
 */
GKCORE_API ModelHandle LoadModel(const char* utf8Path);

/**
 * 呼び出し時点の変換値でモデル描画を予約する。
 */
GKCORE_API int DrawModel(ModelHandle model);

/**
 * 以後のDrawModelに使うmodelのworld位置を設定する。
 */
GKCORE_API int SetModelPosition(ModelHandle model, Vec3 position);

/**
 * 以後のDrawModelに使うXYZ Euler回転をradianで設定する。
 */
GKCORE_API int SetModelRotation(ModelHandle model, Vec3 rotationRadians);

/**
 * 以後のDrawModelに使う各軸の0以外の拡大率を設定する。
 */
GKCORE_API int SetModelScale(ModelHandle model, Vec3 scale);

/**
 * model handleを解放する。予約済み描画はPresentまで形状を保持する。
 */
GKCORE_API int DeleteModel(ModelHandle model);

/**
 * Y-up座標系で有限なcamera位置と注視点を設定する。
 */
GKCORE_API int SetCamera(Vec3 position, Vec3 target);

/**
 * 選択中の層へ画面座標の矩形を予約する。filled=falseでは内側へ1pixelの線を描く。
 */
GKCORE_API int DrawRect(float x, float y, float width, float height, uint32_t color, bool filled = true);

/**
 * 矩形の内側へ画面座標の枠線を予約する。太さは有限かつ正の値を使い、
 * 短辺の半分以上なら矩形全体を塗る。
 */
GKCORE_API int DrawRectOutline(float x, float y, float width, float height, uint32_t color, float thickness = 1.0f);

/**
 * OS標準fontを使ってUTF-8文字列を予約する。初期pixel寸法は24、指定範囲は1から256。
 * 同じ色と寸法の文字列はmain thread上の上限付き画像cacheを共有する。
 * 現在の描画層に属し、画像はPresentまで保持される。
 */
GKCORE_API int DrawString(float x, float y, const char* utf8Text, uint32_t color, uint32_t pixelSize = 24);

/**
 * 3D描画用のworld座標三角形を予約する。
 */
GKCORE_API int DrawTriangle3D(Vec3 a, Vec3 b, Vec3 c, uint32_t color, bool filled = true);

/**
 * 最後に記録した診断文字列へのpointerを返す。
 */
GKCORE_API const char* GetLastErrorMessage();

/**
 * 以後のframeでbloomを使うか設定する。
 */
GKCORE_API int SetBloomEnabled(bool enabled);

/**
 * bloom強度を範囲[0, 4]の有限値へ設定する。
 */
GKCORE_API int SetBloomIntensity(float intensity);

/**
 * exposureを範囲(0, 16]の有限値へ設定する。
 */
GKCORE_API int SetExposure(float exposure);

/**
 * sceneの彩度倍率を[0, 2]の有限値へ設定する。1は彩度を保つ。
 */
GKCORE_API int SetSaturation(float factor);

/**
 * sceneのcontrast倍率を[0, 2]の有限値へ設定する。1はcontrastを保つ。
 */
GKCORE_API int SetContrast(float factor);

/**
 * 以後のframeでFXAAを使うか設定する。初期状態では有効。
 */
GKCORE_API int SetFxaaEnabled(bool enabled);

/**
 * 以後のframeでtone mappingを使うか設定する。
 */
GKCORE_API int SetToneMappingEnabled(bool enabled);

/**
 * 以後の描画命令をsceneまたはUIのどちらへ送るか設定する。
 */
GKCORE_API int SetDrawLayer(DrawLayer layer);

/**
 * 内蔵modelの環境光を[0, 4]の有限値へ設定する。初期値は0.2。
 * BeginFrameで値を読み取り、Shutdownで初期値へ戻す。
 */
GKCORE_API int SetAmbientLight(float intensity);

/**
 * 内蔵modelの方向光を設定する。方向は有限な非zeroのworld移動方向を使い、保存時に正規化する。
 * 強度は[0, 16]、初期値は3。BeginFrameで読み取り、Shutdownで初期値へ戻す。
 */
GKCORE_API int SetDirectionalLight(Vec3 direction, float intensity = 3.0f);

/**
 * compile済みpixel shaderを読み込む。失敗時は無効handleを返す。
 */
GKCORE_API ShaderHandle LoadPixelShader(const char* compiledPath);

/**
 * 使用するcustom shaderを選ぶ。無効handleでは内蔵shaderを使う。
 */
GKCORE_API int SetPixelShader(ShaderHandle shader);

/**
 * 後処理passで使うpixel shaderを選ぶ。無効handleでは後処理shaderを使わない。
 * Init後に呼ぶ。選択はBeginFrameで複製し、古いhandleなら現在値を保つ。
 */
GKCORE_API int SetPostEffectShader(ShaderHandle shader);

/**
 * shader handleを解放する。open frameで使用中は失敗するためPresent後に呼ぶ。
 */
GKCORE_API int DeleteShader(ShaderHandle shader);

/**
 * custom shader定数slotへ有限な4成分値を設定する。
 */
GKCORE_API int SetShaderFloat4(ShaderHandle shader, uint32_t slot, Float4 value);

}

#include <gkcore/ModelAnimation.h>

#endif
