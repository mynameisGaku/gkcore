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
}

#endif
