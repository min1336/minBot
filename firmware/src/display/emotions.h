#pragma once
#include <cstdint>

namespace minbot::display {

enum class Emotion : uint8_t {
    IDLE = 0,
    LISTENING,
    THINKING,
    SPEAKING,
    HAPPY,
    SAD,
    SURPRISED,
    SLEEPING,
    TILTED,
    COUNT
};

/// Initialize emotion state machine
void emotion_init();

/// Set target emotion (triggers transition animation)
void emotion_set(Emotion emotion);

/// Get current emotion
Emotion emotion_get();

/// Update emotion animation (call each frame with delta time ms)
void emotion_update(uint32_t dt_ms);

/// Set lip sync level (0-255, driven by audio volume)
void emotion_set_lip_sync(uint8_t level);

/// Notify audio volume for SPEAKING mouth animation
void emotion_set_audio_level(uint8_t level);

} // namespace minbot::display
