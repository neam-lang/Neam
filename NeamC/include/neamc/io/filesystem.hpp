// SPDX-License-Identifier: Apache-2.0
//
// NeamC - File system API
//

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "neamc/runtime/future.hpp"
#include "neamc/stdlib/option.hpp"
#include "neamc/stdlib/result.hpp"

namespace neamc::io
{
namespace fs = std::filesystem;

struct FileMetadata
{
  fs::path path;
  uint64_t size{0};
  std::chrono::system_clock::time_point created;
  std::chrono::system_clock::time_point modified;
  std::chrono::system_clock::time_point accessed;
  bool is_file{false};
  bool is_directory{false};
  bool is_symlink{false};
  fs::perms permissions{};
};

struct IoError
{
  enum class Kind
  {
    kNotFound,
    kPermissionDenied,
    kAlreadyExists,
    kNotADirectory,
    kNotAFile,
    kInvalidPath,
    kQuotaExceeded,
    kInterrupted,
    kOther
  };

  Kind kind{Kind::kOther};
  std::string message;
  std::optional<int> os_error_code;
  fs::path path;

  static IoError not_found(const fs::path& p);
  static IoError permission_denied(const fs::path& p);
  static IoError already_exists(const fs::path& p);
};

template <typename T>
using IoResult = stdlib::Result<T, IoError>;

enum class SeekFrom
{
  kStart,
  kEnd,
  kCurrent
};

class File
{
public:
  enum class OpenMode
  {
    kRead,
    kWrite,
    kAppend,
    kReadWrite,
    kCreate,
    kCreateNew,
    kTruncate
  };

  static IoResult<File> open(const fs::path& path, OpenMode mode);
  static IoResult<File> create(const fs::path& path);

  File(File&& other) noexcept;
  File& operator=(File&& other) noexcept;
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  ~File();

  IoResult<std::string> read_to_string();
  IoResult<std::vector<uint8_t>> read_bytes();
  IoResult<std::string> read_line();
  IoResult<std::vector<std::string>> read_lines();

  IoResult<size_t> write(const std::string& content);
  IoResult<size_t> write(const std::vector<uint8_t>& bytes);
  IoResult<void> write_line(const std::string& line);

  IoResult<void> flush();
  IoResult<void> sync();
  IoResult<void> close();

  IoResult<uint64_t> seek(int64_t offset, SeekFrom from);
  uint64_t position() const;

  IoResult<FileMetadata> metadata() const;
  bool is_open() const;

private:
  File(std::FILE* handle, fs::path path);

  std::FILE* handle_{nullptr};
  fs::path path_{};
};

class AsyncFile
{
public:
  static runtime::Future<IoResult<AsyncFile>> open(const fs::path& path, File::OpenMode mode);
  static runtime::Future<IoResult<AsyncFile>> create(const fs::path& path);

  runtime::Future<IoResult<std::string>> read_to_string();
  runtime::Future<IoResult<std::vector<uint8_t>>> read_bytes();
  runtime::Future<IoResult<size_t>> write(std::string content);
  runtime::Future<IoResult<size_t>> write(std::vector<uint8_t> bytes);
  runtime::Future<IoResult<void>> flush();
  runtime::Future<IoResult<void>> close();

private:
  explicit AsyncFile(std::shared_ptr<File> inner);

  std::shared_ptr<File> inner_;
};

namespace directory
{
IoResult<void> create(const fs::path& path);
IoResult<void> create_all(const fs::path& path);
IoResult<void> remove(const fs::path& path);
IoResult<void> remove_all(const fs::path& path);

IoResult<std::vector<fs::path>> list(const fs::path& path);
IoResult<std::vector<fs::path>> list_recursive(const fs::path& path);

bool exists(const fs::path& path);
bool is_directory(const fs::path& path);

IoResult<FileMetadata> metadata(const fs::path& path);

}  // namespace directory

namespace path
{
fs::path join(const fs::path& base, const fs::path& other);
fs::path normalize(const fs::path& p);
fs::path absolute(const fs::path& p);
stdlib::Option<fs::path> parent(const fs::path& p);
stdlib::Option<std::string> filename(const fs::path& p);
stdlib::Option<std::string> extension(const fs::path& p);
stdlib::Option<std::string> stem(const fs::path& p);

bool is_absolute(const fs::path& p);
bool is_relative(const fs::path& p);

}  // namespace path

IoResult<std::string> read_to_string(const fs::path& path);
IoResult<std::vector<uint8_t>> read_bytes(const fs::path& path);
IoResult<void> write_string(const fs::path& path, const std::string& content);
IoResult<void> write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes);

IoResult<void> copy(const fs::path& from, const fs::path& to);
IoResult<void> rename(const fs::path& from, const fs::path& to);
IoResult<void> remove(const fs::path& path);

bool exists(const fs::path& path);
IoResult<FileMetadata> metadata(const fs::path& path);

runtime::Future<IoResult<std::string>> read_to_string_async(const fs::path& path);
runtime::Future<IoResult<void>> write_string_async(const fs::path& path, std::string content);

}  // namespace neamc::io
