# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-src")
  file(MAKE_DIRECTORY "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-src")
endif()
file(MAKE_DIRECTORY
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-build"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-subbuild/tree_sitter-populate-prefix"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-subbuild/tree_sitter-populate-prefix/tmp"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-subbuild/tree_sitter-populate-prefix/src/tree_sitter-populate-stamp"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-subbuild/tree_sitter-populate-prefix/src"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-subbuild/tree_sitter-populate-prefix/src/tree_sitter-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-subbuild/tree_sitter-populate-prefix/src/tree_sitter-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/tree_sitter-subbuild/tree_sitter-populate-prefix/src/tree_sitter-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
