#include <gkcore.h>
#include <gkcore/Handle.h>

int main() {
    gk::ImageHandle invalid;
    if (invalid) return 1;
    if (gk::ColorRGB(10, 20, 30) != 0x000a141eU) return 1;
    return gk::DrawString(0.0f, 0.0f, "link check", 0xffffffffU) == -1 ? 0 : 1;
}
