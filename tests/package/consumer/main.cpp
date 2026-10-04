#include <Windows.h>
#include <gkcore.h>
#include <gkcore/Handle.h>

int main()
{
    gk::ImageHandle invalid;
    if (invalid)
        return 1;
    if (gk::ColorRGB(10, 20, 30) != 0x000a141eU)
        return 1;
    // 存在しない画像から無効handleが返るAPIをリンクする。
    const gk::ImageHandle missingImage = gk::LoadImage("__gkcore_missing_consumer_fixture__.png");
    if (missingImage)
        return 1;
    return gk::DrawString(0.0f, 0.0f, "link check", 0xffffffffU) == -1 ? 0 : 1;
}
