#pragma once

#include <gkcore.h>
#include "../internal/Backend.hpp"

/**
 * Application lifetime and frame submission entry points.
 */
namespace gk {

int Init();
void Shutdown();
int SetWindowSize(uint32_t width, uint32_t height);
bool ProcessEvents();
int BeginFrame();
int Present();
const char* GetLastErrorMessage();

} // namespace gk

/**
 * Internal ordered packet submission boundary for draw modules.
 */
namespace gk::detail {

/**
 * Captures current camera, layer, resource, and shader state in the open frame.
 */
int QueueDraw(DrawPacket& packet);

} // namespace gk::detail
