// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Buffered I/O
//

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "neamc/io/filesystem.hpp"

namespace neamc::io
{
template <typename Inner>
class BufReader
{
public:
  explicit BufReader(Inner inner, size_t capacity = 8192)
      : inner_(std::move(inner)), buffer_(capacity)
  {
  }

  IoResult<std::string> read_line()
  {
    return inner_.read_line();
  }

  IoResult<std::vector<std::string>> lines()
  {
    return inner_.read_lines();
  }

  IoResult<size_t> read(std::vector<uint8_t>& buf)
  {
    auto data = inner_.read_bytes();
    if (data.is_err())
    {
      return IoResult<size_t>::Err(data.unwrap_err());
    }
    auto bytes = std::move(data).unwrap();
    const auto count = std::min(buf.size(), bytes.size());
    std::copy(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(count), buf.begin());
    return IoResult<size_t>::Ok(count);
  }

  IoResult<uint8_t> read_byte()
  {
    std::vector<uint8_t> buf(1);
    auto result = read(buf);
    if (result.is_err())
    {
      return IoResult<uint8_t>::Err(result.unwrap_err());
    }
    if (result.unwrap() == 0)
    {
      return IoResult<uint8_t>::Err(IoError{IoError::Kind::kOther, "No data", std::nullopt, {}});
    }
    return IoResult<uint8_t>::Ok(buf[0]);
  }

  size_t buffer_size() const { return buffer_.size(); }

  void set_buffer_size(size_t size) { buffer_.assign(size, 0); }

  Inner& inner() { return inner_; }
  const Inner& inner() const { return inner_; }

private:
  Inner inner_;
  std::vector<uint8_t> buffer_;
  size_t pos_{0};
  size_t filled_{0};

  IoResult<void> fill_buffer()
  {
    (void)pos_;
    (void)filled_;
    return IoResult<void>::Ok();
  }
};

template <typename Inner>
class BufWriter
{
public:
  explicit BufWriter(Inner inner, size_t capacity = 8192)
      : inner_(std::move(inner)), buffer_(capacity)
  {
  }

  ~BufWriter() { flush(); }

  IoResult<size_t> write(const std::vector<uint8_t>& data)
  {
    return inner_.write(data);
  }

  IoResult<size_t> write(const std::string& data)
  {
    return inner_.write(data);
  }

  IoResult<void> write_byte(uint8_t byte)
  {
    std::vector<uint8_t> data{byte};
    auto result = write(data);
    if (result.is_err())
    {
      return IoResult<void>::Err(result.unwrap_err());
    }
    return IoResult<void>::Ok();
  }

  IoResult<void> flush()
  {
    return inner_.flush();
  }

  size_t buffer_size() const { return buffer_.size(); }

  void set_buffer_size(size_t size) { buffer_.assign(size, 0); }

  Inner& inner() { return inner_; }
  const Inner& inner() const { return inner_; }

private:
  Inner inner_;
  std::vector<uint8_t> buffer_;

  IoResult<void> flush_buffer() { return IoResult<void>::Ok(); }
};

}  // namespace neamc::io
