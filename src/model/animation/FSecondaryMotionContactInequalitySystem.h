// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONCONTACTINEQUALITYSYSTEM_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONCONTACTINEQUALITYSYSTEM_H

#include "foundation/Array.h"
#include "foundation/String.h"
#include <stdint.h>

/**
 * 接触条件を同時に扱う内部の二次計画を定義する。
 */
namespace gk::model::animation
{
/**
 * 正定値の目的行列と疎な下限制約を所有し、最大65変数・4096行・65536係数で補正を求める。
 */
class FSecondaryMotionContactInequalitySystem
{
  public:
    /**
     * 変数を持たない空の系を作る。
     */
    FSecondaryMotionContactInequalitySystem();
    /**
     * 1～65変数へ初期化し、目的行列を単位行列に戻す。上限超過は空error、入力・確保失敗は理由付きで返す。
     */
    bool Reset(uint32_t count, gk::String& error);
    /**
     * 制約行だけを消去し、目的勾配と行列は保持する。
     */
    void ClearRows();
    /**
     * 有限な目的勾配を複写する。配列数が不一致または確保失敗の場合は既存値を保持する。
     */
    bool SetObjective(const double* gradient, uint32_t count, gk::String& error);
    /**
     * 有限な符号付き下限を持つ行を始める。行数上限では空errorでfalseを返す。
     */
    bool BeginRow(double lowerBound, gk::String& error);
    /**
     * 現在行の係数を合算する。変数範囲・非有限値は理由付き、係数数上限は空errorで失敗する。
     */
    bool AddCoefficient(uint32_t variableIndex, double coefficient, gk::String& error);
    /**
     * 条件を満たす初期値から有限回のactive-set法で解き、成功した場合だけ解と全行乗数を置き換える。
     * 初期値が制約を満たさない場合や未収束・作業上限では空error、数値・確保失敗では理由を返す。
     */
    bool Solve(const double* feasibleStart, uint32_t count, gk::Array<double>& result, gk::Array<double>& multipliers, gk::String& error);
    /**
     * 変位とLagrange勾配差からPowell減衰BFGSで行列を更新し、正定値を維持できない失敗では既存行列を保つ。
     */
    bool UpdateHessian(const double* step, const double* lagrangianGradientDifference, uint32_t count, gk::String& error);

  private:
    /**
     * 一つのscalar変数に作用する有限係数を保持する。
     */
    struct FEntry
    {
        // 係数が作用する変数番号。
        uint32_t variable;
        // 同じ行・変数について合算した係数。
        double value;
    };
    /**
     * 連続する疎係数範囲と符号付き下限を保持する。
     */
    struct FRow
    {
        // 係数配列の開始位置。
        uint32_t firstEntry;
        // この行の係数数。
        uint32_t entryCount;
        // 行と変数の内積に要求する下限。
        double lowerBound;
    };
    // 現在のscalar変数数。0は初期化前。
    uint32_t variableCount;
    // 行優先の対称な正定値目的行列。
    gk::Array<double> hessian;
    // 線形目的項の係数。
    gk::Array<double> objective;
    // 制約の疎な係数群。
    gk::Array<FEntry> entries;
    // 係数範囲と下限を持つ制約行群。
    gk::Array<FRow> rows;
};
}

#endif
