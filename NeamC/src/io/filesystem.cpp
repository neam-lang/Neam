//
// NeamC - File system implementation
//

#include "neamc/io/filesystem.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace neamc::io
{
namespace
{
IoError from_errno(int err, const fs::path& path)
{
  IoError error;
  error.os_error_code = err;
  error.path = path;
  switch (err)
  {
    case ENOENT:
      error.kind = IoError::Kind::kNotFound;
      error.message = "Path not found";
      break;
    case EACCES:
      error.kind = IoError::Kind::kPermissionDenied;
      error.message = "Permission denied";
      break;
    case EEXIST:
      error.kind = IoError::Kind::kAlreadyExists;
      error.message = "Already exists";
      break;
    case ENOTDIR:
      error.kind = IoError::Kind::kNotADirectory;
      error.message = "Not a directory";
      break;
    case EISDIR:
      error.kind = IoError::Kind::kNotAFile;
      error.message = "Not a file";
      break;
    case EINVAL:
      error.kind = IoError::Kind::kInvalidPath;
      error.message = "Invalid path";
      break;
    default:
      error.kind = IoError::Kind::kOther;
      error.message = std::strerror(err);
      break;
  }
  return error;
}

IoResult<void> void_ok()
{
  return IoResult<void>::Ok();
}

}  // namespace

IoError IoError::not_found(const fs::path& p)
{
  return IoError{Kind::kNotFound, "Path not found", std::nullopt, p};
}

IoError IoError::permission_denied(const fs::path& p)
{
  return IoError{Kind::kPermissionDenied, "Permission denied", std::nullopt, p};
}

IoError IoError::already_exists(const fs::path& p)
{
  return IoError{Kind::kAlreadyExists, "Already exists", std::nullopt, p};
}

File::File(std::FILE* handle, fs::path path) : handle_(handle), path_(std::move(path)) {}

File::File(File&& other) noexcept : handle_(other.handle_), path_(std::move(other.path_))
{
  other.handle_ = nullptr;
}

File& File::operator=(File&& other) noexcept
{
  if (this != &other)
  {
    close();
    handle_ = other.handle_;
    path_ = std::move(other.path_);
    other.handle_ = nullptr;
  }
  return *this;
}

File::~File()
{
  close();
}

IoResult<File> File::open(const fs::path& path, OpenMode mode)
{
  const char* mode_str = "rb";
  switch (mode)
  {
    case OpenMode::kRead:
      mode_str = "rb";
      break;
    case OpenMode::kWrite:
      mode_str = "wb";
      break;
    case OpenMode::kAppend:
      mode_str = "ab";
      break;
    case OpenMode::kReadWrite:
      mode_str = "rb+";
      break;
    case OpenMode::kCreate:
      mode_str = "wb+";
      break;
    case OpenMode::kCreateNew:
      if (fs::exists(path))
      {
        return IoResult<File>::Err(IoError::already_exists(path));
      }
      mode_str = "wb+";
      break;
    case OpenMode::kTruncate:
      mode_str = "wb";
      break;
  }

  std::FILE* handle = std::fopen(path.string().c_str(), mode_str);
  if (!handle)
  {
    return IoResult<File>::Err(from_errno(errno, path));
  }
  return IoResult<File>::Ok(File(handle, path));
}

IoResult<File> File::create(const fs::path& path)
{
  return open(path, OpenMode::kCreate);
}

IoResult<std::string> File::read_to_string()
{
  if (!handle_)
  {
    return IoResult<std::string>::Err(IoError{IoError::Kind::kOther, "File not open", std::nullopt, path_});
  }
  std::string content;
  std::fseek(handle_, 0, SEEK_END);
  auto size = std::ftell(handle_);
  std::fseek(handle_, 0, SEEK_SET);
  if (size < 0)
  {
    return IoResult<std::string>::Err(from_errno(errno, path_));
  }
  content.resize(static_cast<size_t>(size));
  if (!content.empty())
  {
    auto read = std::fread(content.data(), 1, content.size(), handle_);
    content.resize(read);
  }
  return IoResult<std::string>::Ok(std::move(content));
}

IoResult<std::vector<uint8_t>> File::read_bytes()
{
  auto text = read_to_string();
  if (text.is_err())
  {
    return IoResult<std::vector<uint8_t>>::Err(text.unwrap_err());
  }
  auto str = std::move(text).unwrap();
  return IoResult<std::vector<uint8_t>>::Ok(std::vector<uint8_t>(str.begin(), str.end()));
}

IoResult<std::string> File::read_line()
{
  if (!handle_)
  {
    return IoResult<std::string>::Err(IoError{IoError::Kind::kOther, "File not open", std::nullopt, path_});
  }
  std::string line;
  int ch = 0;
  while ((ch = std::fgetc(handle_)) != EOF)
  {
    if (ch == '\n')
    {
      break;
    }
    line.push_back(static_cast<char>(ch));
  }
  if (line.empty() && ch == EOF)
  {
    return IoResult<std::string>::Err(IoError{IoError::Kind::kOther, "EOF", std::nullopt, path_});
  }
  return IoResult<std::string>::Ok(std::move(line));
}

IoResult<std::vector<std::string>> File::read_lines()
{
  std::vector<std::string> lines;
  while (true)
  {
    auto line = read_line();
    if (line.is_err())
    {
      break;
    }
    lines.push_back(std::move(line).unwrap());
  }
  return IoResult<std::vector<std::string>>::Ok(std::move(lines));
}

IoResult<size_t> File::write(const std::string& content)
{
  if (!handle_)
  {
    return IoResult<size_t>::Err(IoError{IoError::Kind::kOther, "File not open", std::nullopt, path_});
  }
  auto written = std::fwrite(content.data(), 1, content.size(), handle_);
  if (written != content.size())
  {
    return IoResult<size_t>::Err(from_errno(errno, path_));
  }
  return IoResult<size_t>::Ok(written);
}

IoResult<size_t> File::write(const std::vector<uint8_t>& bytes)
{
  if (!handle_)
  {
    return IoResult<size_t>::Err(IoError{IoError::Kind::kOther, "File not open", std::nullopt, path_});
  }
  auto written = std::fwrite(bytes.data(), 1, bytes.size(), handle_);
  if (written != bytes.size())
  {
    return IoResult<size_t>::Err(from_errno(errno, path_));
  }
  return IoResult<size_t>::Ok(written);
}

IoResult<void> File::write_line(const std::string& line)
{
  auto result = write(line + "\n");
  if (result.is_err())
  {
    return IoResult<void>::Err(result.unwrap_err());
  }
  return void_ok();
}

IoResult<void> File::flush()
{
  if (!handle_)
  {
    return IoResult<void>::Err(IoError{IoError::Kind::kOther, "File not open", std::nullopt, path_});
  }
  if (std::fflush(handle_) != 0)
  {
    return IoResult<void>::Err(from_errno(errno, path_));
  }
  return void_ok();
}

IoResult<void> File::sync()
{
  return flush();
}

IoResult<void> File::close()
{
  if (handle_)
  {
    std::fclose(handle_);
    handle_ = nullptr;
  }
  return void_ok();
}

IoResult<uint64_t> File::seek(int64_t offset, SeekFrom from)
{
  if (!handle_)
  {
    return IoResult<uint64_t>::Err(IoError{IoError::Kind::kOther, "File not open", std::nullopt, path_});
  }
  int origin = SEEK_SET;
  switch (from)
  {
    case SeekFrom::kStart:
      origin = SEEK_SET;
      break;
    case SeekFrom::kEnd:
      origin = SEEK_END;
      break;
    case SeekFrom::kCurrent:
      origin = SEEK_CUR;
      break;
  }
  if (std::fseek(handle_, static_cast<long>(offset), origin) != 0)
  {
    return IoResult<uint64_t>::Err(from_errno(errno, path_));
  }
  return IoResult<uint64_t>::Ok(position());
}

uint64_t File::position() const
{
  if (!handle_)
  {
    return 0;
  }
  auto pos = std::ftell(handle_);
  if (pos < 0)
  {
    return 0;
  }
  return static_cast<uint64_t>(pos);
}

IoResult<FileMetadata> File::metadata() const
{
  return io::metadata(path_);
}

bool File::is_open() const
{
  return handle_ != nullptr;
}

AsyncFile::AsyncFile(std::shared_ptr<File> inner) : inner_(std::move(inner)) {}

runtime::Future<IoResult<AsyncFile>> AsyncFile::open(const fs::path& path, File::OpenMode mode)
{
  auto result = File::open(path, mode);
  if (result.is_err())
  {
    return runtime::Future<IoResult<AsyncFile>>(IoResult<AsyncFile>::Err(result.unwrap_err()));
  }
  auto file = std::make_shared<File>(std::move(result).unwrap());
  return runtime::Future<IoResult<AsyncFile>>(IoResult<AsyncFile>::Ok(AsyncFile(std::move(file))));
}

runtime::Future<IoResult<AsyncFile>> AsyncFile::create(const fs::path& path)
{
  return open(path, File::OpenMode::kCreate);
}

runtime::Future<IoResult<std::string>> AsyncFile::read_to_string()
{
  return runtime::Future<IoResult<std::string>>(inner_->read_to_string());
}

runtime::Future<IoResult<std::vector<uint8_t>>> AsyncFile::read_bytes()
{
  return runtime::Future<IoResult<std::vector<uint8_t>>>(inner_->read_bytes());
}

runtime::Future<IoResult<size_t>> AsyncFile::write(std::string content)
{
  return runtime::Future<IoResult<size_t>>(inner_->write(content));
}

runtime::Future<IoResult<size_t>> AsyncFile::write(std::vector<uint8_t> bytes)
{
  return runtime::Future<IoResult<size_t>>(inner_->write(bytes));
}

runtime::Future<IoResult<void>> AsyncFile::flush()
{
  return runtime::Future<IoResult<void>>(inner_->flush());
}

runtime::Future<IoResult<void>> AsyncFile::close()
{
  return runtime::Future<IoResult<void>>(inner_->close());
}

namespace directory
{
IoResult<void> create(const fs::path& path)
{
  std::error_code ec;
  if (!fs::create_directory(path, ec))
  {
    if (ec)
    {
      return IoResult<void>::Err(from_errno(ec.value(), path));
    }
  }
  return void_ok();
}

IoResult<void> create_all(const fs::path& path)
{
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec)
  {
    return IoResult<void>::Err(from_errno(ec.value(), path));
  }
  return void_ok();
}

IoResult<void> remove(const fs::path& path)
{
  std::error_code ec;
  fs::remove(path, ec);
  if (ec)
  {
    return IoResult<void>::Err(from_errno(ec.value(), path));
  }
  return void_ok();
}

IoResult<void> remove_all(const fs::path& path)
{
  std::error_code ec;
  fs::remove_all(path, ec);
  if (ec)
  {
    return IoResult<void>::Err(from_errno(ec.value(), path));
  }
  return void_ok();
}

IoResult<std::vector<fs::path>> list(const fs::path& path)
{
  std::vector<fs::path> entries;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(path, ec))
  {
    entries.push_back(entry.path());
  }
  if (ec)
  {
    return IoResult<std::vector<fs::path>>::Err(from_errno(ec.value(), path));
  }
  return IoResult<std::vector<fs::path>>::Ok(std::move(entries));
}

