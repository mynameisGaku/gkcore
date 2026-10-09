// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FMODELDRAWCONSTANTS_H
#define GKCORE_RENDER_FMODELDRAWCONSTANTS_H

/**
 * 内蔵modelのGPU変換へ渡す値。
 */
namespace gk::render
{
/**
 * model変換・camera射影の13行を保持する。float4単位でshaderと配置を揃える。
 */
struct FModelDrawConstants
{
    // model回転の3行。scaleは別に適用する。
    float rotationRows[3][4]{};
    // worldへ移す位置。
    float translation[4]{};
    // 法線へ適用するscaleの逆数。
    float inverseScale[4]{};
    // 位置と接線へ適用するscale。
    float scale[4]{};
    // cameraの前・右・上の単位方向。
    float cameraForward[4]{};
    float cameraRight[4]{};
    float cameraUp[4]{};
    // cameraのworld位置。
    float cameraPosition[4]{};
    // 横/縦倍率と、奥行きをclipへ変換する係数。
    float projection[4]{};
    // 基本色へ追加で掛ける線形RGBA。
    float tint[4]{};
    // xは変換方法。0は投影済み入力、1はmodel空間入力。
    float flags[4]{};
};
static_assert(sizeof(FModelDrawConstants) == 208, "model draw constants must match thirteen float4 rows");
}
#endif
