// SPDX-License-Identifier: NOASSERTION
#include "examples/support/ModelSecondaryMotionPreview.h"

#include <stdio.h>
#include <string.h>

namespace
{

/**
 * 失敗した引数契約を名前付きで表示する。
 */
bool Require(bool condition, const char* contract)
{
    if (condition)
    {
        return true;
    }
    fprintf(stderr, "failed: %s\n", contract);
    return false;
}

/**
 * flag省略時は無効になり、--materialsの値はそのまま残る。
 */
bool TestAbsentFlagAndMaterialsValue()
{
    char* arguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--materials", "--secondary-vertex-contact", nullptr };
    const int originalCount = static_cast<int>(sizeof(arguments) / sizeof(arguments[0])) - 1;
    int count = originalCount;
    bool enabled = true;
    bool passed = Require(gk::examples::ParseSecondaryVertexContactOptions(count, arguments, enabled), "absent flag parses") && Require(!enabled, "absent flag disables vertex contacts") && Require(count == originalCount && strcmp(arguments[count - 1], "--secondary-vertex-contact") == 0 && arguments[count] == nullptr, "materials value matching the flag remains untouched");
    return passed;
}

/**
 * 1個のflagを除去し、ほかのoptionと値の順序を保つ。
 */
bool TestSingleFlagRemoval()
{
    char* arguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-vertex-contact", "--materials", "materials.txt", nullptr };
    int count = static_cast<int>(sizeof(arguments) / sizeof(arguments[0])) - 1;
    bool enabled = false;
    bool passed = Require(gk::examples::ParseSecondaryVertexContactOptions(count, arguments, enabled), "single flag parses") && Require(enabled, "single flag enables vertex contacts") && Require(count == 8 && strcmp(arguments[6], "--materials") == 0 && strcmp(arguments[7], "materials.txt") == 0 && arguments[count] == nullptr, "single flag is removed without disturbing remaining arguments");
    return passed;
}

/**
 * 重複flagは引数列と出力状態を変えずに拒否する。
 */
bool TestDuplicateFlagIsAtomic()
{
    char* arguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-vertex-contact", "--materials", "materials.txt", "--secondary-vertex-contact", nullptr };
    char* const original[] = { arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5], arguments[6], arguments[7], arguments[8], arguments[9], nullptr };
    int count = static_cast<int>(sizeof(arguments) / sizeof(arguments[0])) - 1;
    bool enabled = false;
    const bool rejected = !gk::examples::ParseSecondaryVertexContactOptions(count, arguments, enabled);
    const bool preserved = count == 10 && enabled == false && memcmp(arguments, original, sizeof(original)) == 0;
    return Require(rejected, "duplicate flag is rejected") && Require(preserved, "duplicate rejection preserves argv and enabled state");
}

}

/**
 * 頂点接触flagの引数処理契約を実行する。
 */
bool RunSecondaryMotionPreviewContracts()
{
    return TestAbsentFlagAndMaterialsValue() && TestSingleFlagRemoval() && TestDuplicateFlagIsAtomic();
}
