// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_EXAMPLES_MODELSECONDARYMOTIONPREVIEW_H
#define GKCORE_EXAMPLES_MODELSECONDARYMOTIONPREVIEW_H

#include <gkcore.h>

/**
 * Viewerの揺れもの設定を読み込む補助関数。
 */
namespace gk::examples
{
/**
 * --secondary-motionのpathを取り出す。重複・値なしでは引数を保って失敗する。
 */
bool ParseSecondaryMotionOptions(int& argc, char** argv, const char*& path);
/**
 * 設定ファイルのbone名を解決し、各鎖を登録する。失敗時はViewerを終了する。
 */
bool ApplySecondaryMotionPreview(ModelHandle model, const char* path);
/**
 * --secondary-collidersのpathを取り出す。値なし・重複では引数を保って失敗する。
 */
bool ParseSecondaryColliderOptions(int& argc, char** argv, const char*& path);
/**
 * --secondary-vertex-contactを取り出す。重複では引数列を保って失敗する。
 */
bool ParseSecondaryVertexContactOptions(int& argc, char** argv, bool& enabled);
/**
 * 身体のbone名とローカル形状を設定ファイルから読み、全形状を一括登録する。
 */
bool ApplySecondaryColliderPreview(ModelHandle model, const char* path);
/**
 * --secondary-linksのpathを取り出す。欠落・重複では引数列を保つ。
 */
bool ParseSecondaryLinkOptions(int& argc, char** argv, const char*& path);
/**
 * 半径と2個のbone名から接続線分を読み、全設定を一括登録する。
 */
bool ApplySecondaryLinkPreview(ModelHandle model, const char* path);

}

#endif
