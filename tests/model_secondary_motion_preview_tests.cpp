// SPDX-License-Identifier: NOASSERTION
#include "examples/support/ModelSecondaryMotionPreview.h"
#include "examples/support/ModelSecondaryMotionDebugView.h"
#include "examples/support/ModelPingPongMotion.h"

#include <math.h>
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
 * clip両端で折り返し、巨大時刻も有限の範囲へ写す。
 */
bool TestPingPongMotionTimeContracts()
{
    double output = -7.0;
    if (!Require(gk::examples::EvaluatePingPongMotionTime(0.0, 2.0, output) && output == 0.0, "ping-pong starts at clip time zero"))
        return false;
    if (!Require(gk::examples::EvaluatePingPongMotionTime(1.25, 2.0, output) && output == 1.25, "ping-pong advances before the endpoint"))
        return false;
    if (!Require(gk::examples::EvaluatePingPongMotionTime(2.0, 2.0, output) && output == 2.0, "ping-pong reaches the endpoint"))
        return false;
    if (!Require(gk::examples::EvaluatePingPongMotionTime(2.75, 2.0, output) && output == 1.25, "ping-pong reverses after the endpoint"))
        return false;
    if (!Require(gk::examples::EvaluatePingPongMotionTime(4.0, 2.0, output) && output == 0.0, "ping-pong repeats after a full period"))
        return false;
    if (!Require(gk::examples::EvaluatePingPongMotionTime(1.25, 1.0, output) && output == 0.75, "each clip duration maps the shared phase independently"))
        return false;
    if (!Require(gk::examples::EvaluatePingPongMotionTime(1.0e300, 3.0, output) && output >= 0.0 && output <= 3.0, "large phase remains in clip range"))
        return false;
    output = -7.0;
    const bool invalidRejected = !gk::examples::EvaluatePingPongMotionTime(-1.0, 2.0, output) && !gk::examples::EvaluatePingPongMotionTime(1.0, 0.0, output) && !gk::examples::EvaluatePingPongMotionTime(INFINITY, 2.0, output) && !gk::examples::EvaluatePingPongMotionTime(1.0, INFINITY, output);
    return Require(invalidRejected, "invalid ping-pong input is rejected") && Require(output == -7.0, "invalid inputs preserve output");
}

/**
 * option省略・指定・値optionの通過・重複時のatomic性を確認する。
 */
