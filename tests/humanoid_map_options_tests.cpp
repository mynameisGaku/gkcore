// SPDX-License-Identifier: NOASSERTION
#include "examples/support/FHumanoidMapOptions.h"

#include <stdio.h>
#include <string.h>

namespace
{

/**
 * 失敗した契約を名前付きで表示する。
 */
bool Require(bool condition, const char* contract)
{
    if (condition)
        return true;
    fprintf(stderr, "failed: %s\n", contract);
    return false;
}

/**
 * 共有人型対応表optionの引数処理契約を確認する。
 */
bool TestOptionRemovalAndOrder()
{
    char modelPath[] = "model.map";
    char motionPath[] = "motion.map";
    char blendPath[] = "blend.map";
    char* arguments[] = { "viewer", "scene.glb", "external-blend", "walk.glb", "--motion-bones", motionPath, "run.glb", "--materials", "materials.txt", "--model-bones", modelPath, "--blend-bones", blendPath };
    int argumentCount = static_cast<int>(sizeof(arguments) / sizeof(arguments[0]));
    gk::examples::FHumanoidMapOptions options{};
    const char* error = nullptr;
    bool passed = Require(gk::examples::ParseHumanoidMapOptions(argumentCount, arguments, options, &error), "valid options parse") && Require(error == nullptr, "successful parse clears error") && Require(options.modelPath == modelPath && options.motionPath == motionPath && options.blendPath == blendPath, "paths borrow argv storage") && Require(argumentCount == 7 && arguments[argumentCount] == nullptr, "recognized option pairs are removed and argv is terminated") && Require(strcmp(arguments[0], "viewer") == 0 && strcmp(arguments[1], "scene.glb") == 0 && strcmp(arguments[2], "external-blend") == 0 && strcmp(arguments[3], "walk.glb") == 0 && strcmp(arguments[4], "run.glb") == 0 && strcmp(arguments[5], "--materials") == 0 && strcmp(arguments[6], "materials.txt") == 0, "remaining arguments preserve order and materials option") && Require(gk::examples::ValidateHumanoidMapOptions(options, true, true, &error), "all profiles valid for external blend");
    return passed;
}

/**
 * 重複、値なし、空pathを拒否し、失敗時のargvを保つ。
 */
bool TestInvalidOptions()
{
    char pathA[] = "first.map";
    char pathB[] = "second.map";
    char* duplicate[] = { "viewer", "scene.glb", "--model-bones", pathA, "--model-bones", pathB };
    int duplicateCount = 6;
    char* const duplicateOriginal[] = { duplicate[0], duplicate[1], duplicate[2], duplicate[3], duplicate[4], duplicate[5] };
    gk::examples::FHumanoidMapOptions options{};
    const char* error = nullptr;
    bool passed = Require(!gk::examples::ParseHumanoidMapOptions(duplicateCount, duplicate, options, &error), "duplicate option rejected") && Require(error && error[0], "duplicate has diagnostic") && Require(duplicateCount == 6 && memcmp(duplicate, duplicateOriginal, sizeof(duplicate)) == 0, "failed parse leaves argv unchanged");

    char* missing[] = { "viewer", "scene.glb", "--motion-bones" };
    int missingCount = 3;
    error = nullptr;
    passed = Require(!gk::examples::ParseHumanoidMapOptions(missingCount, missing, options, &error) && error && error[0], "missing value rejected") && passed;

    char* flagValue[] = { "viewer", "scene.glb", "--motion-bones", "--materials", "material.txt" };
    int flagCount = 5;
    error = nullptr;
    passed = Require(!gk::examples::ParseHumanoidMapOptions(flagCount, flagValue, options, &error) && flagCount == 5 && error && error[0], "following option is not consumed as a profile path") && passed;

    char emptyPath[] = "";
    char* empty[] = { "viewer", "scene.glb", "--blend-bones", emptyPath };
    int emptyCount = 4;
    error = nullptr;
    passed = Require(!gk::examples::ParseHumanoidMapOptions(emptyCount, empty, options, &error) && error && error[0], "empty value rejected") && passed;
    return passed;
}

/**
 * 対応する外部motion枠がないprofile指定を拒否する。
 */
bool TestModeCompatibility()
{
    char motionPath[] = "motion.map";
    char blendPath[] = "blend.map";
    gk::examples::FHumanoidMapOptions options{};
    options.motionPath = motionPath;
    const char* error = nullptr;
    bool passed = Require(!gk::examples::ValidateHumanoidMapOptions(options, false, false, &error) && error && error[0], "motion profile requires external motion");
    options.motionPath = nullptr;
    options.blendPath = blendPath;
    error = nullptr;
    passed = Require(!gk::examples::ValidateHumanoidMapOptions(options, true, false, &error) && error && error[0], "blend profile requires secondary external motion") && passed;
    options.blendPath = nullptr;
    options.modelPath = "model.map";
    error = nullptr;
    passed = Require(gk::examples::ValidateHumanoidMapOptions(options, false, false, &error), "model profile is valid without external motion") && passed;
    return passed;
}

}

/**
 * 共有人型対応表optionの契約を実行する。
 */
int main()
{
    return TestOptionRemovalAndOrder() && TestInvalidOptions() && TestModeCompatibility() ? 0 : 1;
}
