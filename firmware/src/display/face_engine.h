#pragma once
#include <cstdint>

namespace minbot::display {

/// Initialize GC9A01 SPI display with LVGL
/// Returns ESP_OK on success
int face_init();

/// Update display (call from display task loop, ~30fps)
void face_update();

/// Set backlight brightness (0-255)
void face_set_brightness(uint8_t brightness);

/// Deinitialize display
void face_deinit();

} // namespace minbot::display
