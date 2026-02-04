#include <gtest/gtest.h>

#include "neamc/io/filesystem.hpp"
#include "neamc/io/temp.hpp"

namespace neamc::io
{
TEST(FileTest, ReadWriteRoundTrip)
{
  auto temp = TempFile::create().unwrap();

  const std::string content = "Hello, World!";
  temp.file().write(content).unwrap();
  temp.file().seek(0, SeekFrom::kStart).unwrap();

  auto read_back = temp.file().read_to_string().unwrap();
  EXPECT_EQ(read_back, content);
}

TEST(FileTest, ReadLines)
{
  auto temp = TempFile::create().unwrap();
  temp.file().write("line1\nline2\nline3\n").unwrap();
  temp.file().seek(0, SeekFrom::kStart).unwrap();

  auto lines = temp.file().read_lines().unwrap();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "line1");
  EXPECT_EQ(lines[1], "line2");
  EXPECT_EQ(lines[2], "line3");
}

TEST(FileTest, NotFoundError)
{
  auto result = File::open("/nonexistent/path", File::OpenMode::kRead);
  ASSERT_TRUE(result.is_err());
  EXPECT_EQ(result.unwrap_err().kind, IoError::Kind::kNotFound);
}

TEST(DirectoryTest, CreateAndList)
{
  auto temp = TempDir::create().unwrap();

  directory::create(temp.path() / "subdir").unwrap();

  // Create files and keep them in scope
  auto f1 = temp.create_file("file1.txt");
  auto f2 = temp.create_file("file2.txt");
  ASSERT_TRUE(f1.is_ok());
  ASSERT_TRUE(f2.is_ok());

  auto entries = directory::list(temp.path()).unwrap();
  // Should have at least 3 entries (subdir + 2 files), may have more on some platforms
  EXPECT_GE(entries.size(), 3u);
}
}  // namespace neamc::io
