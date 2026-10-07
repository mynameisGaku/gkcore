#include "FKeyboardState.h"

namespace gk::platform
{

void FKeyboardState::BeginEventPoll()
{
    for (uint32_t virtualKey = 0; virtualKey < 256; ++virtualKey)
        pressed_[virtualKey] = 0;
}

void FKeyboardState::SetKeyDown(uint32_t virtualKey, bool isDown)
{
    if (virtualKey >= 256)
        return;
    if (!isDown)
    {
        down_[virtualKey] = 0;
        return;
    }
    if (!down_[virtualKey])
        pressed_[virtualKey] = 1;
    down_[virtualKey] = 1;
}

void FKeyboardState::RecordKeyDown(uint32_t virtualKey, bool repeated)
{
    if (virtualKey >= 256)
        return;
    down_[virtualKey] = 1;
    if (!repeated)
        pressed_[virtualKey] = 1;
}

void FKeyboardState::SetHeldState(uint32_t virtualKey, bool isDown)
{
    if (virtualKey < 256)
        down_[virtualKey] = isDown ? 1 : 0;
}

void FKeyboardState::Clear()
{
    for (uint32_t virtualKey = 0; virtualKey < 256; ++virtualKey)
    {
        down_[virtualKey] = 0;
        pressed_[virtualKey] = 0;
    }
}

bool FKeyboardState::WasKeyPressed(uint32_t virtualKey) const
{
    return virtualKey < 256 && pressed_[virtualKey] != 0;
}

bool FKeyboardState::IsKeyDown(uint32_t virtualKey) const
{
    return virtualKey < 256 && down_[virtualKey] != 0;
}

}
