//
// Neam Package Manager CLI
//
// Usage:
//   neam-pkg install                    # Install all dependencies
//   neam-pkg install <package>          # Install specific package
//   neam-pkg install <package>@<ver>    # Install specific version
//   neam-pkg install --dev <package>    # Install as dev dependency
//   neam-pkg update                     # Update all packages
//   neam-pkg update <package>           # Update specific package
//   neam-pkg remove <package>           # Remove package
//   neam-pkg list                       # List installed packages
//   neam-pkg outdated                   # Show outdated packages
//   neam-pkg search <query>             # Search packages
//   neam-pkg info <package>             # Show package info
//   neam-pkg login                      # Login to registry
//   neam-pkg publish                    # Publish package
//   neam-pkg init                       # Initialize new package
//   neam-pkg cache clean                # Clean cache
//

#include "neamc/pkg/installer.hpp"
#include "neamc/pkg/registry.hpp"
#include "neamc/version.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace
{

// ANSI color codes
const char* RESET = "\033[0m";
const char* BOLD = "\033[1m";
const char* RED = "\033[31m";
const char* GREEN = "\033[32m";
const char* YELLOW = "\033[33m";
const char* BLUE = "\033[34m";
const char* CYAN = "\033[36m";

void print_usage()
{
  std::cout << BOLD << "Neam Package Manager" << RESET << "\n\n";
  std::cout << BOLD << "Usage:" << RESET << " neam-pkg <command> [options] [arguments]\n\n";

  std::cout << BOLD << "Commands:" << RESET << "\n";
  std::cout << "  " << GREEN << "install" << RESET << "              Install dependencies from neam.toml\n";
  std::cout << "  " << GREEN << "install <pkg>" << RESET << "       Install a specific package\n";
  std::cout << "  " << GREEN << "install <pkg>@<v>" << RESET << "   Install a specific version\n";
  std::cout << "  " << GREEN << "update" << RESET << "               Update all packages\n";
  std::cout << "  " << GREEN << "update <pkg>" << RESET << "        Update a specific package\n";
  std::cout << "  " << GREEN << "remove <pkg>" << RESET << "        Remove a package\n";
  std::cout << "  " << GREEN << "list" << RESET << "                 List installed packages\n";
  std::cout << "  " << GREEN << "outdated" << RESET << "             Show outdated packages\n";
  std::cout << "  " << GREEN << "search <query>" << RESET << "      Search for packages\n";
  std::cout << "  " << GREEN << "info <pkg>" << RESET << "          Show package information\n";
  std::cout << "  " << GREEN << "login" << RESET << "                Login to registry\n";
  std::cout << "  " << GREEN << "logout" << RESET << "               Logout from registry\n";
  std::cout << "  " << GREEN << "publish" << RESET << "              Publish package to registry\n";
  std::cout << "  " << GREEN << "init" << RESET << "                 Initialize a new package\n";
  std::cout << "  " << GREEN << "cache clean" << RESET << "         Clean the package cache\n";
  std::cout << "  " << GREEN << "cache info" << RESET << "          Show cache information\n";

  std::cout << "\n" << BOLD << "Options:" << RESET << "\n";
  std::cout << "  " << CYAN << "--dev" << RESET << "                Install as dev dependency\n";
  std::cout << "  " << CYAN << "--registry <url>" << RESET << "     Use custom registry URL\n";
  std::cout << "  " << CYAN << "--help, -h" << RESET << "           Show this help message\n";
  std::cout << "  " << CYAN << "--version, -v" << RESET << "        Show version\n";
}

void print_version()
{
  std::cout << "neam-pkg " << NEAM_VERSION << "\n";
}

void print_error(const std::string& msg)
{
  std::cerr << RED << "Error: " << RESET << msg << "\n";
}

void print_success(const std::string& msg)
{
  std::cout << GREEN << "✓ " << RESET << msg << "\n";
}

void print_info(const std::string& msg)
{
  std::cout << BLUE << "→ " << RESET << msg << "\n";
}

void progress_callback(const std::string& package, const std::string& status, double progress)
{
  if (package.empty())
  {
    std::cout << GREEN << "✓ " << RESET << status << "\n";
  }
  else
  {
    int bar_width = 30;
    int filled = static_cast<int>(progress * bar_width);

    std::cout << "\r" << CYAN << package << RESET << " " << status << " [";
    for (int i = 0; i < bar_width; ++i)
    {
      if (i < filled)
        std::cout << "=";
      else if (i == filled)
        std::cout << ">";
      else
        std::cout << " ";
    }
    std::cout << "] " << static_cast<int>(progress * 100) << "%" << std::flush;

    if (progress >= 1.0)
    {
      std::cout << "\n";
    }
  }
}

std::string read_password()
{
  std::string password;

#ifdef _WIN32
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  GetConsoleMode(hStdin, &mode);
  SetConsoleMode(hStdin, mode & (~ENABLE_ECHO_INPUT));
  std::getline(std::cin, password);
  SetConsoleMode(hStdin, mode);
#else
  struct termios old_term{};
  struct termios new_term{};
  if (tcgetattr(STDIN_FILENO, &old_term) == 0)
  {
    new_term = old_term;
    new_term.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    std::getline(std::cin, password);
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
  }
  else
  {
    // Fallback if termios fails (e.g. piped input)
    std::getline(std::cin, password);
  }
#endif

  std::cout << "\n";
  return password;
}

std::filesystem::path find_project_root()
{
  std::filesystem::path current = std::filesystem::current_path();

  while (!current.empty())
  {
    if (std::filesystem::exists(current / "neam.toml"))
    {
      return current;
    }

    std::filesystem::path parent = current.parent_path();
    if (parent == current)
    {
      break;
    }
    current = parent;
  }

  return std::filesystem::current_path();
}

int cmd_install(const std::vector<std::string>& args, bool dev_only, const std::string& registry_url)
{
  std::filesystem::path project_root = find_project_root();
  neamc::pkg::Installer installer(project_root);
  installer.set_progress_callback(progress_callback);

  if (args.empty())
  {
    // Install all dependencies from neam.toml
    print_info("Installing dependencies from neam.toml...");

    if (!installer.install_all())
    {
      print_error(installer.last_error());
      return 1;
    }

    print_success("All dependencies installed successfully!");
    return 0;
  }

  // Install specific packages
  for (const auto& arg : args)
  {
    std::string name = arg;
    std::string version;

    // Parse package@version syntax
    std::size_t at_pos = arg.find('@');
    if (at_pos != std::string::npos)
    {
      name = arg.substr(0, at_pos);
      version = arg.substr(at_pos + 1);
    }

    print_info("Installing " + name + (version.empty() ? "" : "@" + version) + "...");

    bool success = dev_only ? installer.install_dev_package(name, version) : installer.install_package(name, version);

    if (!success)
    {
      print_error(installer.last_error());
      return 1;
    }

    print_success("Installed " + name);
  }

  return 0;
}

int cmd_update(const std::vector<std::string>& args)
{
  std::filesystem::path project_root = find_project_root();
  neamc::pkg::Installer installer(project_root);
  installer.set_progress_callback(progress_callback);

  if (args.empty())
  {
    print_info("Updating all packages...");

    if (!installer.update_all())
    {
      print_error(installer.last_error());
      return 1;
    }

    print_success("All packages updated!");
    return 0;
  }

  for (const auto& name : args)
  {
    print_info("Updating " + name + "...");

    if (!installer.update_package(name))
    {
      print_error(installer.last_error());
      return 1;
    }

    print_success("Updated " + name);
  }

  return 0;
}

int cmd_remove(const std::vector<std::string>& args)
{
  if (args.empty())
  {
    print_error("Please specify a package to remove");
    return 1;
  }

  std::filesystem::path project_root = find_project_root();
  neamc::pkg::Installer installer(project_root);

  for (const auto& name : args)
  {
    print_info("Removing " + name + "...");

    if (!installer.remove_package(name))
    {
      print_error(installer.last_error());
      return 1;
    }

    print_success("Removed " + name);
  }

  return 0;
}

int cmd_list()
{
  std::filesystem::path project_root = find_project_root();
  neamc::pkg::Installer installer(project_root);

  auto installed = installer.list_installed();

  if (installed.empty())
  {
    std::cout << "No packages installed.\n";
    return 0;
  }

  std::cout << BOLD << "Installed packages:" << RESET << "\n\n";

  for (const auto& pkg : installed)
  {
    std::cout << "  " << GREEN << pkg.name << RESET << " " << CYAN << pkg.version << RESET;
    if (pkg.dev_only)
    {
      std::cout << " " << YELLOW << "(dev)" << RESET;
    }
    std::cout << "\n";
  }

  std::cout << "\n" << installed.size() << " package(s) installed.\n";
  return 0;
}

int cmd_outdated()
{
  std::filesystem::path project_root = find_project_root();
  neamc::pkg::Installer installer(project_root);

  print_info("Checking for outdated packages...");

  auto outdated = installer.list_outdated();

  if (outdated.empty())
  {
    print_success("All packages are up to date!");
    return 0;
  }

  std::cout << BOLD << "\nOutdated packages:" << RESET << "\n\n";

  for (const auto& [name, latest] : outdated)
  {
    std::cout << "  " << name << " → " << GREEN << latest << RESET << "\n";
  }

  std::cout << "\nRun 'neam-pkg update' to update all packages.\n";
  return 0;
}

int cmd_search(const std::string& query, const std::string& registry_url)
{
  neamc::pkg::RegistryClient registry(registry_url);

  print_info("Searching for '" + query + "'...");

  auto results = registry.search(query);

  if (results.empty())
  {
    std::cout << "No packages found.\n";
    return 0;
  }

  std::cout << "\n";
  for (const auto& result : results)
  {
    std::cout << BOLD << result.name << RESET << " " << CYAN << result.latest_version << RESET << "\n";
    std::cout << "    " << result.description << "\n";
    std::cout << "    Downloads: " << result.downloads << "\n\n";
  }

  return 0;
}

int cmd_info(const std::string& name, const std::string& registry_url)
{
  neamc::pkg::RegistryClient registry(registry_url);

  auto pkg_info = registry.get_package(name);

  if (!pkg_info)
  {
    print_error("Package not found: " + name);
    return 1;
  }

  std::cout << "\n";
  std::cout << BOLD << pkg_info->name << RESET << "\n";
  std::cout << "  Description: " << pkg_info->description << "\n";
  std::cout << "  License: " << pkg_info->license << "\n";
  std::cout << "  Repository: " << pkg_info->repository << "\n";
  std::cout << "  Homepage: " << pkg_info->homepage << "\n";
  std::cout << "  Downloads: " << pkg_info->downloads << "\n";

  if (!pkg_info->authors.empty())
  {
    std::cout << "  Authors: ";
    for (std::size_t i = 0; i < pkg_info->authors.size(); ++i)
    {
      if (i > 0)
        std::cout << ", ";
      std::cout << pkg_info->authors[i];
    }
    std::cout << "\n";
  }

  if (!pkg_info->versions.empty())
  {
    std::cout << "\n  Versions:\n";
    int count = 0;
    for (const auto& ver : pkg_info->versions)
    {
      if (count >= 5)
      {
        std::cout << "    ... and " << (pkg_info->versions.size() - 5) << " more\n";
        break;
      }
      std::cout << "    " << ver.version;
      if (ver.yanked)
      {
        std::cout << " " << RED << "(yanked)" << RESET;
      }
      std::cout << " - " << ver.published_at << "\n";
      ++count;
    }
  }

  std::cout << "\n";
  return 0;
}

int cmd_login(const std::string& registry_url)
{
  neamc::pkg::RegistryClient registry(registry_url);

  std::string username, password;

  std::cout << "Username: ";
  std::getline(std::cin, username);

  std::cout << "Password: ";
  password = read_password();

  print_info("Logging in...");

  if (!registry.login(username, password))
  {
    print_error(registry.last_error());
    return 1;
  }

  print_success("Logged in as " + username);
  return 0;
}

int cmd_logout(const std::string& registry_url)
{
  neamc::pkg::RegistryClient registry(registry_url);

  if (!registry.is_authenticated())
  {
    std::cout << "Not logged in.\n";
    return 0;
  }

  registry.logout();
  print_success("Logged out successfully");
  return 0;
}

int cmd_publish(const std::string& registry_url)
{
  std::filesystem::path project_root = find_project_root();

  // Check if neam.toml exists
  if (!std::filesystem::exists(project_root / "neam.toml"))
  {
    print_error("No neam.toml found. Are you in a Neam project directory?");
    return 1;
  }

  neamc::pkg::RegistryClient registry(registry_url);

  if (!registry.is_authenticated())
  {
    print_error("Not logged in. Please run 'neam-pkg login' first.");
    return 1;
  }

  print_info("Packaging project...");

  // Create package archive (simplified)
  // In real implementation, this would create a .neampkg archive

  print_info("Publishing to registry...");

  if (!registry.publish_package(project_root.string()))
  {
    print_error(registry.last_error());
    return 1;
  }

  print_success("Package published successfully!");
  return 0;
}

int cmd_init(const std::vector<std::string>& args)
{
  std::string name;
  std::filesystem::path target_dir = std::filesystem::current_path();

  if (!args.empty())
  {
    name = args[0];
    target_dir = target_dir / name;
    std::filesystem::create_directories(target_dir);
  }
  else
  {
    name = target_dir.filename().string();
  }

  std::filesystem::path manifest_path = target_dir / "neam.toml";

  if (std::filesystem::exists(manifest_path))
  {
    print_error("neam.toml already exists");
    return 1;
  }

  print_info("Creating new Neam project: " + name);

  // Create neam.toml
  std::ofstream manifest(manifest_path);
  manifest << "neam_version = \"1.0\"\n\n";
  manifest << "[project]\n";
  manifest << "name = \"" << name << "\"\n";
  manifest << "version = \"0.1.0\"\n";
  manifest << "description = \"A Neam project\"\n";
  manifest << "type = \"binary\"\n";
  manifest << "authors = []\n";
  manifest << "license = \"MIT\"\n\n";
  manifest << "[project.entry_points]\n";
  manifest << "main = \"src/main.neam\"\n\n";
  manifest << "[dependencies]\n\n";
  manifest << "[dev-dependencies]\n\n";
  manifest << "[agent]\n";
  manifest << "provider = \"openai\"\n";
  manifest << "model = \"gpt-4o-mini\"\n";
  manifest.close();

  // Create src directory and main.neam
  std::filesystem::create_directories(target_dir / "src");
  std::ofstream main_file(target_dir / "src" / "main.neam");
  main_file << "//\n";
  main_file << "// " << name << " - Main entry point\n";
  main_file << "//\n\n";
  main_file << "agent HelloAgent {\n";
  main_file << "  provider: \"openai\"\n";
  main_file << "  model: \"gpt-4o-mini\"\n";
  main_file << "  system: \"You are a helpful assistant.\"\n";
  main_file << "}\n\n";
  main_file << "{\n";
  main_file << "  let response = HelloAgent.ask(\"Hello, world!\");\n";
  main_file << "  emit response;\n";
  main_file << "}\n";
  main_file.close();

  // Create tests directory
  std::filesystem::create_directories(target_dir / "tests");

  // Create .gitignore
  std::ofstream gitignore(target_dir / ".gitignore");
  gitignore << "build/\n";
  gitignore << ".neam/\n";
  gitignore << "*.neamb\n";
  gitignore.close();

  print_success("Created new Neam project in " + target_dir.string());
  std::cout << "\nNext steps:\n";
  std::cout << "  cd " << name << "\n";
  std::cout << "  neamc src/main.neam -o build/main.neamb\n";
  std::cout << "  neam build/main.neamb\n";

  return 0;
}

int cmd_cache(const std::vector<std::string>& args)
{
  if (args.empty())
  {
    print_error("Please specify a cache command: clean, info");
    return 1;
  }

  neamc::pkg::PackageCache& cache = neamc::pkg::PackageCache::instance();

  if (args[0] == "clean")
  {
    print_info("Cleaning package cache...");
    cache.clean_cache();
    print_success("Cache cleaned");
    return 0;
  }

  if (args[0] == "info")
  {
    std::size_t size = cache.cache_size();
    std::cout << "Cache directory: " << cache.cache_dir() << "\n";

    if (size < 1024)
    {
      std::cout << "Cache size: " << size << " bytes\n";
    }
    else if (size < 1024 * 1024)
    {
      std::cout << "Cache size: " << std::fixed << std::setprecision(2) << (size / 1024.0) << " KB\n";
    }
    else
    {
      std::cout << "Cache size: " << std::fixed << std::setprecision(2) << (size / (1024.0 * 1024.0)) << " MB\n";
    }

    return 0;
  }

  print_error("Unknown cache command: " + args[0]);
  return 1;
}

}  // namespace

