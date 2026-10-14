// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONCONTACTLINEARSYSTEM_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONCONTACTLINEARSYSTEM_H

#include <stdint.h>
#include "foundation/Array.h"
#include "foundation/FVector3d.h"
#include "foundation/String.h"

/**
 * 接触が骨方向の変化に受ける影響を保持し、補正量を同時に求める。
 */
namespace gk::model::animation
{

/**
 * 最大256方向・4096行・65536係数までを保持し、超過時は空のerrorで呼出し側へ戻す。
 */
class FSecondaryMotionContactLinearSystem
{
  public:
    /**
     * 空の接触線形系を作る。
     */
    FSecondaryMotionContactLinearSystem();

    /**
     * 方向配列を借用して行と係数を消去する。配列はSolve終了まで有効に保ち、256方向超過時はfalseを返す。
     */
    bool Reset(const gk::FVector3d* directions, uint32_t directionCount, gk::String& error);

    /**
     * penetrationを右辺とする新しい接触行を追加する。有限な非負値を指定し、4096行超過時はfalseを返す。
     */
    bool BeginRow(double penetration, gk::String& error);

    /**
     * 指定segmentの導関数を半径方向だけ除いて現在行へ追加する。行内の重複係数は合算し、総係数65536超過時はfalseを返す。
     */
    bool AddCoefficient(uint32_t segmentIndex, const gk::FVector3d& derivative, gk::String& error);

    /**
     * 行列を作らず減衰付き正規方程式を解く。収束成功時だけresultを置き換え、反復上限で未収束ならerrorを空にしてfalseを返す。
     */
    bool Solve(double relativeDamping, gk::Array<gk::FVector3d>& result, gk::String& error);

    /**
     * 最後のSolveで得た、方向ごとの3成分の対角和の最大値を返す。
     */
    double GetMaximumDiagonal() const;

    /**
     * 現在保持している接触行数を返す。
     */
    uint32_t GetRowCount() const;

  private:
    /**
     * 一つの接触行に加わる、半径成分を除いたsegment係数を持つ。
     */
    struct FEntry
    {
        // 係数が作用する借用方向の番号。
        uint32_t segmentIndex;
        // 借用方向の半径成分を除いた導関数。
        gk::FVector3d derivative;
    };

    /**
     * 連続した係数範囲と接触行の右辺を保持する。
     */
    struct FRow
    {
        // 係数配列内の先頭番号。
        uint32_t firstEntry;
        // この行が持つ係数数。
        uint32_t entryCount;
        // 線形化時の接触penetration。
        double penetration;
    };

    /**
     * 疎な接触行へ方向ごとの係数を掛け、正規行列の積を計算する。
     */
    bool MultiplyNormal(const gk::Array<gk::FVector3d>& input, double damping, gk::Array<gk::FVector3d>& output) const;

    // 呼出し側が所有し、Solve中に有効な単位方向配列。
    const gk::FVector3d* directions_;
    // 借用方向配列の要素数。
    uint32_t directionCount_;
    // 行を構成する疎な係数群。
    gk::Array<FEntry> entries_;
    // 係数範囲と右辺を持つ接触行群。
    gk::Array<FRow> rows_;
    // 最後のSolveで得た、方向ごとの対角和の最大値。
    double maximumDiagonal_;
};

}

#endif