IoResult<std::vector<fs::path>> list_recursive(const fs::path& path)
{
  std::vector<fs::path> entries;
  std::error_code ec;
  for (const auto& entry : fs::recursive_directory_iterator(path, ec))
  {
    entries.push_back(entry.path());
  }
  if (ec)
  {
    return IoResult<std::vector<fs::path>>::Err(from_errno(ec.value(), path));
  }
  return IoResult<std::vector<fs::path>>::Ok(std::move(entries));
}

bool exists(const fs::path& path)
{
  return fs::exists(path);
}

bool is_directory(const fs::path& path)
{
  return fs::is_directory(path);
}

IoResult<FileMetadata> metadata(const fs::path& path)
{
  std::error_code ec;
  auto status = fs::status(path, ec);
  if (ec)
  {
    return IoResult<FileMetadata>::Err(from_errno(ec.value(), path));
  }

  FileMetadata meta;
  meta.path = path;
  meta.is_file = fs::is_regular_file(status);
  meta.is_directory = fs::is_directory(status);
  meta.is_symlink = fs::is_symlink(status);
  meta.permissions = status.permissions();

  if (meta.is_file)
  {
    meta.size = fs::file_size(path, ec);
  }

  auto mod_time = fs::last_write_time(path, ec);
  if (!ec)
  {
    auto now = std::chrono::system_clock::now();
    meta.modified = now;
    meta.created = now;
    meta.accessed = now;
  }

  return IoResult<FileMetadata>::Ok(meta);
}

}  // namespace directory

