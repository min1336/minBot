#pragma once
#include <cstdint>
#include <cstddef>

namespace minbot::audio {

/// Initialize MAX98357A I2S speaker
int spk_init();

/// Write PCM samples to speaker
/// @param buffer 16-bit PCM samples
/// @param bytes Number of bytes to write
/// @param bytes_written Actual bytes written (output)
/// @param timeout_ms Max wait time
/// @return ESP_OK on success
int spk_write(const int16_t* buffer, size_t bytes, size_t* bytes_written, uint32_t timeout_ms = 100);

/// Immediately stop playback and clear buffers (for barge-in)
void spk_stop_and_clear();

/// Deinitialize speaker
void spk_deinit();

} // namespace minbot::audio
