// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - PCM audio buffer management
//

#include "neamc/voice/audio_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace neamc::voice
{
AudioBuffer::AudioBuffer(int sample_rate, int channels, int bits_per_sample)
    : sample_rate_(sample_rate)
    , channels_(channels)
    , bits_per_sample_(bits_per_sample)
{
}

void AudioBuffer::append(const std::vector<uint8_t>& pcm)
{
  std::lock_guard<std::mutex> lock(mutex_);
  buffer_.insert(buffer_.end(), pcm.begin(), pcm.end());
}

void AudioBuffer::append(const uint8_t* data, std::size_t len)
{
  std::lock_guard<std::mutex> lock(mutex_);
  buffer_.insert(buffer_.end(), data, data + len);
}

std::vector<uint8_t> AudioBuffer::drain()
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<uint8_t> result;
  result.swap(buffer_);
  return result;
}

std::vector<uint8_t> AudioBuffer::drain(std::size_t max_bytes)
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t n = std::min(max_bytes, buffer_.size());
  std::vector<uint8_t> result(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(n));
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(n));
  return result;
}

std::vector<uint8_t> AudioBuffer::peek() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return buffer_;
}

void AudioBuffer::clear()
{
  std::lock_guard<std::mutex> lock(mutex_);
  buffer_.clear();
}

std::size_t AudioBuffer::size() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return buffer_.size();
}

double AudioBuffer::duration_ms() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  int bytes_per_sample = channels_ * (bits_per_sample_ / 8);
  if (bytes_per_sample == 0 || sample_rate_ == 0)
  {
    return 0.0;
  }
  double samples = static_cast<double>(buffer_.size()) / bytes_per_sample;
  return (samples / sample_rate_) * 1000.0;
}

bool AudioBuffer::empty() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return buffer_.empty();
}

std::vector<std::vector<uint8_t>> AudioBuffer::split_chunks(double chunk_ms)
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t chunk_bytes = ms_to_bytes(chunk_ms);
  if (chunk_bytes == 0)
  {
    return {};
  }

  std::vector<std::vector<uint8_t>> chunks;
  std::size_t offset = 0;
  while (offset + chunk_bytes <= buffer_.size())
  {
    chunks.emplace_back(buffer_.begin() + static_cast<std::ptrdiff_t>(offset),
                        buffer_.begin() + static_cast<std::ptrdiff_t>(offset + chunk_bytes));
    offset += chunk_bytes;
  }

  // Keep remainder in buffer
  if (offset > 0)
  {
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
  }

  return chunks;
}

void AudioBuffer::write_wav(const std::string& path) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  write_wav_file(path, buffer_, sample_rate_, channels_, bits_per_sample_);
}

void AudioBuffer::write_wav(const std::string& path,
                            const std::vector<uint8_t>& pcm) const
{
  write_wav_file(path, pcm, sample_rate_, channels_, bits_per_sample_);
}

void AudioBuffer::write_wav_file(const std::string& path,
                                 const std::vector<uint8_t>& pcm,
                                 int sample_rate, int channels,
                                 int bits_per_sample)
{
  std::ofstream out(path, std::ios::binary);
  if (!out)
  {
    throw std::runtime_error("Failed to create WAV file: " + path);
  }

  auto sr = static_cast<uint32_t>(sample_rate);
  auto bits = static_cast<uint16_t>(bits_per_sample);
  auto ch = static_cast<uint16_t>(channels);
  auto data_size = static_cast<uint32_t>(pcm.size());
  uint32_t byte_rate = sr * ch * bits / 8;
  uint16_t block_align = ch * bits / 8;
  uint32_t chunk_size = 36 + data_size;

  auto write_u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
  auto write_u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

  out.write("RIFF", 4);
  write_u32(chunk_size);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  write_u32(16);          // subchunk1 size
  write_u16(1);           // PCM format
  write_u16(ch);
  write_u32(sr);
  write_u32(byte_rate);
  write_u16(block_align);
  write_u16(bits);
  out.write("data", 4);
  write_u32(data_size);
  out.write(reinterpret_cast<const char*>(pcm.data()),
            static_cast<std::streamsize>(pcm.size()));
}

std::vector<uint8_t> AudioBuffer::resample(const std::vector<uint8_t>& pcm,
                                           int from_rate, int to_rate)
{
  if (from_rate == to_rate || pcm.empty())
  {
    return pcm;
  }

  // Interpret PCM as 16-bit signed mono samples
  std::size_t num_samples = pcm.size() / 2;
  if (num_samples == 0)
  {
    return {};
  }

  const auto* src = reinterpret_cast<const int16_t*>(pcm.data());
  double ratio = static_cast<double>(to_rate) / from_rate;
  auto out_samples = static_cast<std::size_t>(num_samples * ratio);

  std::vector<uint8_t> result(out_samples * 2);
  auto* dst = reinterpret_cast<int16_t*>(result.data());

  for (std::size_t i = 0; i < out_samples; ++i)
  {
    double src_idx = static_cast<double>(i) / ratio;
    auto idx0 = static_cast<std::size_t>(src_idx);
    double frac = src_idx - idx0;

    std::size_t idx1 = std::min(idx0 + 1, num_samples - 1);
    double val = src[idx0] * (1.0 - frac) + src[idx1] * frac;

    // Clamp
    if (val > 32767.0) val = 32767.0;
    if (val < -32768.0) val = -32768.0;
    dst[i] = static_cast<int16_t>(val);
  }

  return result;
}

std::size_t AudioBuffer::ms_to_bytes(double ms) const
{
  int bytes_per_sample = channels_ * (bits_per_sample_ / 8);
  double samples = (ms / 1000.0) * sample_rate_;
  return static_cast<std::size_t>(samples) * bytes_per_sample;
}
}  // namespace neamc::voice
