#pragma once

#include <gkcore.h>

/**
 * Input queries over framework-supported key codes.
 */
namespace gk
{

bool IsKeyDown(Key key);

/**
 * 直近のイベント処理で押されたキーを、次の処理まで非消費で問い合わせる。
 */
bool WasKeyPressed(Key key);

} // namespace gk
