// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_AMODELANIMATIONSOURCE_H
#define GKCORE_MODEL_ANIMATION_AMODELANIMATIONSOURCE_H

#include "model/animation/EModelAnimationFormat.h"
#include "model/animation/FModelPose.h"
#include "model/animation/FModelGpuSkinningGeometry.h"
#include "model/animation/FModelSparsePoseGeometry.h"
#include "model/animation/FModelSparseVertexMap.h"
#include "model/animation/FModelSkeleton.h"
#include "resources/Resources.h"

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
    /**
     * sparse変形に使うcorner対応表を借用参照で返す。未対応形式はnullを返す。
     */
    virtual const Array<animation::FModelSparseVertexMap>* SparseVertexMap() const
    {
        return nullptr;
    }
    /**
     * sparse変形成功時に位置・法線と対応表が検証済みか返す。
     */
    virtual bool SparseDeformationIsValidated() const
    {
        return false;
    }
    /**
     * GPU skinningへ渡せる不変geometryを借用参照で返す。未対応形式はnullを返す。
     */
    virtual const animation::FModelGpuSkinningGeometry* GpuSkinningGeometry() const
    {
        return nullptr;
    }
    /**
     * 指定poseをGPU skinningで扱えるか確認する。未対応poseはfalseを返す。
     */
    virtual bool SupportsGpuSkinningPose(const animation::FModelPose&) const
    {
        return false;
    }
    /**
     * 共通poseからcluster順のaffine行列を作る。失敗時はoutputを変更しない。
     */
    virtual bool EvaluateGpuSkinningMatrices(const animation::FModelPose&, Array<animation::FModelGpuSkinningGeometry::FMatrix>&, String& error) const
    {
        error.Assign("GPU model skinning is unsupported for this source");
        return false;
    }
    /**
     * sparse変形とGPU用pose行列を同時に作る。失敗時は両outputを変更しない。
     */
    virtual bool DeformSparseWithGpuSkinningData(const animation::FModelPose&, animation::FModelSparsePoseGeometry&, Array<animation::FModelGpuSkinningGeometry::FMatrix>&, String& error) const
    {
        error.Assign("GPU model skinning data is unsupported for this source");
        return false;
    }
    /**
     * unique位置とnormal groupだけを計算する。失敗時はoutputを変更しない。
     */
    virtual bool DeformSparse(const animation::FModelPose&, animation::FModelSparsePoseGeometry&, String& error) const
    {
        error.Assign("sparse model deformation is unsupported for this source");
        return false;
    }
};
}

#endif
