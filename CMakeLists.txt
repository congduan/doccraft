cmake_minimum_required(VERSION 3.20)
project(doccraft LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Portability switches. The library (src/base plus the four core layers) builds
# on every target; the CLI needs a hosted C++ runtime with std::filesystem and
# stdio, and the tests need googletest from the network.
option(DOCCRAFT_BUILD_CLI "Build the doccraft command line tool" ON)
option(DOCCRAFT_BUILD_TESTS "Build the test suite (fetches googletest)" ON)
option(DOCCRAFT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

if(DOCCRAFT_BUILD_TESTS AND NOT DOCCRAFT_BUILD_CLI)
  # Every end-to-end test drives the CLI binary through $<TARGET_FILE:doccraft>.
  message(FATAL_ERROR
    "DOCCRAFT_BUILD_TESTS requires DOCCRAFT_BUILD_CLI: the test suite drives "
    "the CLI binary. Configure with -DDOCCRAFT_BUILD_TESTS=OFF for a "
    "library-only build.")
endif()

if(MSVC)
  # /utf-8: the tree is UTF-8 without BOM and the CLI prints UTF-8 diagnostics;
  # MSVC's default source/execution code page would mangle both. /W4 is used
  # instead of /Wall, which flags MSVC's own headers.
  add_compile_options(/W4 /permissive- /utf-8)
  if(DOCCRAFT_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(DOCCRAFT_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
endif()

# Needed by clang-tidy (scripts/lint.sh --tidy); must be set before the targets
# below are created.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Testing must be enabled before src/ so that add_test() in src/cli/ registers.
enable_testing()

add_subdirectory(src)

if(DOCCRAFT_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  # Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_subdirectory(tests)
endif()

# Style targets for the Google C++ Style Guide (.clang-format / .clang-tidy).
# `format-check` fails on any deviation and is meant to run in CI.
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
  file(GLOB_RECURSE DOCCRAFT_FORMAT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")
  # Machine-written schema tables; tools/schema-gen owns their layout.
  list(FILTER DOCCRAFT_FORMAT_SOURCES EXCLUDE REGEX "/generated/")
  add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${DOCCRAFT_FORMAT_SOURCES}
    COMMENT "clang-format: rewriting sources in place (Google style)")
  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror
            ${DOCCRAFT_FORMAT_SOURCES}
    COMMENT "clang-format: verifying Google style formatting")
else()
  message(STATUS "clang-format not found; format/format-check targets disabled")
endif()
