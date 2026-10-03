#include "TextLayout.h"

namespace gk::detail {
namespace {
// Keep the input bound aligned with ValidateText before system-font conversion.
const uint32_t kMaximumInputBytes = 4096;
const char kTabReplacement[] = "    ";
}

bool ExpandTextTabs(const char* utf8Text, String& expandedText, String& error) {
    if (!utf8Text) {
        error.Assign("text is null");
        return false;
    }

    uint32_t length = 0;
    while (length <= kMaximumInputBytes && utf8Text[length]) ++length;
    if (length > kMaximumInputBytes) {
        error.Assign("text exceeds the supported byte limit");
        return false;
    }

    String replacement;
    uint32_t runStart = 0;
    for (uint32_t i = 0; i < length; ++i) {
        if (utf8Text[i] != '\t') continue;
        if (!replacement.Append(utf8Text + runStart, i - runStart) ||
            !replacement.Append(kTabReplacement, 4)) {
            error.Assign("not enough memory to expand text tabs");
            return false;
        }
        runStart = i + 1;
    }
    if (!replacement.Append(utf8Text + runStart, length - runStart)) {
        error.Assign("not enough memory to expand text tabs");
        return false;
    }

    expandedText.MoveFrom(replacement);
    error.Clear();
    return true;
}

} // namespace gk::detail
