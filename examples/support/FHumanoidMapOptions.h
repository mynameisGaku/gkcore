// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_EXAMPLES_FHUMANOIDMAPOPTIONS_H
#define GKCORE_EXAMPLES_FHUMANOIDMAPOPTIONS_H

/**
 * Viewerと実モデルcaptureで共有する起動option。
 */
namespace gk::examples
{

/**
 * argvから借りた人型bone対応表pathを保持する値。
 */
struct FHumanoidMapOptions
{
    // model骨へ適用する対応表path。未指定ならnull。
    const char* modelPath = nullptr;
    // 主external motionへ適用する対応表path。未指定ならnull。
    const char* motionPath = nullptr;
    // secondary external motionへ適用する対応表path。未指定ならnull。
    const char* blendPath = nullptr;
};

/**
 * 既知の人型対応表optionをargvから除去し、pathをargv内の文字列として保持する。
 * 重複、値なし、空値を検出した場合はargvとoptionsを変更せずfalseを返す。
 */
bool ParseHumanoidMapOptions(int& argc, char** argv, FHumanoidMapOptions& options, const char** error);

/**
 * 人型対応表optionが指定modeの外部motion枠に適用できるか検査する。
 */
bool ValidateHumanoidMapOptions(const FHumanoidMapOptions& options, bool hasExternalMotion, bool hasExternalBlend, const char** error);

}

#endif
