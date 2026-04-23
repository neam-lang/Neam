# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-src")
  file(MAKE_DIRECTORY "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-src")
endif()
file(MAKE_DIRECTORY
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-build"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-subbuild/lexbor-populate-prefix"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-subbuild/lexbor-populate-prefix/tmp"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-subbuild/lexbor-populate-prefix/src/lexbor-populate-stamp"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-subbuild/lexbor-populate-prefix/src"
  "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-subbuild/lexbor-populate-prefix/src/lexbor-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-subbuild/lexbor-populate-prefix/src/lexbor-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/praveengovindaraj/Neam_Home/repo/neam-nightly/build-windows-v14/_deps/lexbor-subbuild/lexbor-populate-prefix/src/lexbor-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
