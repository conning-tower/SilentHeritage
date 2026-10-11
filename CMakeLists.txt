cmake_minimum_required(VERSION 3.25)
project(silent_heritage LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(SH_BUILD_TESTS "Build the unit tests" ON)
option(SH_SANITIZE "Use ASan/UBSan in debug builds (Linux only)" ON)
# Not for the game: a test build tracing the SH.EXE / SE.EXE addresses the
# code reads (tests/synthetic, SH_TRACE_EXE_READS).
option(SH_TRACE_EXE_READS "Trace the reads of the original executables (test builds)" OFF)

# The edition built: its product name (window title, messages, Android
# label), SDL per-user folder, executable and application id, in the
# generated header core/edition.h and, for android/build_apk.sh, edition.sh in
# the build directory.
#   heritage  Silent Heritage, the distributed edition: reads the player's
#             own install of the original game.
#   hunter    Silent Hunter: a local build for testing and development,
#             with the original game files embedded (Android package) and
#             the game-art launcher icon. It contains copyrighted files and
#             uses the original's trademarked name: never distribute it.
#             Embedded game files never include the install's SAVE folder
#             (the player's saves, options and best missions), on Android
#             (android/build_apk.sh) as in any future desktop packaging.
set(SH_EDITION heritage CACHE STRING
  "The edition built: heritage (Silent Heritage, asset-free) or hunter (Silent Hunter, original assets embedded)")
set_property(CACHE SH_EDITION PROPERTY STRINGS heritage hunter)
if(SH_EDITION STREQUAL "heritage")
  set(SH_EDITION_NAME "Silent Heritage")
  set(SH_EDITION_EXECUTABLE silent_heritage)
  set(SH_EDITION_APP_ID com.conningtower.silentheritage)
elseif(SH_EDITION STREQUAL "hunter")
  set(SH_EDITION_NAME "Silent Hunter")
  set(SH_EDITION_EXECUTABLE silent_hunter)
  set(SH_EDITION_APP_ID com.conningtower.silenthunter)
else()
  message(FATAL_ERROR "SH_EDITION must be heritage or hunter, not '${SH_EDITION}'")
endif()
set(SH_EDITION_PREF_ORG "Conning Tower")

# The variant built, orthogonal to the edition, in core/edition.h and
# edition.sh:
#   faithful  the original game, its rules, screens and limits.
#   extended  the same game with the improvements beyond the original
#             (core/features.h), under its own name, executable,
#             application id and per-user folder, so that it installs next
#             to the faithful build and never shares its saves.
set(SH_VARIANT faithful CACHE STRING
  "The variant built: faithful (the original game) or extended (with the improvements of core/features.h)")
set_property(CACHE SH_VARIANT PROPERTY STRINGS faithful extended)
if(SH_VARIANT STREQUAL "faithful")
  set(SH_VARIANT_EXTENDED false)
elseif(SH_VARIANT STREQUAL "extended")
  set(SH_VARIANT_EXTENDED true)
  string(APPEND SH_EDITION_NAME " Extended")
  string(APPEND SH_EDITION_EXECUTABLE _extended)
  string(APPEND SH_EDITION_APP_ID .extended)
else()
  message(FATAL_ERROR "SH_VARIANT must be faithful or extended, not '${SH_VARIANT}'")
endif()
configure_file(src/core/edition.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/core/edition.h @ONLY)
configure_file(cmake/edition.sh.in ${CMAKE_CURRENT_BINARY_DIR}/edition.sh @ONLY)

include(FetchContent)

# Third-party dependencies, pinned and built from source.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.18
  GIT_SHALLOW TRUE
  SYSTEM)
FetchContent_MakeAvailable(SDL3)

if(SH_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE
    SYSTEM)
  FetchContent_MakeAvailable(Catch2)
endif()

# Warning and sanitizer flags for our own code only (not the dependencies).
add_library(sh_options INTERFACE)
target_compile_options(sh_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wimplicit-int-conversion
  -Wshorten-64-to-32 -Werror)
target_include_directories(sh_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(SH_SANITIZE AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT WIN32)
  target_compile_options(sh_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(sh_options INTERFACE -fsanitize=address,undefined)
endif()
if(WIN32)
  # A self-contained .exe: no dependency on the mingw runtime DLLs.
  target_link_options(sh_options INTERFACE -static)
  # mingw's <intrin.h> (pulled in by the SDL headers) includes clang's
  # deprecated <mm3dnow.h>, whose #warning would fail the build.
  target_compile_options(sh_options INTERFACE -Wno-\#warnings)
endif()

# Declares module library `name` from SOURCES, linking DEPS publicly. A
# module without sources yet is an interface library carrying its DEPS.
function(sh_add_module name)
  cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
  if(ARG_SOURCES)
    add_library(${name} STATIC ${ARG_SOURCES})
    target_link_libraries(${name} PUBLIC sh_options ${ARG_DEPS})
  else()
    add_library(${name} INTERFACE)
    target_link_libraries(${name} INTERFACE sh_options ${ARG_DEPS})
  endif()
endfunction()

add_subdirectory(src/core)
add_subdirectory(src/gfx)
add_subdirectory(src/view)
add_subdirectory(src/assets)
add_subdirectory(src/audio)
add_subdirectory(src/platform)
add_subdirectory(src/sim)
add_subdirectory(src/ui)
add_subdirectory(src/game)
add_subdirectory(src/editor)
add_subdirectory(src/app)
if(NOT ANDROID)
  add_subdirectory(tools)
endif()

if(SH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
