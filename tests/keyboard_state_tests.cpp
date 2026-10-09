#include "platform/FKeyboardState.h"
#include "platform/WindowsWindow.h"

#include <stdio.h>

namespace
{

#define CHECK(condition)                                                             \
    do                                                                               \
    {                                                                                \
        if (!(condition))                                                            \
        {                                                                            \
            fprintf(stderr, "契約違反 %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                            \
        }                                                                            \
    } while (0)

/**
 * 256個すべてのkeyで同poll内の短い押下、pulse保持、次pollの消去を確認する。
 */
bool TestAllVirtualKeyPressReleasePulses()
{
    gk::platform::FKeyboardState keyboard;
    for (uint32_t pressedKey = 0; pressedKey < 256; ++pressedKey)
    {
        keyboard.Clear();
        keyboard.BeginEventPoll();
        keyboard.SetKeyDown(pressedKey, true);
        keyboard.SetKeyDown(pressedKey, false);
        for (uint32_t queriedKey = 0; queriedKey < 256; ++queriedKey)
        {
            const bool expectedPressed = queriedKey == pressedKey;
            CHECK(keyboard.WasKeyPressed(queriedKey) == expectedPressed);
            CHECK(keyboard.WasKeyPressed(queriedKey) == expectedPressed);
            CHECK(!keyboard.IsKeyDown(queriedKey));
        }
        keyboard.BeginEventPoll();
        for (uint32_t queriedKey = 0; queriedKey < 256; ++queriedKey)
        {
            CHECK(!keyboard.WasKeyPressed(queriedKey));
            CHECK(!keyboard.IsKeyDown(queriedKey));
        }
    }
    return true;
}

/**
 * 全keyでseed、repeat抑止、新down記録、focus喪失相当の消去を確認する。
 */
bool TestAllVirtualKeySeedRepeatAndClear()
{
    gk::platform::FKeyboardState keyboard;
    for (uint32_t seededKey = 0; seededKey < 256; ++seededKey)
    {
        keyboard.Clear();
        keyboard.SetHeldState(seededKey, true);
        for (uint32_t queriedKey = 0; queriedKey < 256; ++queriedKey)
        {
            CHECK(keyboard.IsKeyDown(queriedKey) == (queriedKey == seededKey));
            CHECK(!keyboard.WasKeyPressed(queriedKey));
        }

        keyboard.BeginEventPoll();
        keyboard.RecordKeyDown(seededKey, true);
        for (uint32_t queriedKey = 0; queriedKey < 256; ++queriedKey)
        {
            CHECK(keyboard.IsKeyDown(queriedKey) == (queriedKey == seededKey));
            CHECK(!keyboard.WasKeyPressed(queriedKey));
        }

        // bit 30が0のdown通知はseed済み状態でも新しい押下として記録する。
        keyboard.RecordKeyDown(seededKey, false);
        for (uint32_t queriedKey = 0; queriedKey < 256; ++queriedKey)
        {
            CHECK(keyboard.IsKeyDown(queriedKey) == (queriedKey == seededKey));
            CHECK(keyboard.WasKeyPressed(queriedKey) == (queriedKey == seededKey));
        }

        keyboard.Clear();
        for (uint32_t queriedKey = 0; queriedKey < 256; ++queriedKey)
        {
            CHECK(!keyboard.IsKeyDown(queriedKey));
            CHECK(!keyboard.WasKeyPressed(queriedKey));
        }

        keyboard.RecordKeyDown(seededKey, true);
        for (uint32_t queriedKey = 0; queriedKey < 256; ++queriedKey)
        {
            CHECK(keyboard.IsKeyDown(queriedKey) == (queriedKey == seededKey));
            CHECK(!keyboard.WasKeyPressed(queriedKey));
        }

        keyboard.Clear();
        for (uint32_t queriedKey = 0; queriedKey < 256; ++queriedKey)
        {
            CHECK(!keyboard.IsKeyDown(queriedKey));
            CHECK(!keyboard.WasKeyPressed(queriedKey));
        }
    }
    return true;
}

bool TestKeyRange()
{
    gk::platform::FKeyboardState keyboard;
    keyboard.SetKeyDown(0, true);
    keyboard.SetKeyDown(255, true);
    keyboard.SetKeyDown(256, true);
    keyboard.SetKeyDown(UINT32_MAX, true);
    keyboard.RecordKeyDown(256, false);
    keyboard.RecordKeyDown(UINT32_MAX, true);
    CHECK(keyboard.WasKeyPressed(0));
    CHECK(keyboard.IsKeyDown(0));
    CHECK(keyboard.WasKeyPressed(255));
    CHECK(keyboard.IsKeyDown(255));
    CHECK(!keyboard.WasKeyPressed(256));
    CHECK(!keyboard.IsKeyDown(256));
    CHECK(!keyboard.WasKeyPressed(UINT32_MAX));
    CHECK(!keyboard.IsKeyDown(UINT32_MAX));
    keyboard.Clear();
    keyboard.SetKeyDown(255, true);
    keyboard.SetKeyDown(256, false);
    CHECK(keyboard.WasKeyPressed(255));
    CHECK(keyboard.IsKeyDown(255));
    return true;
}

/**
 * capture用buildだけwindowを表示せず、通常buildの表示を維持する。
 */
bool TestWindowVisibilityPolicy()
{
    CHECK(!gk::platform::ShouldShowWindowForBuild(true));
    CHECK(gk::platform::ShouldShowWindowForBuild(false));
    return true;
}

}

int main()
{
    const bool passed = TestAllVirtualKeyPressReleasePulses() && TestAllVirtualKeySeedRepeatAndClear() && TestKeyRange() && TestWindowVisibilityPolicy();
    return passed ? 0 : 1;
}
