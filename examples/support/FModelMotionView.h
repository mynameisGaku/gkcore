// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_EXAMPLES_FMODELMOTIONVIEW_H
#define GKCORE_EXAMPLES_FMODELMOTIONVIEW_H

#include <gkcore.h>

/**
 * モデルviewerの動きに追従する表示補助。
 */
namespace gk::examples
{
/**
 * 腰の移動量を表示中心へ加える、sample所有の追従状態。モデルは所有しない。
 */
class FModelMotionView
{
  public:
    /**
     * 再生前の腰位置と表示中心を記録する。役割が一意でない、無効入力なら状態を保ちfalseを返す。
     * モデルを再読込した場合や役割表を変えた場合は、再生前に呼び直す。
     */
    bool Initialize(gk::ModelHandle model, gk::Vec3 center);
    /**
     * モデル空間の腰の移動を加えた現在の中心を返す。時刻とモデルの姿勢は変えない。
     * モデル解放・姿勢評価失敗・有限な中心を計算できない場合はfalseでoutputを保つ。
     */
    bool GetCenter(gk::Vec3& output) const;

  private:
    // sampleが生存させるモデルの借用handle。
    gk::ModelHandle model;
    // 初期化時のHips役割に対応した骨番号。
    uint32_t hipsBone = 0;
    // 再生前のモデル空間での腰位置。
    gk::Vec3 initialOrigin{};
    // 初期形状に合わせて指定した表示中心。
    gk::Vec3 initialCenter{};
};

/**
 * --follow-motionを位置引数から除く。同じflagの重複はfalseで引数とoutputを保つ。
 */
bool ParseMotionViewOptions(int& argc, char** argv, bool& followMotion);
}

#endif
