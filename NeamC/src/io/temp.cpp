//
// NeamC - Temporary file utilities implementation
//

#include "neamc/io/temp.hpp"

#include <random>

namespace neamc::io
{
namespace
{
std::string generate_random_hex(size_t length)
{
  static thread_local std::random_device rd;
  static thread_local std::mt19937 gen(rd());
  static thread_local std::uniform_int_distribution<> dis(0, 15);
  static const char hex_chars[] = "0123456789abcdef";

  std::string result;
  result.reserve(length);
  for (size_t i = 0; i < length; ++i)
  {
    result += hex_chars[dis(gen)];
  }
  return result;
}

fs::path unique_path(const std::string& prefix)
{
  auto dir = fs::temp_directory_path();
  auto name = prefix.empty() ? "neam-" : prefix + "-";
  name += generate_random_hex(16);
  return dir / name;
}

fs::path unique_path_with_pattern(const std::string& pattern)
{
  std::string result;
  result.reserve(pattern.size());
  for (char c : pattern)
  {
    if (c == '%')
    {
      result += generate_random_hex(1);
    }
    else
    {
      result += c;
    }
  }
  return result;
}

}  // namespace

IoResult<TempFile> TempFile::create()
{
  return create_in(fs::temp_directory_path());
}

IoResult<TempFile> TempFile::create_in(const fs::path& dir)
{
  auto path = dir / unique_path_with_pattern("neam-%%%%-%%%%-%%%%.tmp");
  auto file = File::create(path);
  if (file.is_err())
  {
    return IoResult<TempFile>::Err(file.unwrap_err());
  }
  return IoResult<TempFile>::Ok(TempFile(path, std::move(file).unwrap()));
}

IoResult<TempFile> TempFile::with_prefix(const std::string& prefix)
{
  auto path = unique_path(prefix + "-file");
  auto file = File::create(path);
  if (file.is_err())
  {
    return IoResult<TempFile>::Err(file.unwrap_err());
  }
  return IoResult<TempFile>::Ok(TempFile(path, std::move(file).unwrap()));
}

TempFile::~TempFile()
{
  if (!persist_)
  {
    std::error_code ec;
    file_.close();
    fs::remove(path_, ec);
  }
}

TempFile::TempFile(fs::path path, File file) : path_(std::move(path)), file_(std::move(file)) {}

TempFile::TempFile(TempFile&& other) noexcept
    : path_(std::move(other.path_)), file_(std::move(other.file_)), persist_(other.persist_)
{
  other.persist_ = true;
}

TempFile& TempFile::operator=(TempFile&& other) noexcept
{
  if (this != &other)
  {
    path_ = std::move(other.path_);
    file_ = std::move(other.file_);
    persist_ = other.persist_;
    other.persist_ = true;
  }
  return *this;
}

const fs::path& TempFile::path() const
{
  return path_;
}

File& TempFile::file()
{
  return file_;
}

const File& TempFile::file() const
{
  return file_;
}

fs::path TempFile::persist()
{
  persist_ = true;
  return path_;
}

IoResult<TempDir> TempDir::create()
{
  return create_in(fs::temp_directory_path());
}

IoResult<TempDir> TempDir::create_in(const fs::path& parent)
{
  auto path = parent / unique_path_with_pattern("neamdir-%%%%-%%%%-%%%%");
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec)
  {
    return IoResult<TempDir>::Err(IoError{IoError::Kind::kOther, ec.message(), ec.value(), path});
  }
  return IoResult<TempDir>::Ok(TempDir(path));
}

IoResult<TempDir> TempDir::with_prefix(const std::string& prefix)
{
  auto path = unique_path(prefix + "-dir");
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec)
  {
    return IoResult<TempDir>::Err(IoError{IoError::Kind::kOther, ec.message(), ec.value(), path});
  }
  return IoResult<TempDir>::Ok(TempDir(path));
}

TempDir::~TempDir()
{
  if (!persist_)
  {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
}

TempDir::TempDir(fs::path path) : path_(std::move(path)) {}

TempDir::TempDir(TempDir&& other) noexcept : path_(std::move(other.path_)), persist_(other.persist_)
{
  other.persist_ = true;
}

TempDir& TempDir::operator=(TempDir&& other) noexcept
{
  if (this != &other)
  {
    path_ = std::move(other.path_);
    persist_ = other.persist_;
    other.persist_ = true;
  }
  return *this;
}

const fs::path& TempDir::path() const
{
  return path_;
}

IoResult<TempFile> TempDir::create_file(const std::string& name)
{
  auto path = path_ / name;
  auto file = File::create(path);
  if (file.is_err())
  {
    return IoResult<TempFile>::Err(file.unwrap_err());
  }
  return IoResult<TempFile>::Ok(TempFile(path, std::move(file).unwrap()));
}

fs::path TempDir::persist()
{
  persist_ = true;
  return path_;
}

}  // namespace neamc::io
