//
// Neam CLI - Package manager for .npk archives
//

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "miniz.h"

namespace
{
struct PackageMetadata
{
  std::string name;
  std::string version;
  std::string author;
};

std::string trim(const std::string& value)
{
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
  {
    ++start;
  }
  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
  {
    --end;
  }
  return value.substr(start, end - start);
}

std::string unquote(const std::string& value)
{
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
  {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

PackageMetadata parse_neam_yaml(const std::string& content)
{
  PackageMetadata meta;
  std::istringstream input(content);
  std::string line;
  while (std::getline(input, line))
  {
    const auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#')
    {
      continue;
    }
    const auto sep = trimmed.find(':');
    if (sep == std::string::npos)
    {
      continue;
    }
    const std::string key = trim(trimmed.substr(0, sep));
    const std::string value = trim(trimmed.substr(sep + 1));
    const std::string normalized = unquote(value);
    if (key == "name")
    {
      meta.name = normalized;
    }
    else if (key == "version")
    {
      meta.version = normalized;
    }
    else if (key == "author")
    {
      meta.author = normalized;
    }
  }
  return meta;
}

std::string read_file(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input)
  {
    throw std::runtime_error("Failed to read file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void add_file_to_zip(mz_zip_archive& zip, const std::filesystem::path& file_path,
                     const std::string& archive_name)
{
  if (!mz_zip_writer_add_file(&zip, archive_name.c_str(), file_path.string().c_str(), nullptr, 0,
                              MZ_BEST_COMPRESSION))
  {
    throw std::runtime_error("Failed to add file to archive: " + archive_name);
  }
}

void add_directory_to_zip(mz_zip_archive& zip, const std::filesystem::path& base_dir,
                          const std::filesystem::path& dir)
{
  if (!std::filesystem::exists(dir))
  {
    return;
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
  {
    if (!entry.is_regular_file())
    {
      continue;
    }
    const auto relative_path = std::filesystem::relative(entry.path(), base_dir);
    add_file_to_zip(zip, entry.path(), relative_path.generic_string());
  }
}

std::filesystem::path registry_root()
{
  const char* home = std::getenv("HOME");
  if (!home)
  {
    throw std::runtime_error("HOME environment variable is not set");
  }
  return std::filesystem::path(home) / ".neam" / "registry";
}

PackageMetadata load_metadata_from_archive(mz_zip_archive& zip)
{
  const char* neam_yaml = "neam.yaml";
  size_t size = 0;
  void* data = mz_zip_reader_extract_file_to_heap(&zip, neam_yaml, &size, 0);
  if (!data)
  {
    throw std::runtime_error("Archive missing neam.yaml");
  }
  std::string content(reinterpret_cast<const char*>(data), size);
  mz_free(data);
  return parse_neam_yaml(content);
}

void update_manifest(const std::filesystem::path& manifest_path, const PackageMetadata& meta,
                     const std::filesystem::path& package_path)
{
  nlohmann::json manifest;
  if (std::filesystem::exists(manifest_path))
  {
    std::ifstream input(manifest_path);
    if (input)
    {
      input >> manifest;
    }
  }
  if (!manifest.is_object())
  {
    manifest = nlohmann::json::object();
  }
  if (!manifest.contains("packages") || !manifest["packages"].is_array())
  {
    manifest["packages"] = nlohmann::json::array();
  }
  nlohmann::json entry = {{"name", meta.name},
                          {"version", meta.version},
                          {"author", meta.author},
                          {"path", package_path.string()}};
  auto& packages = manifest["packages"];
  bool replaced = false;
  for (auto& existing : packages)
  {
    if (existing.value("name", "") == meta.name &&
        existing.value("version", "") == meta.version &&
        existing.value("author", "") == meta.author)
    {
      existing = entry;
      replaced = true;
      break;
    }
  }
  if (!replaced)
  {
    packages.push_back(entry);
  }
  std::ofstream output(manifest_path);
  output << manifest.dump(2) << "\n";
}

int package_command(const std::filesystem::path& source_dir)
{
  const auto manifest_path = source_dir / "neam.yaml";
  if (!std::filesystem::exists(manifest_path))
  {
    std::cerr << "Missing neam.yaml in package directory.\n";
    return 1;
  }
  const auto manifest_content = read_file(manifest_path);
  const auto meta = parse_neam_yaml(manifest_content);
  if (meta.name.empty() || meta.version.empty())
  {
    std::cerr << "neam.yaml must include name and version.\n";
    return 1;
  }
  const std::string archive_name = meta.name + "-" + meta.version + ".npk";
  const auto output_path = source_dir / archive_name;

  mz_zip_archive zip{};
  if (!mz_zip_writer_init_file(&zip, output_path.string().c_str(), 0))
  {
    std::cerr << "Failed to create archive.\n";
    return 1;
  }

  try
  {
    add_file_to_zip(zip, manifest_path, "neam.yaml");
    add_directory_to_zip(zip, source_dir, source_dir / "src");
    add_directory_to_zip(zip, source_dir, source_dir / "schemas");
  }
  catch (const std::exception& ex)
  {
    mz_zip_writer_end(&zip);
    std::cerr << ex.what() << "\n";
    return 1;
  }

  mz_zip_writer_finalize_archive(&zip);
  mz_zip_writer_end(&zip);
  std::cout << "Created " << output_path << "\n";
  return 0;
}

int install_command(const std::filesystem::path& archive_path)
{
  if (!std::filesystem::exists(archive_path))
  {
    std::cerr << "Archive not found.\n";
    return 1;
  }
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, archive_path.string().c_str(), 0))
  {
    std::cerr << "Failed to open archive.\n";
    return 1;
  }

  PackageMetadata meta;
  try
  {
    meta = load_metadata_from_archive(zip);
  }
  catch (const std::exception& ex)
  {
    mz_zip_reader_end(&zip);
    std::cerr << ex.what() << "\n";
    return 1;
  }

  if (meta.name.empty() || meta.version.empty())
  {
    mz_zip_reader_end(&zip);
    std::cerr << "neam.yaml missing name or version.\n";
    return 1;
  }
  if (meta.author.empty())
  {
    meta.author = "unknown";
  }

  const auto base = registry_root();
  const auto install_root = base / meta.author / meta.name / meta.version;
  std::filesystem::create_directories(install_root);

  mz_uint file_count = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < file_count; ++i)
  {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&zip, i, &stat))
    {
      continue;
    }
    const std::string filename = stat.m_filename ? stat.m_filename : "";
    if (filename.empty())
    {
      continue;
    }
    const auto output_path = install_root / filename;
    if (stat.m_is_directory)
    {
      std::filesystem::create_directories(output_path);
      continue;
    }
    std::filesystem::create_directories(output_path.parent_path());
    if (!mz_zip_reader_extract_to_file(&zip, i, output_path.string().c_str(), 0))
    {
      mz_zip_reader_end(&zip);
      std::cerr << "Failed to extract " << filename << "\n";
      return 1;
    }
  }

  mz_zip_reader_end(&zip);
  const auto manifest_path = base / "manifest.json";
  update_manifest(manifest_path, meta, install_root);
  std::cout << "Installed " << meta.name << "@" << meta.version << "\n";
  return 0;
}

void print_usage()
{
  std::cout << "Usage:\n";
  std::cout << "  neam-cli package <path>\n";
  std::cout << "  neam-cli install <archive.npk>\n";
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc < 3)
  {
    print_usage();
    return 1;
  }
  const std::string command = argv[1];
  const std::filesystem::path target = argv[2];

  try
  {
    if (command == "package")
    {
      return package_command(target);
    }
    if (command == "install")
    {
      return install_command(target);
    }
  }
  catch (const std::exception& ex)
  {
    std::cerr << ex.what() << "\n";
    return 1;
  }

  print_usage();
  return 1;
}
