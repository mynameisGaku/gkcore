#include "Input.h"
#include "Context.h"

namespace gk {
namespace {
/**
 * Converts a supported framework key to its native key value.
 */
bool MapKey(Key key, uint32_t& platformKey) {
    const uint32_t value = static_cast<uint8_t>(key);
    const uint32_t digit0 = static_cast<uint8_t>(Key::Digit0);
    const uint32_t digit9 = static_cast<uint8_t>(Key::Digit9);
    const uint32_t letterA = static_cast<uint8_t>(Key::A);
    const uint32_t letterZ = static_cast<uint8_t>(Key::Z);
    if (value >= digit0 && value <= digit9) {
        platformKey = static_cast<uint32_t>('0') + value - digit0;
        return true;
    }
    if (value >= letterA && value <= letterZ) {
        platformKey = static_cast<uint32_t>('A') + value - letterA;
        return true;
    }
    switch (key) {
    case Key::Escape: platformKey = 0x1b; return true;
    case Key::ArrowLeft: platformKey = 0x25; return true;
    case Key::ArrowUp: platformKey = 0x26; return true;
    case Key::ArrowRight: platformKey = 0x27; return true;
    case Key::ArrowDown: platformKey = 0x28; return true;
    case Key::Space: platformKey = 0x20; return true;
    case Key::Enter: platformKey = 0x0d; return true;
    case Key::Tab: platformKey = 0x09; return true;
    case Key::Backspace: platformKey = 0x08; return true;
    case Key::Shift: platformKey = 0x10; return true;
    case Key::Control: platformKey = 0x11; return true;
    default: return false;
    }
}

/**
 * Converts a supported framework mouse button to its native button value.
 */
bool MapMouseButton(MouseButton button, uint32_t& platformButton) {
    switch (button) {
    case MouseButton::Left: platformButton = 0x01; return true;
    case MouseButton::Right: platformButton = 0x02; return true;
    case MouseButton::Middle: platformButton = 0x04; return true;
    default: return false;
    }
}
}

bool IsKeyDown(Key key) {
    detail::Context& context = detail::GetContext();
    if (!context.initialized || !context.backend) {
        detail::SetError("framework is not initialized");
        return false;
    }
    uint32_t platformKey = 0;
    if (!MapKey(key, platformKey)) {
        detail::SetError("unsupported key code");
        return false;
    }
    if (!context.backend->HasInputFocus()) {
        detail::ClearError();
        return false;
    }
    detail::ClearError();
    return context.backend->IsKeyDown(platformKey);
}

bool IsMouseButtonDown(MouseButton button) {
    detail::Context& context = detail::GetContext();
    if (!context.initialized || !context.backend) {
        detail::SetError("framework is not initialized");
        return false;
    }
    uint32_t platformButton = 0;
    if (!MapMouseButton(button, platformButton)) {
        detail::SetError("unsupported mouse button");
        return false;
    }
    if (!context.backend->HasInputFocus()) {
        detail::ClearError();
        return false;
    }
    if (!context.backend->SupportsMouseInput()) {
        detail::SetError("mouse input is unavailable in this backend");
        return false;
    }
    detail::ClearError();
    return context.backend->IsMouseButtonDown(platformButton);
}

bool GetMousePosition(int32_t& x, int32_t& y) {
    x = 0;
    y = 0;
    detail::Context& context = detail::GetContext();
    if (!context.initialized || !context.backend) {
        detail::SetError("framework is not initialized");
        return false;
    }
    if (!context.backend->HasInputFocus()) {
        detail::ClearError();
        return false;
    }
    if (!context.backend->SupportsMouseInput()) {
        detail::SetError("mouse input is unavailable in this backend");
        return false;
    }
    if (!context.backend->GetMousePosition(x, y)) {
        x = 0;
        y = 0;
        detail::SetError("mouse position is unavailable");
        return false;
    }
    detail::ClearError();
    return true;
}

} // namespace gk
