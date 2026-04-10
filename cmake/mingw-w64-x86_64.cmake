# =============================================================================
# CMake Toolchain File: MinGW-w64 x86_64 Cross-Compilation (macOS -> Windows)
# =============================================================================
#
# Usage:
#   cmake -B build-windows \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
#         -DCMAKE_BUILD_TYPE=Release ..
#
# Prerequisites (macOS):
#   brew install mingw-w64
#   Download curl for Windows/MinGW from https://curl.se/windows/
#   Extract to build-windows/deps/curl-8.12.1_1-win64-mingw/
#
# The curl.se MinGW package bundles all required dependencies:
#   - LibreSSL 4.0.0 (OpenSSL-compatible: libssl.a, libcrypto.a, headers)
#   - zlib 1.3.1, zstd 1.5.6, brotli 1.1.0
#   - nghttp2 1.64.0, nghttp3 1.7.0, ngtcp2 1.10.0
#   - libssh2 1.11.1, libpsl 0.21.5
# =============================================================================

# -- Target system -----------------------------------------------------------
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# -- Cross-compiler toolchain ------------------------------------------------
set(MINGW_PREFIX      "x86_64-w64-mingw32")
set(MINGW_TOOLCHAIN   "/opt/homebrew/bin")
set(MINGW_SYSROOT     "/opt/homebrew/Cellar/mingw-w64/13.0.0_2/toolchain-x86_64/${MINGW_PREFIX}")

set(CMAKE_C_COMPILER   "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-gcc")
set(CMAKE_CXX_COMPILER "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-g++")
set(CMAKE_RC_COMPILER  "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-windres")
set(CMAKE_AR           "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-ar"      CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB       "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-ranlib"  CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP        "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-strip"   CACHE FILEPATH "Strip")
set(CMAKE_NM           "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-nm"      CACHE FILEPATH "NM")
set(CMAKE_DLLTOOL      "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-dlltool" CACHE FILEPATH "DLL tool")
set(CMAKE_OBJCOPY      "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-objcopy" CACHE FILEPATH "Objcopy")
set(CMAKE_OBJDUMP      "${MINGW_TOOLCHAIN}/${MINGW_PREFIX}-objdump" CACHE FILEPATH "Objdump")

# -- Sysroot and search paths ------------------------------------------------
set(CMAKE_SYSROOT "${MINGW_SYSROOT}")

# Prebuilt dependencies extracted from curl.se Windows/MinGW package
set(NEAM_WIN_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/../build-windows/deps/curl-8.12.1_1-win64-mingw")

# Tell CMake where to find headers/libraries for the target
set(CMAKE_FIND_ROOT_PATH
    "${MINGW_SYSROOT}"
    "${NEAM_WIN_DEPS_DIR}"
)

# Search for programs on the host, libraries/headers on the target only
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# -- CURL hints (static linking) ---------------------------------------------
set(CURL_INCLUDE_DIR    "${NEAM_WIN_DEPS_DIR}/include"       CACHE PATH     "CURL include directory")
set(CURL_LIBRARY        "${NEAM_WIN_DEPS_DIR}/lib/libcurl.a" CACHE FILEPATH "CURL static library")
set(CURL_USE_STATIC_LIBS ON CACHE BOOL "Use static libcurl")

# When statically linking curl, its transitive dependencies must be provided.
# The curl.se MinGW build links against these libraries:
set(CURL_STATIC_DEPS
    "${NEAM_WIN_DEPS_DIR}/lib/libssl.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libcrypto.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libnghttp2.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libnghttp3.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libngtcp2.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libngtcp2_crypto_libressl.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libssh2.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libpsl.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libbrotlidec.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libbrotlicommon.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libzstd.a"
    "${NEAM_WIN_DEPS_DIR}/lib/libz.a"
    # Windows system libraries required by curl and OpenSSL
    ws2_32      # Winsock2
    crypt32     # Windows crypto API (certificate stores)
    wldap32     # LDAP
    bcrypt      # BCrypt (LibreSSL backend)
    secur32     # SSPI (InitSecurityInterfaceA — curl 8.19+)
    iphlpapi    # if_nametoindex (curl 8.19+)
)

# -- OpenSSL / LibreSSL hints ------------------------------------------------
# The curl.se package ships LibreSSL 4.0.0 with OpenSSL-compatible headers.
# CMake's FindOpenSSL will find these via the hints below.
set(OPENSSL_ROOT_DIR       "${NEAM_WIN_DEPS_DIR}"                    CACHE PATH     "OpenSSL root")
set(OPENSSL_INCLUDE_DIR    "${NEAM_WIN_DEPS_DIR}/include"            CACHE PATH     "OpenSSL include dir")
set(OPENSSL_CRYPTO_LIBRARY "${NEAM_WIN_DEPS_DIR}/lib/libcrypto.a"    CACHE FILEPATH "OpenSSL crypto lib")
set(OPENSSL_SSL_LIBRARY    "${NEAM_WIN_DEPS_DIR}/lib/libssl.a"       CACHE FILEPATH "OpenSSL SSL lib")
set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "Use static OpenSSL/LibreSSL")

# -- Static linking defaults -------------------------------------------------
# Prefer static libraries for cross-compilation
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries by default")

# -- Platform flags ----------------------------------------------------------
# MinGW threading model; target Windows 7+
add_compile_definitions(_WIN32_WINNT=0x0601)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

# Suppress MinGW-specific warnings and define static-linking macros
add_compile_definitions(
    _CRT_SECURE_NO_WARNINGS
    CURL_STATICLIB           # Required when linking static libcurl on Windows
)
