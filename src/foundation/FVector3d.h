// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_FOUNDATION_FVECTOR3D_H
#define GKCORE_FOUNDATION_FVECTOR3D_H

/**
 * 値計算に使う共通の座標型。
 */
namespace gk
{
/**
 * 計算途中の位置や方向を、倍精度の3成分で保持する値。
 */
struct FVector3d
{
    // XYZ順の位置または方向。
    double value[3]{};
};
}

#endif
