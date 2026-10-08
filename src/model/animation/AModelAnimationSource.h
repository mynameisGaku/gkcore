// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_AMODELANIMATIONSOURCE_H
#define GKCORE_MODEL_ANIMATION_AMODELANIMATIONSOURCE_H

#include "EModelAnimationFormat.h"
#include "FModelPose.h"
#include "FModelSkeleton.h"
#include "../../resources/Resources.h"

/**
 * formatごとのanimation source共通interface。
 */
namespace gk::model
{
/**
 * 元のモデル・clipを所有し、共通姿勢から独立した形状を作る。
 */
class AModelAnimationSource
{
  public:
    /**
     * 形式固有の解析情報と保持参照を解放する。
     */
    virtual ~AModelAnimationSource() = default;
    /**
     * 入力形式を返す。
     */
    virtual EModelAnimationFormat Format() const = 0;
    /**
     * 親が先に並ぶ骨格と、初期morph係数を借用参照で返す。
     */
    virtual const animation::FModelSkeleton& Skeleton() const = 0;
    /**
     * 指定ボーン名を借用参照で返す。範囲外はnullを返す。
     */
    virtual const char* BoneName(uint32_t bone) const = 0;
    /**
     * ボーンの姿勢を変更できるか返す。形式側で固定した変換はfalse。
     */
    virtual bool BoneWritable(uint32_t bone) const
    {
        return bone < Skeleton().parents.Count();
    }
    /**
     * morph係数を対応付ける名前を借用参照で返す。未命名・範囲外はnull。
     */
    virtual const char* MorphName(uint32_t morph) const = 0;
    /**
     * 読み込み済みclipの数を返す。
     */
    virtual uint32_t ClipCount() const = 0;
    /**
     * clip名を借用参照で返す。範囲外ではnullを返す。
     */
    virtual const char* ClipName(uint32_t clip) const = 0;
    /**
     * clipの長さを秒で返す。範囲外では負の値を返す。
     */
    virtual double ClipDuration(uint32_t clip) const = 0;
    /**
     * clip先頭からの有限時刻を共通姿勢へ評価する。時刻は両端へ制限する。
     * 失敗時はoutputを変更しない。
     */
    virtual bool Sample(uint32_t clip, double seconds, animation::FModelPose& output, String& error) const = 0;
    /**
     * 共通姿勢でoutputの頂点位置・法線・接線を更新する。
     * outputは元モデルの独立したコピーで、材質・indexは変更しない。
     * skin・morph・node変換を反映し、失敗時はoutputを変更しない。
     */
    virtual bool Deform(const animation::FModelPose& pose, detail::ModelResource& output, String& error) const = 0;
};
}

#endif
