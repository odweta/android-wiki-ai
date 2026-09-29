cmake_minimum_required(VERSION 3.22.1)
project("androidwikiai")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# llama.cpp: on-device GGUF model inference.
#
# Vendored via FetchContent (the same approach llama.cpp's own
# examples/llama.android uses for add_subdirectory-based integration),
# pinned to a specific release tag for reproducible builds. This requires
# network access the first time CMake configures this module on a clean
# checkout; the resulting app makes no network calls at runtime.
# ---------------------------------------------------------------------------
set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_LLAMAFILE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    llama
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG v0.5.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(llama)

# ---------------------------------------------------------------------------
# zstd: only the decompression path is needed, for reading Zstandard
# compressed ZIM clusters (the compression used by modern Kiwix ZIM dumps).
# ---------------------------------------------------------------------------
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(zstd)

# ---------------------------------------------------------------------------
# Our JNI bridge + custom lightweight ZIM reader.
# ---------------------------------------------------------------------------
add_library(androidwikiai SHARED
    jni_bridge.cpp
    llama_bridge.cpp
    llama_bridge.h
    zim_format.cpp
    zim_format.h
    zim_reader.cpp
    zim_reader.h
    html_strip.cpp
    html_strip.h
    text_similarity.h
)

target_link_libraries(androidwikiai
    llama
    libzstd_static
    android
    log
)
