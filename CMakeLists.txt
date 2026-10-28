cmake_minimum_required(VERSION 3.20)
project(doccraft LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Needed by clang-tidy (scripts/lint.sh --tidy); must be set before the targets
# below are created.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
# Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)

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