bool TestPingPongMotionOptionContracts()
{
    char* absent[] = { "viewer", "model.fbx", "1", "0", "0", "0", "animate", "--materials", "--ping-pong-motion", nullptr };
    int count = 9;
    bool enabled = true;
    if (!Require(gk::examples::ParsePingPongMotionOptions(count, absent, enabled) && !enabled && count == 9 && strcmp(absent[8], "--ping-pong-motion") == 0, "materials value matching the flag remains untouched"))
        return false;

    char* selected[] = { "viewer", "model.fbx", "1", "0", "0", "0", "animate", "--ping-pong-motion", "--materials", "materials.txt", nullptr };
    count = 10;
    enabled = false;
    if (!Require(gk::examples::ParsePingPongMotionOptions(count, selected, enabled) && enabled && count == 9 && strcmp(selected[7], "--materials") == 0 && strcmp(selected[8], "materials.txt") == 0 && selected[count] == nullptr, "single flag is removed while argument-bearing options remain"))
        return false;

    char* duplicate[] = { "viewer", "model.fbx", "1", "0", "0", "0", "animate", "--ping-pong-motion", "--ping-pong-motion", nullptr };
    char* const original[] = { duplicate[0], duplicate[1], duplicate[2], duplicate[3], duplicate[4], duplicate[5], duplicate[6], duplicate[7], duplicate[8], nullptr };
    count = 9;
    enabled = false;
    if (!Require(!gk::examples::ParsePingPongMotionOptions(count, duplicate, enabled), "duplicate flag is rejected") || !Require(count == 9 && !enabled && memcmp(duplicate, original, sizeof(original)) == 0, "duplicate rejection preserves arguments and output"))
        return false;

    char* nullArgument[] = { "viewer", "model.fbx", "1", "0", "0", "0", "animate", nullptr, nullptr };
    count = 8;
    enabled = true;
    return Require(!gk::examples::ParsePingPongMotionOptions(count, nullArgument, enabled), "null argument is rejected") && Require(count == 8 && enabled && nullArgument[7] == nullptr, "null rejection preserves arguments and output");
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
 * 揺れ・身体形状pathを取り出してもmaterials値と頂点flagを誤読しない。
 */
bool TestMotionAndColliderPathsPreserveFlagLikeMaterialsValue()
{
    char* arguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--materials", "--secondary-vertex-contact", "--secondary-motion", "yumeka-bone-contact-motion.txt", "--secondary-colliders", "yumeka-bone-colliders.txt", nullptr };
    int count = static_cast<int>(sizeof(arguments) / sizeof(arguments[0])) - 1;
    const char* motionPath = nullptr;
    const char* colliderPath = nullptr;
    bool passed = Require(gk::examples::ParseSecondaryMotionOptions(count, arguments, motionPath), "bone-motion config path parses") && Require(motionPath && strcmp(motionPath, "yumeka-bone-contact-motion.txt") == 0, "bone-motion config path is returned");
    passed = Require(gk::examples::ParseSecondaryColliderOptions(count, arguments, colliderPath), "body-collider config path parses") && Require(colliderPath && strcmp(colliderPath, "yumeka-bone-colliders.txt") == 0, "body-collider config path is returned") && passed;
    bool enabled = true;
    passed = Require(gk::examples::ParseSecondaryVertexContactOptions(count, arguments, enabled), "remaining vertex flag parses") && Require(!enabled, "materials value matching the vertex flag is not enabled") && Require(count == 8 && strcmp(arguments[6], "--materials") == 0 && strcmp(arguments[7], "--secondary-vertex-contact") == 0 && arguments[count] == nullptr, "path removal preserves materials and its flag-like value") && passed;
    return passed;
}

/**
 * debug flagの省略・指定・重複とmaterials値を確認する。
 */
bool TestCollisionDebugFlagContracts()
{
    char* absentArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--materials", "--collision-debug", nullptr };
    int absentCount = static_cast<int>(sizeof(absentArguments) / sizeof(absentArguments[0])) - 1;
    bool enabled = true;
    const bool absentParsed = gk::examples::ParseSecondaryMotionDebugOptions(absentCount, absentArguments, enabled);
    if (!Require(absentParsed && !enabled && absentCount == 8 && strcmp(absentArguments[7], "--collision-debug") == 0 && absentArguments[absentCount] == nullptr, "materials value matching the debug flag remains a value"))
        return false;

    char* selectedArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--collision-debug", "--materials", "materials.txt", nullptr };
    int selectedCount = static_cast<int>(sizeof(selectedArguments) / sizeof(selectedArguments[0])) - 1;
    enabled = false;
    if (!Require(gk::examples::ParseSecondaryMotionDebugOptions(selectedCount, selectedArguments, enabled) && enabled && selectedCount == 8 && strcmp(selectedArguments[6], "--materials") == 0 && strcmp(selectedArguments[7], "materials.txt") == 0 && selectedArguments[selectedCount] == nullptr, "one debug flag is removed while materials stays intact"))
        return false;

    char* duplicateArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--collision-debug", "--materials", "materials.txt", "--collision-debug", nullptr };
    char* const originalArguments[] = { duplicateArguments[0], duplicateArguments[1], duplicateArguments[2], duplicateArguments[3], duplicateArguments[4], duplicateArguments[5], duplicateArguments[6], duplicateArguments[7], duplicateArguments[8], duplicateArguments[9], nullptr };
    int duplicateCount = static_cast<int>(sizeof(duplicateArguments) / sizeof(duplicateArguments[0])) - 1;
    enabled = false;
    const bool duplicateRejected = !gk::examples::ParseSecondaryMotionDebugOptions(duplicateCount, duplicateArguments, enabled);
    return Require(duplicateRejected, "duplicate debug flags are rejected") && Require(duplicateCount == 10 && !enabled && memcmp(duplicateArguments, originalArguments, sizeof(originalArguments)) == 0, "duplicate rejection preserves argv and output state");
}

/**
 * --secondary-linksの省略・path・重複とmaterials値を確認する。
 */
bool TestSecondaryLinkPathContracts()
{
    char* absentArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--materials", "--secondary-links", nullptr };
    int absentCount = static_cast<int>(sizeof(absentArguments) / sizeof(absentArguments[0])) - 1;
    const int originalAbsentCount = absentCount;
    const char* path = "unchanged";
    const bool absentParsed = gk::examples::ParseSecondaryLinkOptions(absentCount, absentArguments, path);
    if (!Require(absentParsed && path == nullptr && absentCount == originalAbsentCount && strcmp(absentArguments[7], "--secondary-links") == 0 && absentArguments[absentCount] == nullptr, "materials value matching the link flag stays a value"))
        return false;

    char* selectedArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-links", "links.txt", "--secondary-colliders", "body.txt", "--materials", "materials.txt", nullptr };
    int selectedCount = static_cast<int>(sizeof(selectedArguments) / sizeof(selectedArguments[0])) - 1;
    path = nullptr;
    if (!Require(gk::examples::ParseSecondaryLinkOptions(selectedCount, selectedArguments, path) && path && strcmp(path, "links.txt") == 0 && selectedCount == 10 && strcmp(selectedArguments[6], "--secondary-colliders") == 0 && strcmp(selectedArguments[7], "body.txt") == 0 && strcmp(selectedArguments[8], "--materials") == 0 && strcmp(selectedArguments[9], "materials.txt") == 0 && selectedArguments[selectedCount] == nullptr, "link path is removed while collider and material arguments remain"))
        return false;

    char* missingPathArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-links", nullptr };
    int missingPathCount = static_cast<int>(sizeof(missingPathArguments) / sizeof(missingPathArguments[0])) - 1;
    path = "unchanged";
    if (!Require(!gk::examples::ParseSecondaryLinkOptions(missingPathCount, missingPathArguments, path) && path && strcmp(path, "unchanged") == 0 && missingPathCount == 7 && strcmp(missingPathArguments[6], "--secondary-links") == 0 && missingPathArguments[missingPathCount] == nullptr, "missing link path is rejected without changing arguments or output"))
        return false;

    char* duplicateArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-links", "first.txt", "--materials", "materials.txt", "--secondary-links", "second.txt", nullptr };
    char* const originalDuplicateArguments[] = { duplicateArguments[0], duplicateArguments[1], duplicateArguments[2], duplicateArguments[3], duplicateArguments[4], duplicateArguments[5], duplicateArguments[6], duplicateArguments[7], duplicateArguments[8], duplicateArguments[9], duplicateArguments[10], duplicateArguments[11], nullptr };
    int duplicateCount = static_cast<int>(sizeof(duplicateArguments) / sizeof(duplicateArguments[0])) - 1;
    const int originalDuplicateCount = duplicateCount;
    path = "unchanged";
    const bool duplicateRejected = !gk::examples::ParseSecondaryLinkOptions(duplicateCount, duplicateArguments, path);
    return Require(duplicateRejected, "duplicate link paths are rejected") && Require(duplicateCount == originalDuplicateCount && path && strcmp(path, "unchanged") == 0 && memcmp(duplicateArguments, originalDuplicateArguments, sizeof(originalDuplicateArguments)) == 0, "duplicate rejection preserves argv and path output");
}

/**
 * 距離制約pathを取り出し、不正引数では元の列を保つ。
 */
bool TestSecondaryDistancePathContracts()
{
    char* absentArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--materials", "--secondary-distances", nullptr };
    int absentCount = static_cast<int>(sizeof(absentArguments) / sizeof(absentArguments[0])) - 1;
    const int originalAbsentCount = absentCount;
    const char* path = "unchanged";
    const bool absentParsed = gk::examples::ParseSecondaryDistanceOptions(absentCount, absentArguments, path);
    if (!Require(absentParsed && path == nullptr && absentCount == originalAbsentCount && strcmp(absentArguments[7], "--secondary-distances") == 0 && absentArguments[absentCount] == nullptr, "materials value matching the link flag stays a value"))
        return false;

    char* selectedArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-distances", "links.txt", "--secondary-colliders", "body.txt", "--materials", "materials.txt", nullptr };
    int selectedCount = static_cast<int>(sizeof(selectedArguments) / sizeof(selectedArguments[0])) - 1;
    path = nullptr;
    if (!Require(gk::examples::ParseSecondaryDistanceOptions(selectedCount, selectedArguments, path) && path && strcmp(path, "links.txt") == 0 && selectedCount == 10 && strcmp(selectedArguments[6], "--secondary-colliders") == 0 && strcmp(selectedArguments[7], "body.txt") == 0 && strcmp(selectedArguments[8], "--materials") == 0 && strcmp(selectedArguments[9], "materials.txt") == 0 && selectedArguments[selectedCount] == nullptr, "link path is removed while collider and material arguments remain"))
        return false;

    char* missingPathArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-distances", nullptr };
    int missingPathCount = static_cast<int>(sizeof(missingPathArguments) / sizeof(missingPathArguments[0])) - 1;
    path = "unchanged";
    if (!Require(!gk::examples::ParseSecondaryDistanceOptions(missingPathCount, missingPathArguments, path) && path && strcmp(path, "unchanged") == 0 && missingPathCount == 7 && strcmp(missingPathArguments[6], "--secondary-distances") == 0 && missingPathArguments[missingPathCount] == nullptr, "missing link path is rejected without changing arguments or output"))
        return false;

    char* duplicateArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-distances", "first.txt", "--materials", "materials.txt", "--secondary-distances", "second.txt", nullptr };
    char* const originalDuplicateArguments[] = { duplicateArguments[0], duplicateArguments[1], duplicateArguments[2], duplicateArguments[3], duplicateArguments[4], duplicateArguments[5], duplicateArguments[6], duplicateArguments[7], duplicateArguments[8], duplicateArguments[9], duplicateArguments[10], duplicateArguments[11], nullptr };
    int duplicateCount = static_cast<int>(sizeof(duplicateArguments) / sizeof(duplicateArguments[0])) - 1;
    const int originalDuplicateCount = duplicateCount;
    path = "unchanged";
    const bool duplicateRejected = !gk::examples::ParseSecondaryDistanceOptions(duplicateCount, duplicateArguments, path);
    return Require(duplicateRejected, "duplicate distance paths are rejected") && Require(duplicateCount == originalDuplicateCount && path && strcmp(path, "unchanged") == 0 && memcmp(duplicateArguments, originalDuplicateArguments, sizeof(originalDuplicateArguments)) == 0, "duplicate rejection preserves argv and path output");
}

/**
 * 接触線分pathの省略・指定・欠落・重複を確認する。
 */
bool TestSecondaryContactLinkPathContracts()
{
    char* absentArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--materials", "--secondary-contact-links", nullptr };
    int absentCount = static_cast<int>(sizeof(absentArguments) / sizeof(absentArguments[0])) - 1;
    const int originalAbsentCount = absentCount;
    const char* path = "unchanged";
    if (!Require(gk::examples::ParseSecondaryContactLinkOptions(absentCount, absentArguments, path) && path == nullptr && absentCount == originalAbsentCount && strcmp(absentArguments[7], "--secondary-contact-links") == 0 && absentArguments[absentCount] == nullptr, "materials value matching the contact-link flag remains untouched"))
        return false;

    char* selectedArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-contact-links", "contacts.txt", "--secondary-colliders", "body.txt", "--secondary-motion", "motion.txt", "--materials", "materials.txt", nullptr };
    int selectedCount = static_cast<int>(sizeof(selectedArguments) / sizeof(selectedArguments[0])) - 1;
    path = nullptr;
    if (!Require(gk::examples::ParseSecondaryContactLinkOptions(selectedCount, selectedArguments, path) && path && strcmp(path, "contacts.txt") == 0 && selectedCount == 12 && strcmp(selectedArguments[6], "--secondary-colliders") == 0 && strcmp(selectedArguments[7], "body.txt") == 0 && strcmp(selectedArguments[8], "--secondary-motion") == 0 && strcmp(selectedArguments[9], "motion.txt") == 0 && strcmp(selectedArguments[10], "--materials") == 0 && strcmp(selectedArguments[11], "materials.txt") == 0 && selectedArguments[selectedCount] == nullptr, "contact-link path is removed while prerequisites and materials remain"))
        return false;

    char* missingPathArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-contact-links", nullptr };
    int missingPathCount = static_cast<int>(sizeof(missingPathArguments) / sizeof(missingPathArguments[0])) - 1;
    path = "unchanged";
    if (!Require(!gk::examples::ParseSecondaryContactLinkOptions(missingPathCount, missingPathArguments, path) && path && strcmp(path, "unchanged") == 0 && missingPathCount == 7 && strcmp(missingPathArguments[6], "--secondary-contact-links") == 0 && missingPathArguments[missingPathCount] == nullptr, "missing contact-link path is rejected without changing arguments or output"))
        return false;

    char* duplicateArguments[] = { "viewer", "scene.fbx", "1", "0", "0", "0", "--secondary-contact-links", "first.txt", "--materials", "materials.txt", "--secondary-contact-links", "second.txt", nullptr };
    char* const originalDuplicateArguments[] = { duplicateArguments[0], duplicateArguments[1], duplicateArguments[2], duplicateArguments[3], duplicateArguments[4], duplicateArguments[5], duplicateArguments[6], duplicateArguments[7], duplicateArguments[8], duplicateArguments[9], duplicateArguments[10], duplicateArguments[11], nullptr };
    int duplicateCount = static_cast<int>(sizeof(duplicateArguments) / sizeof(duplicateArguments[0])) - 1;
    const int originalDuplicateCount = duplicateCount;
    path = "unchanged";
    const bool duplicateRejected = !gk::examples::ParseSecondaryContactLinkOptions(duplicateCount, duplicateArguments, path);
    return Require(duplicateRejected, "duplicate contact-link paths are rejected") && Require(duplicateCount == originalDuplicateCount && path && strcmp(path, "unchanged") == 0 && memcmp(duplicateArguments, originalDuplicateArguments, sizeof(originalDuplicateArguments)) == 0, "duplicate rejection preserves argv and path output");
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

/**
 * 複数bone接触球のpathを他optionから区別し、重複や値なしで状態を保つ。
 */
bool TestSecondaryContactPointPathContracts()
{
    char* args[] = { "viewer", "model.fbx", "1", "0", "0", "0", "--materials", "--secondary-contact-points", "--secondary-contact-points", "weighted.txt", nullptr };
    int count = 10;
    const char* path = nullptr;
    if (!Require(gk::examples::ParseSecondaryContactPointOptions(count, args, path) && path && strcmp(path, "weighted.txt") == 0 && count == 8 && strcmp(args[7], "--secondary-contact-points") == 0, "weighted contact path preserves a matching material value"))
    {
        return false;
    }
    char* duplicate[] = { "viewer", "model.fbx", "1", "0", "0", "0", "--secondary-contact-points", "a.txt", "--secondary-contact-points", "b.txt", nullptr };
    char* snapshot[11]{};
    memcpy(snapshot, duplicate, sizeof(duplicate));
    count = 10;
    path = "previous";
    if (!Require(!gk::examples::ParseSecondaryContactPointOptions(count, duplicate, path) && count == 10 && strcmp(path, "previous") == 0 && memcmp(snapshot, duplicate, sizeof(snapshot)) == 0, "duplicate weighted paths preserve argument and path state"))
    {
        return false;
    }
    char* missing[] = { "viewer", "model.fbx", "1", "0", "0", "0", "--secondary-contact-points", nullptr };
    count = 7;
    return Require(!gk::examples::ParseSecondaryContactPointOptions(count, missing, path) && count == 7 && strcmp(path, "previous") == 0, "missing weighted path preserves state");
}

}

/**
 * 頂点接触flagの引数処理契約を実行する。
 */
bool RunSecondaryMotionPreviewContracts()
{
    return TestPingPongMotionTimeContracts() && TestPingPongMotionOptionContracts() && TestAbsentFlagAndMaterialsValue() && TestSingleFlagRemoval() && TestDuplicateFlagIsAtomic() && TestMotionAndColliderPathsPreserveFlagLikeMaterialsValue() && TestCollisionDebugFlagContracts() && TestSecondaryLinkPathContracts() && TestSecondaryDistancePathContracts() && TestSecondaryContactLinkPathContracts() && TestSecondaryContactPointPathContracts();
}
