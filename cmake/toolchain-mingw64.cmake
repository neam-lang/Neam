# CMake Toolchain File for cross-compiling to Windows x86_64 using MinGW-w64

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Compilers
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Target environment - include pre-built Windows libraries
set(CURL_WIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/windows/curl-8.11.1_1-win64-mingw")

set(CMAKE_FIND_ROOT_PATH
    /opt/homebrew/opt/mingw-w64
    ${CURL_WIN_DIR}
)

# Search for programs on the host
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers on the target
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Pre-set CURL location
set(CURL_LIBRARY "${CURL_WIN_DIR}/lib/libcurl.a")
set(CURL_INCLUDE_DIR "${CURL_WIN_DIR}/include")
set(CURL_FOUND TRUE)

# Pre-set OpenSSL location
set(OPENSSL_ROOT_DIR "${CURL_WIN_DIR}")
set(OPENSSL_INCLUDE_DIR "${CURL_WIN_DIR}/include")
set(OPENSSL_CRYPTO_LIBRARY "${CURL_WIN_DIR}/lib/libcrypto.a")
set(OPENSSL_SSL_LIBRARY "${CURL_WIN_DIR}/lib/libssl.a")
set(OPENSSL_FOUND TRUE)

# Static linking of C++ runtime for portability
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")
set(CMAKE_SHARED_LINKER_FLAGS "-static-libgcc -static-libstdc++")

# Windows-specific
add_definitions(-DWIN32 -D_WIN32 -D_WINDOWS)
add_definitions(-DWINVER=0x0601 -D_WIN32_WINNT=0x0601)  # Windows 7+
add_definitions(-DCURL_STATICLIB)
