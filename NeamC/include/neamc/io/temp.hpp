//
// NeamC - Temporary file utilities
//

#pragma once

#include <filesystem>

#include "neamc/io/filesystem.hpp"

namespace neamc::io
{
class TempFile
{
public:
  static IoResult<TempFile> create();
  static IoResult<TempFile> create_in(const fs::path& dir);
  static IoResult<TempFile> with_prefix(const std::string& prefix);

  ~TempFile();

  TempFile(TempFile&& other) noexcept;
  TempFile& operator=(TempFile&& other) noexcept;
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;

  const fs::path& path() const;
  File& file();
  const File& file() const;

  fs::path persist();

private:
  friend class TempDir;
  TempFile(fs::path path, File file);

  fs::path path_{};
  File file_;
  bool persist_{false};
};

class TempDir
{
public:
  static IoResult<TempDir> create();
  static IoResult<TempDir> create_in(const fs::path& parent);
  static IoResult<TempDir> with_prefix(const std::string& prefix);

  ~TempDir();

  TempDir(TempDir&& other) noexcept;
  TempDir& operator=(TempDir&& other) noexcept;
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const fs::path& path() const;
  IoResult<TempFile> create_file(const std::string& name);

  fs::path persist();

private:
  explicit TempDir(fs::path path);

  fs::path path_{};
  bool persist_{false};
};

}  // namespace neamc::io
