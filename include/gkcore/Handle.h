// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_PUBLIC_HANDLE_H
#define GKCORE_PUBLIC_HANDLE_H

#include <stdint.h>

/**
 * 公開gkcore APIで使う型付きresource識別子。
 */
namespace gk
{

/**
 * 小さな型付き識別子。値0は無効値として予約する。
 */
template <class Tag> struct Handle
{
    // 0を無効値として使うresource番号。
    uint32_t value;

    /**
     * 無効handleを作る。
     */
    Handle() : value(0)
    {
    }
    /**
     * 0以外の識別番号からhandleを作る。
     */
    explicit Handle(uint32_t id) : value(id)
    {
    }
    /**
     * resourceを指す有効な番号か返す。
     */
    bool IsValid() const
    {
        return value != 0;
    }
    /**
     * 有効な番号ならtrueへ変換する。
     */
    explicit operator bool() const
    {
        return IsValid();
    }
    /**
     * 2つの識別子が等しいか調べる。
     */
    bool operator==(Handle other) const
    {
        return value == other.value;
    }
    /**
     * 2つの識別子が異なるか調べる。
     */
    bool operator!=(Handle other) const
    {
        return value != other.value;
    }
};

/**
 * 画像handleを他のresource番号と区別するtag。
 */
struct ImageTag;
/**
 * model handleを他のresource番号と区別するtag。
 */
struct ModelTag;
/**
 * shader handleを他のresource番号と区別するtag。
 */
struct ShaderTag;
/**
 * アニメーションデータのhandleを区別するタグ。
 */
struct ModelAnimationTag;
/**
 * 読み込んだ画像を識別する型付きhandle。
 */
using ImageHandle = Handle<ImageTag>;
/**
 * 読み込んだmodelを識別する型付きhandle。
 */
using ModelHandle = Handle<ModelTag>;
/**
 * 読み込んだcustom shaderを識別する型付きhandle。
 */
using ShaderHandle = Handle<ShaderTag>;
/**
 * 独立して読み込んだclip・骨格を識別するhandle。
 */
using ModelAnimationHandle = Handle<ModelAnimationTag>;

} // namespace gk

#endif
