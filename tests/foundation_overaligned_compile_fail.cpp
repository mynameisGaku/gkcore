#include "../src/foundation/Array.h"

// Arrayが拒否すべき境界を超える値型。
struct alignas(64) FFoundationOverAlignedRecord {
    // 拒否対象の型を実体化するための値。
    unsigned char value[64];
};

/**
 * 型の配置だけを参照し、拒否用static_assert以外のcompile failureを避ける。
 */
int main() {
    return sizeof(gk::Array<FFoundationOverAlignedRecord>) == 0;
}
