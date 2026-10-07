#include "TextureSampler.h"

/**
 * texture samplerの小さな値検査とindex計算。
 */
namespace gk::detail
{

/**
 * address modeとfilter値が固定36状態の範囲内か調べる。
 */
bool IsTextureSamplerValid(const FTextureSampler& sampler)
{
    // 表の範囲確認に使うU軸の値。
    const uint32_t addressU = static_cast<uint32_t>(sampler.addressU);
    // 表の範囲確認に使うV軸の値。
    const uint32_t addressV = static_cast<uint32_t>(sampler.addressV);
    // min filterの表内値。
    const uint32_t minFilter = static_cast<uint32_t>(sampler.minFilter);
    // mag filterの表内値。
    const uint32_t magFilter = static_cast<uint32_t>(sampler.magFilter);
    return addressU < 3 && addressV < 3 && minFilter < 2 && magFilter < 2;
}

/**
 * 2つのsampler値について、各軸とfilterの設定が一致するか調べる。
 */
bool AreTextureSamplersEqual(const FTextureSampler& left, const FTextureSampler& right)
{
    return left.addressU == right.addressU && left.addressV == right.addressV && left.minFilter == right.minFilter && left.magFilter == right.magFilter;
}

/**
 * samplerを固定36状態の表indexへ変換し、無効値では出力を維持する。
 */
bool GetTextureSamplerIndex(const FTextureSampler& sampler, uint32_t& index)
{
    if (!IsTextureSamplerValid(sampler))
        return false;
    // 表index計算に使うU軸の値。
    const uint32_t addressU = static_cast<uint32_t>(sampler.addressU);
    // 表index計算に使うV軸の値。
    const uint32_t addressV = static_cast<uint32_t>(sampler.addressV);
    // 表index計算に使うmin filter値。
    const uint32_t minFilter = static_cast<uint32_t>(sampler.minFilter);
    // 表index計算に使うmag filter値。
    const uint32_t magFilter = static_cast<uint32_t>(sampler.magFilter);
    // U、V、min、magの順で固定表の位置へ畳み込む。
    index = (((addressU * 3 + addressV) * 2 + minFilter) * 2 + magFilter);
    return true;
}

// namespace gk::detail
}