namespace path
{
fs::path join(const fs::path& base, const fs::path& other)
{
  return base / other;
}

fs::path normalize(const fs::path& p)
{
  return p.lexically_normal();
}

fs::path absolute(const fs::path& p)
{
  return fs::absolute(p);
}

stdlib::Option<fs::path> parent(const fs::path& p)
{
  if (p.has_parent_path())
  {
    return stdlib::Some(p.parent_path());
  }
  return stdlib::None;
}

stdlib::Option<std::string> filename(const fs::path& p)
{
  if (!p.filename().empty())
  {
    return stdlib::Some(p.filename().string());
  }
  return stdlib::None;
}

stdlib::Option<std::string> extension(const fs::path& p)
{
  if (!p.extension().empty())
  {
    return stdlib::Some(p.extension().string());
  }
  return stdlib::None;
}

stdlib::Option<std::string> stem(const fs::path& p)
{
  if (!p.stem().empty())
  {
    return stdlib::Some(p.stem().string());
  }
  return stdlib::None;
}

bool is_absolute(const fs::path& p)
{
  return p.is_absolute();
}

bool is_relative(const fs::path& p)
{
  return p.is_relative();
}

}  // namespace path

IoResult<std::string> read_to_string(const fs::path& path)
{
  auto file = File::open(path, File::OpenMode::kRead);
  if (file.is_err())
  {
    return IoResult<std::string>::Err(file.unwrap_err());
  }
  return std::move(file).unwrap().read_to_string();
}

