#include "platform/FKeyboardState.h"

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

bool TestPressAndReleaseWithinOnePoll()
{
    gk::platform::FKeyboardState keyboard;
    keyboard.BeginEventPoll();
    keyboard.SetKeyDown(0x20, true);
    keyboard.SetKeyDown(0x20, false);
    CHECK(keyboard.WasKeyPressed(0x20));
    CHECK(!keyboard.IsKeyDown(0x20));
    return true;
}

bool TestPulseLifetimeAndRepeatedQueries()
{
    gk::platform::FKeyboardState keyboard;
    keyboard.SetKeyDown(0x1b, true);
    CHECK(keyboard.WasKeyPressed(0x1b));
    CHECK(keyboard.WasKeyPressed(0x1b));
    CHECK(keyboard.IsKeyDown(0x1b));
    keyboard.BeginEventPoll();
    CHECK(!keyboard.WasKeyPressed(0x1b));
    CHECK(keyboard.IsKeyDown(0x1b));
    keyboard.SetKeyDown(0x1b, false);
    CHECK(!keyboard.IsKeyDown(0x1b));
    return true;
}

bool TestRepeatAndRepressTransitions()
{
    gk::platform::FKeyboardState keyboard;
    keyboard.SetKeyDown(0x20, true);
    keyboard.BeginEventPoll();
    keyboard.SetKeyDown(0x20, true);
    CHECK(!keyboard.WasKeyPressed(0x20));
    CHECK(keyboard.IsKeyDown(0x20));
    keyboard.SetKeyDown(0x20, false);
    keyboard.BeginEventPoll();
    keyboard.SetKeyDown(0x20, true);
    CHECK(keyboard.WasKeyPressed(0x20));
    CHECK(keyboard.IsKeyDown(0x20));
    return true;
}

bool TestSeedAndFocusClear()
{
    gk::platform::FKeyboardState keyboard;
    keyboard.SetHeldState(0x1b, true);
    CHECK(keyboard.IsKeyDown(0x1b));
    CHECK(!keyboard.WasKeyPressed(0x1b));
    keyboard.Clear();
    CHECK(!keyboard.IsKeyDown(0x1b));
    CHECK(!keyboard.WasKeyPressed(0x1b));
    keyboard.SetKeyDown(0x1b, true);
    keyboard.Clear();
    CHECK(!keyboard.IsKeyDown(0x1b));
    CHECK(!keyboard.WasKeyPressed(0x1b));
    return true;
}

bool TestSeededKeyDownMessageRaisesPulse()
{
    gk::platform::FKeyboardState keyboard;
    keyboard.SetHeldState(0x20, true);
    keyboard.RecordKeyDown(0x20, false);
    CHECK(keyboard.WasKeyPressed(0x20));
    CHECK(keyboard.IsKeyDown(0x20));
    return true;
}

bool TestRepeatAfterClearDoesNotRaisePulse()
{
    gk::platform::FKeyboardState keyboard;
    keyboard.Clear();
    keyboard.RecordKeyDown(0x20, true);
    CHECK(!keyboard.WasKeyPressed(0x20));
    CHECK(!keyboard.WasKeyPressed(0x20));
    CHECK(keyboard.IsKeyDown(0x20));
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

}

int main()
{
    const bool passed = TestPressAndReleaseWithinOnePoll() && TestPulseLifetimeAndRepeatedQueries() && TestRepeatAndRepressTransitions() && TestSeedAndFocusClear() && TestSeededKeyDownMessageRaisesPulse() && TestRepeatAfterClearDoesNotRaisePulse() && TestKeyRange();
    return passed ? 0 : 1;
}
