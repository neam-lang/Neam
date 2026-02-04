// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - PCM audio buffer management utility
//
// Provides accumulate, drain, chunk-splitting, WAV header writing,
// and sample rate conversion for PCM16 mono audio.
//

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace neamc::voice
{
// PCM audio buffer with thread-safe accumulate / drain / chunking.
class AudioBuffer
{
public:
  explicit AudioBuffer(int sample_rate = 16000, int channels = 1,
                       int bits_per_sample = 16);

  // Append raw PCM bytes.
  void append(const std::vector<uint8_t>& pcm);
  void append(const uint8_t* data, std::size_t len);

  // Drain all accumulated data (moves buffer out, leaves internal empty).
  std::vector<uint8_t> drain();

  // Drain up to `max_bytes` of data.
  std::vector<uint8_t> drain(std::size_t max_bytes);

  // Peek at all data without draining.
  std::vector<uint8_t> peek() const;

  // Clear buffer.
  void clear();

  // Current size in bytes.
  std::size_t size() const;

  // Current duration in milliseconds.
  double duration_ms() const;

  // True if buffer is empty.
  bool empty() const;

  // Split buffer into fixed-size chunks of `chunk_ms` milliseconds.
  // Returns complete chunks and leaves remainder in buffer.
  std::vector<std::vector<uint8_t>> split_chunks(double chunk_ms);

  // Write accumulated data to a WAV file. Does NOT drain the buffer.
  void write_wav(const std::string& path) const;

  // Write given PCM data to a WAV file with these buffer's format params.
  void write_wav(const std::string& path, const std::vector<uint8_t>& pcm) const;

  // Static helper: write WAV header + PCM data.
  static void write_wav_file(const std::string& path,
                             const std::vector<uint8_t>& pcm,
                             int sample_rate, int channels = 1,
                             int bits_per_sample = 16);

  // Simple sample-rate conversion (linear interpolation, mono 16-bit only).
  static std::vector<uint8_t> resample(const std::vector<uint8_t>& pcm,
                                       int from_rate, int to_rate);

  int sample_rate() const { return sample_rate_; }
  int channels() const { return channels_; }
  int bits_per_sample() const { return bits_per_sample_; }

private:
  std::size_t ms_to_bytes(double ms) const;

  mutable std::mutex mutex_;
  std::vector<uint8_t> buffer_;
  int sample_rate_;
  int channels_;
  int bits_per_sample_;
};
}  // namespace neamc::voice