int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    print_usage();
    return 0;
  }

  std::vector<std::string> args;
  std::string command;
  std::string registry_url = "https://registry.neam.dev";
  bool dev_only = false;

  // Parse arguments
  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h")
    {
      print_usage();
      return 0;
    }

    if (arg == "--version" || arg == "-v")
    {
      print_version();
      return 0;
    }

    if (arg == "--dev")
    {
      dev_only = true;
      continue;
    }

    if (arg == "--registry" && i + 1 < argc)
    {
      registry_url = argv[++i];
      continue;
    }

    if (command.empty())
    {
      command = arg;
    }
    else
    {
      args.push_back(arg);
    }
  }

  if (command.empty())
  {
    print_usage();
    return 0;
  }

  // Execute command
  if (command == "install" || command == "i" || command == "add")
  {
    return cmd_install(args, dev_only, registry_url);
  }

  if (command == "update" || command == "upgrade")
  {
    return cmd_update(args);
  }

  if (command == "remove" || command == "rm" || command == "uninstall")
  {
    return cmd_remove(args);
  }

  if (command == "list" || command == "ls")
  {
    return cmd_list();
  }

  if (command == "outdated")
  {
    return cmd_outdated();
  }

  if (command == "search")
  {
    if (args.empty())
    {
      print_error("Please specify a search query");
      return 1;
    }
    return cmd_search(args[0], registry_url);
  }

  if (command == "info" || command == "show")
  {
    if (args.empty())
    {
      print_error("Please specify a package name");
      return 1;
    }
    return cmd_info(args[0], registry_url);
  }

  if (command == "login")
  {
    return cmd_login(registry_url);
  }

  if (command == "logout")
  {
    return cmd_logout(registry_url);
  }

  if (command == "publish")
  {
    return cmd_publish(registry_url);
  }

  if (command == "init" || command == "new")
  {
    return cmd_init(args);
  }

  if (command == "cache")
  {
    return cmd_cache(args);
  }

  print_error("Unknown command: " + command);
  print_usage();
  return 1;
}
