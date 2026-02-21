#pragma once
#include <cstdint>

namespace minbot::display {

struct Keyframe {
    const uint16_t* sprite_data;  // RGB565 pixel data
    uint16_t x, y, w, h;         // position and size on 240x240 canvas
    uint8_t  alpha;               // 0-255 opacity
};

struct Animation {
    const Keyframe* frames;
    uint8_t         frame_count;
    uint16_t        frame_duration_ms;
    bool            loop;
};

/// Initialize animator
void animator_init();

/// Play an animation (replaces current)
void animator_play(const Animation* anim);

/// Blend between two animations for state transitions
/// progress: 0.0 = fully from, 1.0 = fully to
void animator_blend(const Animation* from, const Animation* to, float progress);

/// Update animator state (call each frame with delta time ms)
void animator_update(uint32_t dt_ms);

/// Render current frame to LVGL canvas object
void animator_render(void* canvas);

} // namespace minbot::display
