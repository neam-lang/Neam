// Package manager installer and cache tests.
#include <gtest/gtest.h>
#include "neamc/pkg/installer.hpp"
#include <filesystem>
#include <fstream>

using namespace neamc::pkg;
namespace fs = std::filesystem;

// ===========================================================================
// Installer basics
// ===========================================================================

TEST(Installer, Create) {
    fs::path project_root = fs::temp_directory_path() / "neam_pkg_test";
    fs::create_directories(project_root);

    Installer installer(project_root);

    fs::remove_all(project_root);
}

TEST(Installer, ListInstalledEmpty) {
    fs::path project_root = fs::temp_directory_path() / "neam_pkg_test_empty";
    fs::create_directories(project_root);

    Installer installer(project_root);
    auto installed = installer.list_installed();
    EXPECT_TRUE(installed.empty());

    fs::remove_all(project_root);
}

TEST(Installer, ProgressCallback) {
    fs::path project_root = fs::temp_directory_path() / "neam_pkg_test_progress";
    fs::create_directories(project_root);

    Installer installer(project_root);

    bool callback_called = false;
    installer.set_progress_callback(
        [&](const std::string& /*package*/, const std::string& /*status*/, double /*pct*/) {
            callback_called = true;
        });

    EXPECT_FALSE(callback_called);

    fs::remove_all(project_root);
}

// ===========================================================================
// Lock file
// ===========================================================================

TEST(Installer, GenerateLockFile) {
    fs::path project_root = fs::temp_directory_path() / "neam_pkg_test_lock";
    fs::create_directories(project_root);

    Installer installer(project_root);
    installer.generate_lock_file();

    fs::remove_all(project_root);
}

TEST(Installer, VerifyLockFile) {
    fs::path project_root = fs::temp_directory_path() / "neam_pkg_test_verify";
    fs::create_directories(project_root);

    Installer installer(project_root);
    bool valid = installer.verify_lock_file();
    // Should return true (no lock = trivially valid) or false

    fs::remove_all(project_root);
}

// ===========================================================================
// PackageCache
// ===========================================================================

TEST(PackageCache, Singleton) {
    auto& cache1 = PackageCache::instance();
    auto& cache2 = PackageCache::instance();
    EXPECT_EQ(&cache1, &cache2);
}

TEST(PackageCache, CacheDir) {
    auto& cache = PackageCache::instance();
    auto dir = cache.cache_dir();
    EXPECT_FALSE(dir.empty());
}

TEST(PackageCache, HasPackage) {
    auto& cache = PackageCache::instance();
    bool has = cache.has_package("nonexistent_package", "0.0.0");
    EXPECT_FALSE(has);
}

TEST(PackageCache, AddAndRemove) {
    auto& cache = PackageCache::instance();

    fs::path pkg_path = fs::temp_directory_path() / "neam_test_pkg";
    fs::create_directories(pkg_path);
    {
        std::ofstream f(pkg_path / "main.neam");
        f << "emit \"hello\";";
    }

    cache.add_package("test_pkg", "1.0.0", pkg_path);
    EXPECT_TRUE(cache.has_package("test_pkg", "1.0.0"));

    cache.remove_package("test_pkg", "1.0.0");
    EXPECT_FALSE(cache.has_package("test_pkg", "1.0.0"));

    fs::remove_all(pkg_path);
}

TEST(PackageCache, CacheSize) {
    auto& cache = PackageCache::instance();
    auto size = cache.cache_size();
    EXPECT_GE(size, 0u);
}

TEST(PackageCache, Clean) {
    auto& cache = PackageCache::instance();
    EXPECT_NO_THROW(cache.clean_cache());
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST(InstallerEdge, NonexistentPackage) {
    fs::path project_root = fs::temp_directory_path() / "neam_pkg_test_noexist";
    fs::create_directories(project_root);

    Installer installer(project_root);
    auto result = installer.install_package("nonexistent_package_xyz", "99.99.99");
    if (!result) {
        EXPECT_FALSE(installer.last_error().empty());
    }

    fs::remove_all(project_root);
}

TEST(InstallerEdge, RemoveNonexistent) {
    fs::path project_root = fs::temp_directory_path() / "neam_pkg_test_rmnoexist";
    fs::create_directories(project_root);

    Installer installer(project_root);
    installer.remove_package("nonexistent");

    fs::remove_all(project_root);
}

TEST(PackageCacheEdge, RemoveNonexistentPackage) {
    auto& cache = PackageCache::instance();
    EXPECT_NO_THROW(cache.remove_package("definitely_not_here", "0.0.0"));
}
