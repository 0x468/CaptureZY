cmake_minimum_required(VERSION 3.22)

project(
  CaptureZY
  VERSION 0.1.0
  DESCRIPTION "Windows 原生截图与贴图工具"
  LANGUAGES CXX
)

option(CAPTUREZY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CAPTUREZY_ENABLE_LTO "Enable link time optimization when supported" OFF)
option(CAPTUREZY_ENABLE_CLANG_TIDY "Run clang-tidy during the build when available" OFF)
option(CAPTUREZY_FORCE_DIAGNOSTIC_SELF_TEST "Enable diagnostic self-test CLI hooks in all build configurations" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CAPTUREZY_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  else()
    message(WARNING "CAPTUREZY_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found.")
  endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CaptureZYOptions)

if(WIN32)
  add_subdirectory(src)
else()
  message(STATUS "CaptureZY 仅支持在 Windows 上生成可执行程序；当前仅执行工程配置校验。")
endif()