IoResult<std::vector<uint8_t>> read_bytes(const fs::path& path)
{
  auto file = File::open(path, File::OpenMode::kRead);
  if (file.is_err())
  {
    return IoResult<std::vector<uint8_t>>::Err(file.unwrap_err());
  }
  return std::move(file).unwrap().read_bytes();
}

IoResult<void> write_string(const fs::path& path, const std::string& content)
{
  auto file = File::create(path);
  if (file.is_err())
  {
    return IoResult<void>::Err(file.unwrap_err());
  }
  auto result = std::move(file).unwrap().write(content);
  if (result.is_err())
  {
    return IoResult<void>::Err(result.unwrap_err());
  }
  return void_ok();
}

IoResult<void> write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes)
{
  auto file = File::create(path);
  if (file.is_err())
  {
    return IoResult<void>::Err(file.unwrap_err());
  }
  auto result = std::move(file).unwrap().write(bytes);
  if (result.is_err())
  {
    return IoResult<void>::Err(result.unwrap_err());
  }
  return void_ok();
}

IoResult<void> copy(const fs::path& from, const fs::path& to)
{
  std::error_code ec;
  fs::copy(from, to, fs::copy_options::overwrite_existing, ec);
  if (ec)
  {
    return IoResult<void>::Err(from_errno(ec.value(), from));
  }
  return void_ok();
}

IoResult<void> rename(const fs::path& from, const fs::path& to)
{
  std::error_code ec;
  fs::rename(from, to, ec);
  if (ec)
  {
    return IoResult<void>::Err(from_errno(ec.value(), from));
  }
  return void_ok();
}

IoResult<void> remove(const fs::path& path)
{
  return directory::remove(path);
}

bool exists(const fs::path& path)
{
  return fs::exists(path);
}

IoResult<FileMetadata> metadata(const fs::path& path)
{
  return directory::metadata(path);
}

runtime::Future<IoResult<std::string>> read_to_string_async(const fs::path& path)
{
  return runtime::Future<IoResult<std::string>>(read_to_string(path));
}

runtime::Future<IoResult<void>> write_string_async(const fs::path& path, std::string content)
{
  return runtime::Future<IoResult<void>>(write_string(path, content));
}

}  // namespace neamc::io
