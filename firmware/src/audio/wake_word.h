#pragma once
#include <functional>

namespace minbot::audio {

using WakeWordCallback = std::function<void()>;

/// Initialize ESP-SR WakeNet for wake word detection
int wake_word_init();

/// Feed audio frame for wake word detection
/// @param pcm_data 16-bit PCM audio
/// @param samples Number of samples
/// @return true if wake word detected
bool wake_word_detect(const int16_t* pcm_data, int samples);

/// Register callback for wake word detection
void wake_word_on_detect(WakeWordCallback cb);

/// Deinitialize wake word engine
void wake_word_deinit();

} // namespace minbot::audio
