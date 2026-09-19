# Switch dependency shims.
#
# devkitPro ships portlibs for most of what libultraship needs (SDL2, tinyxml2, zlib,
# bzip2, libpng, ogg/vorbis, glad, mesa, libdrm_nouveau), but there is no portlib for
# libzip, spdlog, fmt or nlohmann-json. Those get built from source here so the rest of
# the build can keep using the same namespaced targets it uses everywhere else.
#
# Verified against devkitPro/pacman-packages: switch/ contains SDL2 and TinyXML2 but no
# libzip, spdlog, fmt or json package.

include(FetchContent)

# newlib hides its POSIX declarations when __STRICT_ANSI__ is defined, which is what a
# plain -std=c++20 gives you. spdlog's bundled fmt calls fileno(), so the dependencies
# built here need GNU extensions and _GNU_SOURCE (newlib honours it via sys/features.h).
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_EXTENSIONS ON)
add_compile_definitions(_GNU_SOURCE)

set(SWITCH_DEPS_BUILD_TESTING OFF CACHE INTERNAL "")

#=================== nlohmann_json ===================
# Header only, so nothing to compile for aarch64. v3.12.0 rather than 3.11.x because
# 3.11's cmake_minimum_required(VERSION 3.1...3.14) is a hard error under CMake 4.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

#=================== spdlog ===================
# Uses its bundled fmt: there is no switch-fmt portlib either.
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
set(SPDLOG_FMT_EXTERNAL OFF CACHE INTERNAL "")
set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
set(SPDLOG_BUILD_TESTS OFF CACHE INTERNAL "")
set(SPDLOG_INSTALL OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(spdlog)

# Belt and braces: spdlog sets its own CXX_STANDARD on the target, which resets the
# extensions default, so pin them back on and define _GNU_SOURCE for the target itself.
if (TARGET spdlog)
    set_target_properties(spdlog PROPERTIES C_EXTENSIONS ON CXX_EXTENSIONS ON)
    target_compile_definitions(spdlog PRIVATE _GNU_SOURCE)
endif()

#=================== libzip ===================
# zlib comes from the switch-zlib portlib; everything optional is switched off so the
# build does not go looking for OpenSSL, zstd or lzma on a platform that has none.
# v1.11.4 declares a 3.10 minimum, so it configures cleanly under CMake 4 as well.
FetchContent_Declare(
    libzip
    GIT_REPOSITORY https://github.com/nih-at/libzip.git
    GIT_TAG v1.11.4
)
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(BUILD_TOOLS OFF CACHE INTERNAL "")
set(BUILD_REGRESS OFF CACHE INTERNAL "")
set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(BUILD_DOC OFF CACHE INTERNAL "")
set(BUILD_OSSFUZZ OFF CACHE INTERNAL "")
set(LIBZIP_DO_INSTALL OFF CACHE INTERNAL "")
set(ENABLE_FDOPEN OFF CACHE INTERNAL "")
set(ENABLE_COMMONCRYPTO OFF CACHE INTERNAL "")
set(ENABLE_GNUTLS OFF CACHE INTERNAL "")
set(ENABLE_MBEDTLS OFF CACHE INTERNAL "")
set(ENABLE_OPENSSL OFF CACHE INTERNAL "")
set(ENABLE_WINDOWS_CRYPTO OFF CACHE INTERNAL "")
set(ENABLE_BZIP2 OFF CACHE INTERNAL "")
set(ENABLE_LZMA OFF CACHE INTERNAL "")
set(ENABLE_ZSTD OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(libzip)

# libzip's CMake exports `zip`; the rest of the build expects the namespaced alias that
# find_package(libzip) would have provided.
if (TARGET zip AND NOT TARGET libzip::zip)
    add_library(libzip::zip ALIAS zip)
endif()
