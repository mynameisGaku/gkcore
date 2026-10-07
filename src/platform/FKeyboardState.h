#ifndef GKCORE_PLATFORM_FKEYBOARDSTATE_H
#define GKCORE_PLATFORM_FKEYBOARDSTATE_H

#include <stdint.h>

/**
 * Win32 key eventを保持状態と押下pulseへ変換する。
 */
namespace gk::platform
{

/**
 * 256個のvirtual keyについて、down状態と未消費の押下を記録する。
 */
class FKeyboardState
{
  public:
    /**
     * 新しいevent pollを始め、前pollの押下pulseを消去する。
     */
    void BeginEventPoll();

    /**
     * keyの押下または解放を記録し、範囲外の値は無視する。
     */
    void SetKeyDown(uint32_t virtualKey, bool isDown);

    /**
     * Win32 keydown eventをrepeat flagに従って記録する。
     */
    void RecordKeyDown(uint32_t virtualKey, bool repeated);

    /**
     * 押下遷移を発生させずに現在down状態をseedする。
     */
    void SetHeldState(uint32_t virtualKey, bool isDown);

    /**
     * すべてのkeyのdown状態と押下pulseを消去する。
     */
    void Clear();

    /**
     * 直近poll中に押下遷移したか返し、読み取りでは消費しない。
     */
    bool WasKeyPressed(uint32_t virtualKey) const;

    /**
     * keyが現在down状態か返す単体検査用のquery。
     */
    bool IsKeyDown(uint32_t virtualKey) const;

  private:
    uint8_t down_[256]{};    // keyごとの保持状態。
    uint8_t pressed_[256]{}; // poll中に起きた押下遷移。
};

}

#endif
