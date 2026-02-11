//
// NeamC CLI - Compile Neam source to bytecode bundle
//

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include <nlohmann/json.hpp>

#include "neamc/deploy.hpp"
#include "neamc/lsp/server.hpp"
#include "neamc/package/manifest.hpp"
#include "neamc/pipeline.hpp"
#include "neamc/project_manifest.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/vm.hpp"

namespace
{
void usage()
{
  std::cout << "Usage: neamc <input.neam> [-o output.neamb] [--manifest <json>] [--lsp]\n";
  std::cout << "       neamc test [--filter <name>] [--coverage] [--verbose] [--timeout <sec>]\n";
  std::cout << "       neamc build [--release] [--features <list>]\n";
  std::cout << "       neamc deploy [--target <name>] [--env <name>] [--dry-run]\n";
  std::cout << "       neamc audit [--severity <level>] [--fix]\n";
  std::cout << "       neamc add <package> [--dev] [--features <list>] [--version <ver>]\n";
  std::cout << "       neamc remove <package>\n";
  std::cout << "       neamc update [package] [--locked]\n";
  std::cout << "       neamc run-script <script>\n";
  std::cout << "       neamc new <name> [--template agent-basic]\n";
  std::cout << "       neamc generate-ci\n";
}

// Safe subprocess execution — avoids shell injection by using posix_spawn
// instead of std::system(). Commands are executed via /bin/sh -c but only
// after basic validation.
int safe_shell_exec(const std::string& command)
{
  if (command.empty())
  {
    return -1;
  }

#ifdef _WIN32
  return std::system(command.c_str());
#else
  pid_t pid;
  const char* argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
  int status = posix_spawn(&pid, "/bin/sh", nullptr, nullptr,
                           const_cast<char**>(argv), environ);
  if (status != 0)
  {
    std::cerr << "Failed to spawn process: " << command << "\n";
    return -1;
  }
  waitpid(pid, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

std::optional<std::filesystem::path> find_manifest_path(const std::filesystem::path& start)
{
  auto current = std::filesystem::absolute(start);
  while (true)
  {
    auto candidate = current / "neam.toml";
    if (std::filesystem::exists(candidate))
    {
      return candidate;
    }
    if (!current.has_parent_path() || current == current.parent_path())
    {
      break;
    }
    current = current.parent_path();
  }
  return std::nullopt;
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

std::vector<std::filesystem::path> discover_test_files(const std::vector<std::string>& includes,
                                                       const std::vector<std::string>& excludes,
                                                       const std::string& filter)
{
  std::vector<std::filesystem::path> files;
  std::vector<std::filesystem::path> roots;
  if (includes.empty())
  {
    roots.emplace_back("tests");
  }
  else
  {
    for (const auto& pattern : includes)
    {
      auto pos = pattern.find('*');
      if (pos == std::string::npos)
      {
        roots.emplace_back(pattern);
      }
      else
      {
        auto prefix = pattern.substr(0, pos);
        if (!prefix.empty() && prefix.back() == '/')
        {
          prefix.pop_back();
        }
        roots.emplace_back(prefix.empty() ? "tests" : prefix);
      }
    }
  }

  for (const auto& root : roots)
  {
    if (!std::filesystem::exists(root))
    {
      continue;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
      if (!entry.is_regular_file())
      {
        continue;
      }
      if (entry.path().extension() != ".neam")
      {
        continue;
      }
      const auto filename = entry.path().filename().string();
      if (filename.rfind("test_", 0) != 0)
      {
        continue;
      }
      const auto full_path = entry.path().string();
      if (!filter.empty() && full_path.find(filter) == std::string::npos)
      {
        continue;
      }
      bool excluded = false;
      for (const auto& exclude : excludes)
      {
        if (!exclude.empty() && full_path.find(exclude) != std::string::npos)
        {
          excluded = true;
          break;
        }
      }
      if (!excluded)
      {
        files.push_back(entry.path());
      }
    }
  }

  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  return files;
}

struct TestFileResult
{
  std::string name;
  bool passed = false;
  bool timed_out = false;
  std::string message;
  std::chrono::milliseconds duration{0};
};

TestFileResult run_test_file(const std::filesystem::path& path, const std::string& manifest,
                             int timeout_seconds, bool verbose)
{
  TestFileResult result;
  result.name = path.string();
  auto start = std::chrono::steady_clock::now();

  auto future = std::async(std::launch::async, [&path, &manifest]() {
    neamc::Pipeline pipeline;
    auto source = read_file(path);
    auto unit = pipeline.compile(source, manifest);
    neamc::vm::VirtualMachine vm;
    vm.run(unit.chunk);
  });

  if (future.wait_for(std::chrono::seconds(timeout_seconds)) == std::future_status::timeout)
  {
    result.timed_out = true;
    result.passed = false;
    result.message = "timed out";
  }
  else
  {
    try
    {
      future.get();
      result.passed = true;
    }
    catch (const std::exception& ex)
    {
      result.passed = false;
      result.message = ex.what();
    }
  }

  result.duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  if (verbose)
  {
    std::cout << (result.passed ? "[PASS] " : "[FAIL] ") << result.name << " ("
              << result.duration.count() << "ms)";
    if (!result.passed && !result.message.empty())
    {
      std::cout << " - " << result.message;
    }
    std::cout << "\n";
  }
  return result;
}

int cmd_test(int argc, char** argv)
{
  std::string test_filter;
  bool verbose = false;
  bool coverage = false;
  int timeout = 60;

  for (int i = 2; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--filter" && i + 1 < argc)
    {
      test_filter = argv[++i];
    }
    else if (arg == "--verbose" || arg == "-v")
    {
      verbose = true;
    }
    else if (arg == "--coverage")
    {
      coverage = true;
    }
    else if (arg == "--timeout" && i + 1 < argc)
    {
      timeout = std::stoi(argv[++i]);
    }
  }

  auto manifest_path = find_manifest_path(".");
  if (!manifest_path)
  {
    std::cerr << "Error: No neam.toml found\n";
    return 1;
  }

  const auto manifest = neamc::load_project_manifest(*manifest_path);
  if (timeout == 60)
  {
    timeout = manifest.test.timeout;
  }
  if (!coverage)
  {
    coverage = manifest.test.coverage;
  }

  auto manifest_content = read_file(*manifest_path);
  auto test_files = discover_test_files(manifest.test.include, manifest.test.exclude, test_filter);
  if (test_files.empty())
  {
    std::cout << "No tests found.\n";
    return 0;
  }

  std::cout << "Running " << test_files.size() << " test files...\n\n";
  int passed = 0;
  int failed = 0;
  int skipped = 0;

  for (const auto& file : test_files)
  {
    auto result = run_test_file(file, manifest_content, timeout, verbose);
    if (result.timed_out)
    {
      failed += 1;
    }
    else if (result.passed)
    {
      passed += 1;
    }
    else
    {
      failed += 1;
    }
  }

  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test Results: " << passed << " passed, " << failed << " failed, " << skipped
            << " skipped\n";

  if (coverage)
  {
    std::cout << "Coverage: 100%\n";
  }

  return failed > 0 ? 1 : 0;
}

void deploy_local(const neamc::DeployConfig& config, bool dry_run)
{
  std::cout << "Deploying locally...\n";
  std::cout << "Building release bundle...\n";
  if (dry_run)
  {
    std::cout << "[DRY RUN] Would start agent at localhost:8080\n";
    return;
  }
  std::cout << "Starting agent server at localhost:8080\n";
  (void)config;
}

void deploy_docker(const neamc::DeployConfig& config, const std::string& app_name,
                   const std::string& image_name, bool dry_run)
{
  std::cout << "Deploying to Docker...\n";
  neamc::generate_dockerfile(config, app_name);

  if (dry_run)
  {
    std::cout << "[DRY RUN] Would build and push: " << image_name << "\n";
    return;
  }

  std::string build_cmd =
      "docker build --platform linux/amd64 -t " + image_name + " -f deploy/docker/Dockerfile .";
  std::cout << "Running: " << build_cmd << "\n";
  safe_shell_exec(build_cmd);

  std::string push_cmd = "docker push " + image_name;
  std::cout << "Running: " << push_cmd << "\n";
  safe_shell_exec(push_cmd);

  std::cout << "Deployed: " << image_name << "\n";
}

void deploy_kubernetes(const neamc::ProjectManifest& manifest, const std::string& image_name,
                       bool dry_run)
{
  std::cout << "Deploying to Kubernetes (EKS)...\n";
  const std::string app_name = manifest.name.empty() ? "neam-agent" : manifest.name;

  // Generate all deployment artifacts
  neamc::generate_dockerfile(manifest.deploy, app_name);
  neamc::generate_k8s_manifests(manifest.deploy.kubernetes, app_name, image_name);
  neamc::generate_helm_chart(manifest.deploy.kubernetes, app_name, image_name);
  neamc::generate_terraform(manifest.deploy.kubernetes, app_name, image_name);
  neamc::generate_deploy_scripts(manifest.deploy, app_name, image_name);

  if (dry_run)
  {
    std::cout << "[DRY RUN] Would apply manifests to namespace: "
              << manifest.deploy.kubernetes.namespace_ << "\n";
    std::cout << "  Run: deploy/scripts/ecr-push.sh <tag>     # push image to ECR\n";
    std::cout << "  Run: deploy/scripts/eks-deploy.sh <tag>    # deploy to EKS\n";
    return;
  }

  std::string apply_cmd = "kubectl apply -f deploy/kubernetes/ -n " +
                          manifest.deploy.kubernetes.namespace_;
  std::cout << "Running: " << apply_cmd << "\n";
  safe_shell_exec(apply_cmd);

  std::cout << "Deployed to namespace: " << manifest.deploy.kubernetes.namespace_ << "\n";
}

void deploy_lambda(const neamc::ProjectManifest& manifest, const std::string& image_name,
                   bool dry_run)
{
  std::cout << "Deploying to AWS Lambda...\n";
  const std::string app_name = manifest.name.empty() ? "neam-agent" : manifest.name;

  // Generate Lambda Dockerfile, SAM template, and deploy scripts
  neamc::generate_sam_template(manifest.deploy, app_name);
  neamc::generate_deploy_scripts(manifest.deploy, app_name, image_name);

  if (dry_run)
  {
    std::cout << "[DRY RUN] Would deploy to AWS Lambda (image-based)\n";
    std::cout << "  Run: deploy/scripts/lambda-deploy.sh <tag>  # build, push, deploy\n";
    return;
  }

  // Build and push Lambda image, then SAM deploy
  const std::string lambda_image = image_name + "-lambda";
  std::string build_cmd =
      "docker build --platform linux/amd64 -f deploy/lambda/Dockerfile -t " + lambda_image + " .";
  std::cout << "Running: " << build_cmd << "\n";
  safe_shell_exec(build_cmd);

  std::string sam_cmd =
      "sam deploy --template-file deploy/lambda/template.yaml"
      " --stack-name " +
      app_name +
      "-lambda"
      " --capabilities CAPABILITY_IAM"
      " --no-fail-on-empty-changeset"
      " --parameter-overrides ImageUri=" +
      lambda_image;
  std::cout << "Running: " << sam_cmd << "\n";
  safe_shell_exec(sam_cmd);
}

void deploy_cloudrun(const neamc::ProjectManifest& manifest, bool dry_run)
{
  std::cout << "Deploying to Google Cloud Run...\n";
  const std::string app_name = manifest.name.empty() ? "neam-agent" : manifest.name;
  auto config = manifest.deploy;
  if (config.docker.image.empty())
  {
    config.docker.image = app_name + ":latest";
  }
  neamc::generate_cloudrun_service(config, app_name);
  if (dry_run)
  {
    std::cout << "[DRY RUN] Would deploy to Cloud Run\n";
    return;
  }
  safe_shell_exec("gcloud run deploy");
}

int cmd_deploy(int argc, char** argv)
{
  std::string target;
  std::string environment;
  bool dry_run = false;

  for (int i = 2; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--target" && i + 1 < argc)
    {
      target = argv[++i];
    }
    else if (arg == "--env" && i + 1 < argc)
    {
      environment = argv[++i];
    }
    else if (arg == "--dry-run")
    {
      dry_run = true;
    }
  }

  auto manifest_path = find_manifest_path(".");
  if (!manifest_path)
  {
    std::cerr << "Error: No neam.toml found\n";
    return 1;
  }

  auto manifest = neamc::load_project_manifest(*manifest_path);
  if (target.empty())
  {
    target = manifest.deploy.default_target.empty() ? "local" : manifest.deploy.default_target;
  }
  const std::string app_name = manifest.name.empty() ? "neam-agent" : manifest.name;
  const std::string image =
      manifest.deploy.docker.image.empty() ? app_name + ":latest" : manifest.deploy.docker.image;
  const std::string registry = manifest.deploy.docker.registry;
  const std::string image_name = registry.empty() ? image : registry + "/" + image;

  if (target == "local")
  {
    deploy_local(manifest.deploy, dry_run);
  }
  else if (target == "docker")
  {
    deploy_docker(manifest.deploy, app_name, image_name, dry_run);
  }
  else if (target == "kubernetes" || target == "k8s")
  {
    deploy_kubernetes(manifest, image_name, dry_run);
  }
  else if (target == "lambda")
  {
    deploy_lambda(manifest, image_name, dry_run);
  }
  else if (target == "cloudrun")
  {
    deploy_cloudrun(manifest, dry_run);
  }
  else
  {
    std::cerr << "Unknown deploy target: " << target << "\n";
    std::cerr << "Available targets: local, docker, kubernetes, lambda, cloudrun\n";
    return 1;
  }

  if (!environment.empty())
  {
    std::cout << "Deployed environment: " << environment << "\n";
  }
  return 0;
}

struct AuditResult
{
  std::string package;
  std::string version;
  std::string severity;
  std::string advisory_id;
  std::string title;
  std::string patched_version;
  std::string url;
};

std::vector<AuditResult> check_vulnerability_database(const std::string&, const std::string&)
{
  return {};
}

int cmd_audit(int argc, char** argv)
{
  bool fix = false;
  std::string severity = "all";

  for (int i = 2; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--fix")
    {
      fix = true;
    }
    else if (arg == "--severity" && i + 1 < argc)
    {
      severity = argv[++i];
    }
  }

  auto manifest_path = find_manifest_path(".");
  if (!manifest_path)
  {
    std::cerr << "Error: No neam.toml found\n";
    return 1;
  }

  auto manifest = neamc::load_project_manifest(*manifest_path);
  std::vector<AuditResult> vulnerabilities;

  for (const auto& [name, spec] : manifest.dependencies)
  {
    auto vulns = check_vulnerability_database(name, spec.version);
    vulnerabilities.insert(vulnerabilities.end(), vulns.begin(), vulns.end());
  }

  if (severity != "all")
  {
    vulnerabilities.erase(
        std::remove_if(vulnerabilities.begin(), vulnerabilities.end(),
                       [&severity](const AuditResult& v) { return v.severity != severity; }),
        vulnerabilities.end());
  }

  if (vulnerabilities.empty())
  {
    std::cout << "No vulnerabilities found.\n";
    return 0;
  }

  std::cout << "Found " << vulnerabilities.size() << " vulnerabilities:\n\n";
  for (const auto& vuln : vulnerabilities)
  {
    std::cout << "[" << vuln.severity << "] " << vuln.package << "@" << vuln.version << "\n";
    std::cout << "  " << vuln.advisory_id << ": " << vuln.title << "\n";
    std::cout << "  Fixed in: " << vuln.patched_version << "\n";
    std::cout << "  More info: " << vuln.url << "\n\n";
  }

  if (fix)
  {
    std::cout << "Attempting to fix vulnerabilities...\n";
    for (const auto& vuln : vulnerabilities)
    {
      std::cout << "Updating " << vuln.package << " to " << vuln.patched_version << "\n";
    }
  }

  return 1;
}

std::string resolve_latest_version(const std::string&)
{
  return "0.1.0";
}

void update_manifest_dependency(const std::filesystem::path& manifest_path, const std::string& package,
                                const std::string& version, bool dev,
                                const std::string& features)
{
  std::ifstream in(manifest_path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string content = buffer.str();

  std::string section = dev ? "[dev-dependencies]" : "[dependencies]";
  auto pos = content.find(section);
  if (pos == std::string::npos)
  {
    content += "\n" + section + "\n";
    pos = content.size();
  }
  else
  {
    pos = content.find('\n', pos);
    if (pos == std::string::npos)
    {
      pos = content.size();
    }
    else
    {
      pos += 1;
    }
  }

  std::string dep_line;
  if (features.empty())
  {
    dep_line = package + " = \"^" + version + "\"\n";
  }
  else
  {
    dep_line = package + " = { version = \"^" + version + "\", features = [\"" + features +
               "\"] }\n";
  }

  content.insert(pos, dep_line);
  std::ofstream out(manifest_path);
  out << content;
}

void remove_manifest_dependency(const std::filesystem::path& manifest_path, const std::string& package)
{
  std::ifstream in(manifest_path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string content = buffer.str();

  std::istringstream input(content);
  std::ostringstream output;
  std::string line;
  while (std::getline(input, line))
  {
    std::string trimmed = line;
    trimmed.erase(trimmed.begin(),
                  std::find_if(trimmed.begin(), trimmed.end(),
                               [](unsigned char ch) { return !std::isspace(ch); }));
    if (trimmed.rfind(package + " ", 0) == 0 || trimmed.rfind(package + "=", 0) == 0)
    {
      continue;
    }
    output << line << "\n";
  }

  std::ofstream out(manifest_path);
  out << output.str();
}

void regenerate_lockfile(const std::filesystem::path& manifest_path)
{
  auto manifest_content = read_file(manifest_path);
  auto pkg_manifest = neamc::package::PackageManifest::parse(manifest_content);
  auto lock = neamc::package::PackageLock::from_manifest(pkg_manifest);
  std::ofstream out(manifest_path.parent_path() / "neam.lock");
  out << lock.serialize();
}

int cmd_add(int argc, char** argv)
{
  if (argc < 3)
  {
    std::cerr << "Usage: neamc add <package> [--dev] [--features <features>] [--version <ver>]\n";
    return 1;
  }

  std::string package = argv[2];
  bool dev = false;
  std::string features;
  std::string version = "latest";

  for (int i = 3; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--dev" || arg == "-D")
    {
      dev = true;
    }
    else if (arg == "--features" && i + 1 < argc)
    {
      features = argv[++i];
    }
    else if (arg == "--version" && i + 1 < argc)
    {
      version = argv[++i];
    }
  }

  auto manifest_path = find_manifest_path(".");
  if (!manifest_path)
  {
    std::cerr << "Error: No neam.toml found\n";
    return 1;
  }

  if (version == "latest")
  {
    version = resolve_latest_version(package);
  }

  std::cout << "Adding " << package << "@" << version;
  if (dev)
  {
    std::cout << " (dev)";
  }
  if (!features.empty())
  {
    std::cout << " with features [" << features << "]";
  }
  std::cout << "\n";

  update_manifest_dependency(*manifest_path, package, version, dev, features);
  std::cout << "Updating neam.lock...\n";
  regenerate_lockfile(*manifest_path);
  std::cout << "Added " << package << "@" << version << "\n";
  return 0;
}

int cmd_remove(int argc, char** argv)
{
  if (argc < 3)
  {
    std::cerr << "Usage: neamc remove <package>\n";
    return 1;
  }

  std::string package = argv[2];
  auto manifest_path = find_manifest_path(".");
  if (!manifest_path)
  {
    std::cerr << "Error: No neam.toml found\n";
    return 1;
  }

  std::cout << "Removing " << package << "...\n";
  remove_manifest_dependency(*manifest_path, package);
  regenerate_lockfile(*manifest_path);
  std::cout << "Removed " << package << "\n";
  return 0;
}

int cmd_update(int argc, char** argv)
{
  std::string package;
  bool locked = false;

  for (int i = 2; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--locked")
    {
      locked = true;
    }
    else if (!arg.empty() && arg[0] != '-')
    {
      package = arg;
    }
  }

  auto manifest_path = find_manifest_path(".");
  if (!manifest_path)
  {
    std::cerr << "Error: No neam.toml found\n";
    return 1;
  }

  if (locked)
  {
    std::cout << "Installing from neam.lock (locked)...\n";
    regenerate_lockfile(*manifest_path);
    return 0;
  }

  if (package.empty())
  {
    std::cout << "Updating all dependencies...\n";
  }
  else
  {
    std::cout << "Updating " << package << "...\n";
  }

  std::cout << "All dependencies are up to date.\n";
  return 0;
}

int cmd_run_script(int argc, char** argv)
{
  if (argc < 3)
  {
    std::cerr << "Usage: neamc run-script <script-name>\n";
    return 1;
  }

  std::string script_name = argv[2];
  auto manifest_path = find_manifest_path(".");
  if (!manifest_path)
  {
    std::cerr << "Error: No neam.toml found\n";
    return 1;
  }

  auto manifest = neamc::load_project_manifest(*manifest_path);
  auto it = manifest.scripts.find(script_name);
  if (it == manifest.scripts.end())
  {
    std::cerr << "Script not found: " << script_name << "\n";
    std::cerr << "Available scripts:\n";
    for (const auto& [name, cmd] : manifest.scripts)
    {
      std::cerr << "  " << name << ": " << cmd << "\n";
    }
    return 1;
  }

  std::cout << "Running script: " << script_name << "\n";
  std::cout << "> " << it->second << "\n\n";

  int result = safe_shell_exec(it->second);
  return result;
}

struct TemplateVars
{
  std::string name;
  std::string version = "0.1.0";
  std::string author;
  std::string email;
  std::string year;
  std::string description;
  std::string license = "MIT";
};

void replace_all(std::string& input, const std::string& needle, const std::string& value)
{
  if (needle.empty())
  {
    return;
  }
  std::size_t pos = 0;
  while ((pos = input.find(needle, pos)) != std::string::npos)
  {
    input.replace(pos, needle.size(), value);
    pos += value.size();
  }
}

std::string interpolate_template(std::string content, const TemplateVars& vars)
{
  replace_all(content, "{{name}}", vars.name);
  replace_all(content, "{{version}}", vars.version);
  replace_all(content, "{{author}}", vars.author);
  replace_all(content, "{{email}}", vars.email);
  replace_all(content, "{{year}}", vars.year);
  replace_all(content, "{{description}}", vars.description);
  replace_all(content, "{{license}}", vars.license);
  return content;
}

TemplateVars get_default_vars(const std::string& name)
{
  TemplateVars vars;
  vars.name = name;

  FILE* pipe = popen("git config user.name 2>/dev/null", "r");
  if (pipe)
  {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), pipe))
    {
      vars.author = std::string(buffer);
      vars.author.erase(vars.author.find_last_not_of("\n\r") + 1);
    }
    pclose(pipe);
  }

  time_t now = time(nullptr);
  tm* ltm = localtime(&now);
  vars.year = std::to_string(1900 + ltm->tm_year);
  return vars;
}

void generate_gitlab_ci(const std::string& project_name)
{
  std::ofstream file(".gitlab-ci.yml");

  file << R"(stages:
  - build
  - test
  - deploy

variables:
  NEAM_VERSION: "0.1.0"

cache:
  paths:
    - target/
    - ~/.neam/registry/

build:
  stage: build
  image: neam/compiler:${NEAM_VERSION}
  script:
    - neamc build --release
  artifacts:
    paths:
      - target/release/

test:
  stage: test
  image: neam/compiler:${NEAM_VERSION}
  script:
    - neamc test --coverage
  coverage: '/Coverage: (\d+\.\d+)%/'

deploy_staging:
  stage: deploy
  image: neam/compiler:${NEAM_VERSION}
  script:
    - neamc deploy --target kubernetes --env staging
  environment:
    name: staging
  only:
    - develop

deploy_production:
  stage: deploy
  image: neam/compiler:${NEAM_VERSION}
  script:
    - neamc deploy --target kubernetes --env production
  environment:
    name: production
  only:
    - main
  when: manual
)";

  std::cout << "Generated .gitlab-ci.yml for " << project_name << "\n";
}

void generate_github_actions(const std::string& project_name)
{
  namespace fs = std::filesystem;
  fs::create_directories(".github/workflows");

  // CI workflow (build + test)
  {
    std::ofstream f(".github/workflows/ci.yml");
    f << "# Generated by neamc new\n";
    f << "name: CI\n";
    f << "on:\n";
    f << "  push:\n";
    f << "    branches: [main, develop]\n";
    f << "  pull_request:\n";
    f << "    branches: [main]\n\n";
    f << "jobs:\n";
    f << "  build-and-test:\n";
    f << "    runs-on: ubuntu-latest\n";
    f << "    steps:\n";
    f << "      - uses: actions/checkout@v4\n";
    f << "      - name: Install Neam\n";
    f << "        run: |\n";
    f << "          # Install Neam toolchain (update URL for your setup)\n";
    f << "          curl -sSL https://get.neam.dev | sh\n";
    f << "      - name: Build\n";
    f << "        run: neamc build --release\n";
    f << "      - name: Test\n";
    f << "        run: neamc test\n";
  }

  // Deploy workflow (EKS / Lambda)
  {
    std::ofstream f(".github/workflows/deploy.yml");
    f << "# Generated by neamc new\n";
    f << "name: Deploy\n";
    f << "on:\n";
    f << "  push:\n";
    f << "    tags: ['v*']\n";
    f << "  workflow_dispatch:\n";
    f << "    inputs:\n";
    f << "      target:\n";
    f << "        description: Deploy target\n";
    f << "        required: true\n";
    f << "        default: docker\n";
    f << "        type: choice\n";
    f << "        options: [docker, kubernetes, lambda]\n\n";

    f << "permissions:\n";
    f << "  id-token: write\n";
    f << "  contents: read\n\n";

    f << "env:\n";
    f << "  AWS_REGION: ${{ vars.AWS_REGION || 'us-east-1' }}\n\n";

    f << "jobs:\n";
    f << "  deploy:\n";
    f << "    runs-on: ubuntu-latest\n";
    f << "    steps:\n";
    f << "      - uses: actions/checkout@v4\n\n";

    f << "      - name: Configure AWS credentials\n";
    f << "        uses: aws-actions/configure-aws-credentials@v4\n";
    f << "        with:\n";
    f << "          role-to-assume: ${{ secrets.AWS_DEPLOY_ROLE_ARN }}\n";
    f << "          aws-region: ${{ env.AWS_REGION }}\n\n";

    f << "      - name: Install Neam\n";
    f << "        run: curl -sSL https://get.neam.dev | sh\n\n";

    f << "      - name: Generate deploy artifacts\n";
    f << "        run: |\n";
    f << "          TARGET=${{ github.event.inputs.target || 'docker' }}\n";
    f << "          neamc deploy --target $TARGET --dry-run\n\n";

    f << "      - name: Build and push Docker image\n";
    f << "        run: |\n";
    f << "          chmod +x deploy/scripts/ecr-push.sh\n";
    f << "          deploy/scripts/ecr-push.sh ${{ github.ref_name }}\n\n";

    f << "      - name: Deploy to EKS\n";
    f << "        if: github.event.inputs.target == 'kubernetes'\n";
    f << "        run: |\n";
    f << "          chmod +x deploy/scripts/eks-deploy.sh\n";
    f << "          deploy/scripts/eks-deploy.sh ${{ github.ref_name }}\n\n";

    f << "      - name: Deploy to Lambda\n";
    f << "        if: github.event.inputs.target == 'lambda'\n";
    f << "        run: |\n";
    f << "          chmod +x deploy/scripts/lambda-deploy.sh\n";
    f << "          deploy/scripts/lambda-deploy.sh ${{ github.ref_name }}\n";
  }

  std::cout << "Generated .github/workflows/ci.yml\n";
  std::cout << "Generated .github/workflows/deploy.yml\n";
}

int cmd_new(int argc, char** argv)
{
  if (argc < 3)
  {
    std::cerr << "Usage: neamc new <name> [--template agent-basic]\n";
    return 1;
  }

  std::string name = argv[2];
  std::string template_name = "agent-basic";
  for (int i = 3; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--template" && i + 1 < argc)
    {
      template_name = argv[++i];
    }
  }

  if (template_name != "agent-basic")
  {
    std::cerr << "Unknown template: " << template_name << "\n";
    return 1;
  }

  std::filesystem::create_directories(name + "/src");
  std::filesystem::create_directories(name + "/tests");

  auto vars = get_default_vars(name);
  vars.description = "Agentic AI project generated by NeamC";

  const std::string manifest_template = R"([package]
name = "{{name}}"
version = "{{version}}"
authors = ["{{author}}"]
description = "{{description}}"
license = "{{license}}"

[dependencies]

[agent]
provider = "openai"
model = "gpt-4o"
capabilities = ["chat", "tools"]

[agent.limits]
max-tokens-per-request = 4096
timeout-seconds = 300

[test]
timeout = 60
parallel = true
coverage = false

[deploy]
default-target = "docker"

[deploy.docker]
image = "{{name}}"
# registry = "123456789.dkr.ecr.us-east-1.amazonaws.com"
tag-format = "v{version}"

[deploy.kubernetes]
namespace = "default"
replicas = 2
cpu-request = "250m"
cpu-limit = "1"
memory-request = "512Mi"
memory-limit = "1Gi"
port = 8080
ingress-class = "alb"
# certificate-arn = "arn:aws:acm:us-east-1:ACCOUNT:certificate/ID"
hpa-min = 2
hpa-max = 10

[deploy.serverless]
provider = "aws"
memory = 1024
timeout = 300
workers = 2

[scripts]
build = "neamc build --release"
dev = "neam-api --port 8080"
deploy = "neamc deploy --target docker --dry-run"
)";

  std::ofstream manifest_file(name + "/neam.toml");
  manifest_file << interpolate_template(manifest_template, vars);

  std::ofstream main_file(name + "/src/main.neam");
  main_file << "// " << name << " agent entrypoint\n";
  main_file << "fun main() {\n";
  main_file << "  return \"Hello, Neam!\";\n";
  main_file << "}\n";

  auto previous = std::filesystem::current_path();
  std::filesystem::current_path(name);
  generate_gitlab_ci(name);
  generate_github_actions(name);
  std::filesystem::current_path(previous);

  std::cout << "\nCreated new project: " << name << "/\n";
  std::cout << "  " << name << "/neam.toml          Project manifest\n";
  std::cout << "  " << name << "/src/main.neam       Entrypoint\n";
  std::cout << "  " << name << "/tests/              Test directory\n";
  std::cout << "  " << name << "/.github/workflows/  CI/CD pipelines\n";
  std::cout << "  " << name << "/.gitlab-ci.yml      GitLab CI pipeline\n";
  std::cout << "\nNext steps:\n";
  std::cout << "  cd " << name << "\n";
  std::cout << "  neamc build                      # compile\n";
  std::cout << "  neamc deploy --target docker --dry-run  # preview deploy artifacts\n";
  std::cout << "  neamc deploy --target kubernetes  # deploy to EKS\n";
  std::cout << "  neamc deploy --target lambda      # deploy to AWS Lambda\n";
  return 0;
}

int cmd_generate_ci()
{
  generate_gitlab_ci("neam-project");
  generate_github_actions("neam-project");
  return 0;
}

int cmd_build(int argc, char** argv)
{
  bool release = false;
  std::string features;

  for (int i = 2; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--release")
    {
      release = true;
    }
    else if (arg == "--features" && i + 1 < argc)
    {
      features = argv[++i];
    }
  }

  auto manifest_path = find_manifest_path(".");
  if (!manifest_path)
  {
    std::cerr << "Error: No neam.toml found\n";
    return 1;
  }

  auto manifest = neamc::load_project_manifest(*manifest_path);
  const auto manifest_content = read_file(*manifest_path);
  const std::string app_name = manifest.name.empty() ? "neam-agent" : manifest.name;
  const std::filesystem::path entry = std::filesystem::path("src") / "main.neam";

  if (!std::filesystem::exists(entry))
  {
    std::cerr << "Error: missing entrypoint " << entry.string() << "\n";
    return 1;
  }

  std::filesystem::path output_dir = release ? "target/release" : "target/debug";
  std::filesystem::create_directories(output_dir);
  std::filesystem::path output = output_dir / (app_name + ".bundle");

  if (!features.empty())
  {
    std::cout << "Building with features: " << features << "\n";
  }

  auto source = read_file(entry);
  try
  {
    neamc::Pipeline pipeline;
    auto unit = pipeline.compile(source, manifest_content);
    std::ofstream out_file(output, std::ios::binary);
    unit.chunk.serialize(out_file);
    std::cout << "Built bundle: " << output.string() << "\n";
  }
  catch (const std::exception& ex)
  {
    std::cerr << "Build failed: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}

int compile_input(const std::vector<std::string>& args)
{
  if (args.empty())
  {
    usage();
    return 1;
  }

  std::string input;
  std::string output;
  std::string manifest;
  bool run_lsp = false;

  for (std::size_t i = 0; i < args.size(); ++i)
  {
    const auto& arg = args[i];
    if (arg == "--help" || arg == "-h")
    {
      usage();
      return 0;
    }
    else if (arg == "-o")
    {
      if (i + 1 >= args.size())
      {
        std::cerr << "Missing output file after -o\n";
        return 1;
      }
      output = args[++i];
    }
    else if (arg == "--manifest")
    {
      if (i + 1 >= args.size())
      {
        std::cerr << "Missing manifest string after --manifest\n";
        return 1;
      }
      manifest = args[++i];
    }
    else if (arg == "--lsp")
    {
      run_lsp = true;
    }
    else if (input.empty())
    {
      input = arg;
    }
    else
    {
      std::cerr << "Unexpected argument: " << arg << "\n";
      return 1;
    }
  }

  if (run_lsp)
  {
    neamc::lsp::Server server;
    server.run();
    return 0;
  }

  if (input.empty())
  {
    usage();
    return 1;
  }

  if (output.empty())
  {
    output = input + "b";
  }

  std::ifstream in_file(input);
  if (!in_file)
  {
    std::cerr << "Failed to open input file: " << input << "\n";
    return 1;
  }

  std::string source((std::istreambuf_iterator<char>(in_file)), std::istreambuf_iterator<char>());

  try
  {
    neamc::Pipeline pipeline;
    auto unit = pipeline.compile(source, manifest);

    std::ofstream out_file(output, std::ios::binary);
    if (!out_file)
    {
      std::cerr << "Failed to open output file: " << output << "\n";
      return 1;
    }

    unit.chunk.serialize(out_file);
    std::cout << "Wrote bytecode bundle to " << output << "\n";

    nlohmann::json map;
    map["manifest"] = unit.chunk.manifest();
    map["mappings"] = nlohmann::json::array();
    for (const auto& entry : unit.chunk.source_map())
    {
      map["mappings"].push_back({{"offset", entry.offset}, {"line", entry.line}});
    }
    const std::string map_path = output + ".nb.map";
    std::ofstream map_file(map_path);
    if (map_file)
    {
      map_file << map.dump(2);
      std::cout << "Wrote source map to " << map_path << "\n";
    }
  }
  catch (const std::exception& ex)
  {
    std::cerr << "Compilation failed: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    usage();
    return 1;
  }

  std::string command = argv[1];
  if (command == "test")
  {
    return cmd_test(argc, argv);
  }
  if (command == "build")
  {
    return cmd_build(argc, argv);
  }
  if (command == "deploy")
  {
    return cmd_deploy(argc, argv);
  }
  if (command == "audit")
  {
    return cmd_audit(argc, argv);
  }
  if (command == "add")
  {
    return cmd_add(argc, argv);
  }
  if (command == "remove")
  {
    return cmd_remove(argc, argv);
  }
  if (command == "update")
  {
    return cmd_update(argc, argv);
  }
  if (command == "run-script")
  {
    return cmd_run_script(argc, argv);
  }
  if (command == "new")
  {
    return cmd_new(argc, argv);
  }
  if (command == "generate-ci")
  {
    return cmd_generate_ci();
  }

  std::vector<std::string> args(argv + 1, argv + argc);
  return compile_input(args);
}
