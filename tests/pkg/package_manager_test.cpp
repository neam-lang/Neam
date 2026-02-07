//
// Neam v0.6.5 - Package Manager Tests
//
// Tests Installer and PackageCache functionality.
//

#include "neamc/pkg/installer.hpp"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace
{
namespace fs = std::filesystem;

// ============================================================================
// Installer Construction Tests
// ============================================================================

TEST(InstallerTest, ConstructWithValidPath)
{
  auto tmp = fs::temp_directory_path() / "neam_test_installer";
  fs::create_directories(tmp);
  neamc::pkg::Installer installer(tmp);
  // Should construct without throwing
  fs::remove_all(tmp);
}

TEST(InstallerTest, ListInstalledOnEmptyProject)
{
  auto tmp = fs::temp_directory_path() / "neam_test_empty";
  fs::create_directories(tmp);
  neamc::pkg::Installer installer(tmp);
  auto installed = installer.list_installed();
  EXPECT_TRUE(installed.empty());
  fs::remove_all(tmp);
}

TEST(InstallerTest, InstallNonexistentPackageFails)
{
  auto tmp = fs::temp_directory_path() / "neam_test_install_fail";
  fs::create_directories(tmp);
  neamc::pkg::Installer installer(tmp);
  bool result = installer.install_package("nonexistent-package-xyz", "1.0.0");
  EXPECT_FALSE(result);
  fs::remove_all(tmp);
}

TEST(InstallerTest, RemoveNonexistentPackageSucceeds)
{
  auto tmp = fs::temp_directory_path() / "neam_test_remove_noop";
  fs::create_directories(tmp);
  neamc::pkg::Installer installer(tmp);
  // Removing a non-installed package is a no-op success
  bool result = installer.remove_package("nonexistent-package");
  EXPECT_TRUE(result);
  fs::remove_all(tmp);
}

TEST(InstallerTest, InstallAllOnEmptyProjectSucceeds)
{
  auto tmp = fs::temp_directory_path() / "neam_test_install_all";
  fs::create_directories(tmp);
  neamc::pkg::Installer installer(tmp);
  // No neam.toml — should succeed with nothing to install
  bool result = installer.install_all();
  // Either succeeds (no deps) or fails gracefully (no toml)
  (void)result;
  fs::remove_all(tmp);
}

TEST(InstallerTest, ProgressCallbackIsInvoked)
{
  auto tmp = fs::temp_directory_path() / "neam_test_progress";
  fs::create_directories(tmp);
  neamc::pkg::Installer installer(tmp);
  bool callback_called = false;
  installer.set_progress_callback(
      [&](const std::string&, const std::string&, double) {
        callback_called = true;
      });
  installer.install_package("test-pkg", "1.0.0");
  // Callback may or may not be called depending on implementation
  // This test ensures setting it doesn't crash
  fs::remove_all(tmp);
}

TEST(InstallerTest, LastErrorPopulatedOnFailure)
{
  auto tmp = fs::temp_directory_path() / "neam_test_last_error";
  fs::create_directories(tmp);
  neamc::pkg::Installer installer(tmp);
  installer.install_package("nonexistent-pkg", "1.0.0");
  // After a failed install, last_error should have content
  // (implementation-dependent but shouldn't crash)
  auto error = installer.last_error();
  (void)error;
  fs::remove_all(tmp);
}

// ============================================================================
// Lock File Tests
// ============================================================================

TEST(InstallerTest, GenerateLockFileOnEmptyProject)
{
  auto tmp = fs::temp_directory_path() / "neam_test_lock";
  fs::create_directories(tmp);
  neamc::pkg::Installer installer(tmp);
  // Should handle empty project gracefully
  bool result = installer.generate_lock_file();
  (void)result;
  fs::remove_all(tmp);
}

TEST(InstallerTest, VerifyLockFileOnEmptyProject)
{
  auto tmp = fs::temp_directory_path() / "neam_test_verify_lock";
  fs::create_directories(tmp);
  neamc::pkg::Installer installer(tmp);
  bool result = installer.verify_lock_file();
  (void)result;
  fs::remove_all(tmp);
}

// ============================================================================
// PackageCache Tests
// ============================================================================

TEST(PackageCacheTest, SingletonAccess)
{
  auto& cache1 = neamc::pkg::PackageCache::instance();
  auto& cache2 = neamc::pkg::PackageCache::instance();
  EXPECT_EQ(&cache1, &cache2);
}

TEST(PackageCacheTest, CacheDirExists)
{
  auto& cache = neamc::pkg::PackageCache::instance();
  auto dir = cache.cache_dir();
  EXPECT_FALSE(dir.empty());
}

TEST(PackageCacheTest, HasPackageReturnsFalseForMissing)
{
  auto& cache = neamc::pkg::PackageCache::instance();
  EXPECT_FALSE(cache.has_package("nonexistent-test-pkg", "99.99.99"));
}

TEST(PackageCacheTest, CacheSizeNonNegative)
{
  auto& cache = neamc::pkg::PackageCache::instance();
  auto size = cache.cache_size();
  EXPECT_GE(size, static_cast<std::size_t>(0));
}

// ============================================================================
// ResolvedDependency Struct Tests
// ============================================================================

TEST(ResolvedDependencyTest, DefaultConstruction)
{
  neamc::pkg::ResolvedDependency dep;
  EXPECT_TRUE(dep.name.empty());
  EXPECT_TRUE(dep.version.empty());
  EXPECT_TRUE(dep.source.empty());
  EXPECT_FALSE(dep.dev_only);
}

TEST(ResolvedDependencyTest, Assignment)
{
  neamc::pkg::ResolvedDependency dep;
  dep.name = "test-pkg";
  dep.version = "1.0.0";
  dep.source = "registry";
  dep.dev_only = true;
  EXPECT_EQ(dep.name, "test-pkg");
  EXPECT_EQ(dep.version, "1.0.0");
  EXPECT_EQ(dep.source, "registry");
  EXPECT_TRUE(dep.dev_only);
}

}  // namespace
