cmake_minimum_required(VERSION 4.0)

# Enable experimental `import std;` support (gate UUID is CMake-version specific).
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

project(FifteenPuzzle LANGUAGES C CXX)

# The code only uses C++23 features (modules, `import std`, expected, jthread,
# stop_token). Use C++26 on Clang/GCC, but C++23 on MSVC: CMake 4.2 does not
# support the C++26 dialect or `import std` at C++26 for MSVC, while C++23 works.
if(MSVC)
  set(CMAKE_CXX_STANDARD 23)
else()
  set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# On Clang/GCC the `std` module CMake builds for `import std;` uses the default
# dialect (gnu++NN), so our module units must enable extensions to load the same
# std.pcm. MSVC's modules require conformance mode instead, so keep them off.
if(MSVC)
  set(CMAKE_CXX_EXTENSIONS OFF)
  # C4161 (#pragma pack pop imbalance) is emitted from MSVC's own ucrt/stdarg
  # headers while building the `std` module on recent toolchains — benign and
  # not from our code. Suppress it project-wide so the build is warning-clean.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4161")
else()
  set(CMAKE_CXX_EXTENSIONS ON)
endif()

# Cache compilations when ccache is available. The heavy third-party sources
# (raylib/openal) are cached across clean builds, which is the bulk of the build
# time; our own module units recompile (ccache does not cache C++ modules).
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()

# Some system package configs (e.g. Debian's OpenALConfig.cmake) and older
# dependency CMakeLists still declare cmake_minimum_required below 3.5, which
# CMake 4.x rejects ("Compatibility with CMake < 3.5 has been removed"). Clamp
# those old minimums so such packages still load.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

# Dependency strategy:
#   * Default (dev / CI): resolve raylib + openal-soft system-first via
#     find_package (installed by brew/apt/vcpkg). This skips compiling ~170
#     third-party translation units and links them dynamically — fast configure,
#     fast linking. Falls back to building from pinned sources if not installed.
#   * FIFTEEN_STATIC_DEPS=ON (release): always build the deps from source and
#     link them statically, producing a self-contained, optimized binary.
option(FIFTEEN_STATIC_DEPS "Build dependencies from source and link them statically (release binaries)" OFF)

# --- raylib -------------------------------------------------------------------
if(NOT FIFTEEN_STATIC_DEPS)
  find_package(raylib QUIET)
endif()
if(raylib_FOUND)
  message(STATUS "raylib: using system package")
else()
  message(STATUS "raylib: building from source (static=${FIFTEEN_STATIC_DEPS})")
  FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
  )
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)  # static raylib
  FetchContent_MakeAvailable(raylib)
endif()

# --- OpenAL -------------------------------------------------------------------
# `OpenAL::OpenAL` is exported by openal-soft's CMake config; the source build
# names the target `OpenAL`. Normalize to ${OPENAL_TARGET} for linking.
if(NOT FIFTEEN_STATIC_DEPS)
  find_package(OpenAL CONFIG QUIET)
endif()
if(OpenAL_FOUND)
  message(STATUS "OpenAL: using system package")
  set(OPENAL_TARGET OpenAL::OpenAL)
else()
  message(STATUS "OpenAL: building openal-soft from source (static=${FIFTEEN_STATIC_DEPS})")
  FetchContent_Declare(
      openal-soft
      GIT_REPOSITORY https://github.com/kcat/openal-soft.git
      GIT_TAG 1.24.2
      GIT_SHALLOW TRUE
  )
  set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
  set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
  set(ALSOFT_NO_CONFIG_UTIL ON CACHE BOOL "" FORCE)
  set(ALSOFT_EAX OFF CACHE BOOL "" FORCE)  # legacy EAX extension we don't use — trims the build
  if(FIFTEEN_STATIC_DEPS)
    set(LIBTYPE STATIC CACHE STRING "" FORCE)  # static openal-soft
  endif()

  # Workarounds for building openal-soft 1.24.2 with upstream Clang/libc++,
  # applied to this subtree only and restored before our own (module) targets:
  #   * -include cstdlib   : bundled fmt 11.1.1 relies on a transitive <cstdlib>
  #                          that newer libc++ no longer provides.
  #   * -Wno-elaborated-enum-base : Apple's CF_ENUM macros trip a diagnostic that
  #                          upstream Clang treats as an error.
  #   * -D__LITTLE_ENDIAN__ etc. (Apple): upstream Clang doesn't predefine these
  #                          Apple macros that CoreFoundation headers test with
  #                          `#if`, so openal's -Werror=undef errors out. Defining
  #                          them (arm64/x86_64 = little-endian, LP64) avoids the
  #                          warning entirely — order-independent, unlike -Wno-*.
  # These are Clang/GCC flags; MSVC does not need (or understand) them.
  set(_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
  if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include cstdlib -Wno-elaborated-enum-base")
    if(APPLE)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__LITTLE_ENDIAN__=1 -D__BIG_ENDIAN__=0 -D__LLP64__=0")
    endif()
  endif()
  FetchContent_MakeAvailable(openal-soft)
  set(CMAKE_CXX_FLAGS "${_saved_cxx_flags}")
  set(OPENAL_TARGET OpenAL)

  # openal-soft hard-codes -Werror=undef (and friends). Upstream Clang doesn't
  # predefine the Apple-specific macros that CoreFoundation headers test with
  # `#if`, so those become errors. Demote openal's warnings-as-errors on its own
  # target — applied after openal's own options, so this one wins the ordering.
  if(NOT MSVC AND TARGET OpenAL)
    target_compile_options(OpenAL PRIVATE -Wno-error)
  endif()
endif()

# --- nlohmann/json (header-only) ----------------------------------------------
# Used by the Sharing file strategy and the network client for (de)serialization.
# Header-only, so it is resolved the same way under static and dynamic builds.
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
  message(STATUS "nlohmann_json: using system package")
else()
  message(STATUS "nlohmann_json: building from source")
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
  )
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- SQLite3 ------------------------------------------------------------------
# Local leaderboard/stats database. find_package first (system libsqlite3 on
# macOS/Linux, vcpkg on Windows); otherwise vendor the official amalgamation as
# a tiny static C library, sidestepping SQLite's own CMake entirely.
if(NOT FIFTEEN_STATIC_DEPS)
  find_package(SQLite3 QUIET)
endif()
if(SQLite3_FOUND)
  message(STATUS "SQLite3: using system package")
  set(SQLITE_TARGET SQLite::SQLite3)
else()
  message(STATUS "SQLite3: building amalgamation from source")
  # Fallback only — every CI runner has system SQLite (macOS/Linux) or vcpkg
  # (Windows), so this path is a safety net. Bump the version if it is ever hit.
  FetchContent_Declare(
    sqlite_amalgamation
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3470200.zip
  )
  FetchContent_MakeAvailable(sqlite_amalgamation)
  add_library(sqlite3_amalg STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
  target_include_directories(sqlite3_amalg PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
  target_compile_definitions(sqlite3_amalg PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
  set(SQLITE_TARGET sqlite3_amalg)
endif()

# --- CURL ---------------------------------------------------------------------
# HTTP transport for the network client. Always resolved via find_package (even
# for static release builds): system libcurl ships on every CI runner and via
# vcpkg, whereas a from-source static curl would also need a TLS backend built
# and configured — out of scope here. The network client links it dynamically.
find_package(CURL REQUIRED)
message(STATUS "CURL: using ${CURL_VERSION_STRING}")

# Helper: declare a C++20-module library from a list of module interface units.
# `import std;` is enabled per target (not globally) so it does not leak into the
# fetched third-party dependencies.
# Targets inherit the standard from the global CMAKE_CXX_STANDARD (C++26 on
# Clang/GCC, C++23 on MSVC). We do not call target_compile_features(cxx_std_26):
# CMake does not list it as a known feature for MSVC, which breaks that build.
function(add_module_library name)
  add_library(${name})
  target_sources(${name} PUBLIC FILE_SET CXX_MODULES FILES ${ARGN})
  set_target_properties(${name} PROPERTIES CXX_MODULE_STD ON)
endfunction()

# --- Core libraries -----------------------------------------------------------

add_module_library(Dependencies
  src/Dependencies/Dependencies-Core.cppm
  src/Dependencies/Dependencies-DateGenerator.cppm
  src/Dependencies/Dependencies-RandomNumberGenerator.cppm
  src/Dependencies/Dependencies.cppm
)

add_module_library(ComposableArchitecture
  src/ComposableArchitecture/ComposableArchitecture-CasePath.cppm
  src/ComposableArchitecture/ComposableArchitecture-Store.cppm
  src/ComposableArchitecture/ComposableArchitecture-Feature.cppm
  src/ComposableArchitecture/ComposableArchitecture-Runtime.cppm
  src/ComposableArchitecture/ComposableArchitecture-Scope.cppm
  src/ComposableArchitecture/ComposableArchitecture-TestStore.cppm
  src/ComposableArchitecture/ComposableArchitecture.cppm
)
target_link_libraries(ComposableArchitecture PUBLIC Dependencies)

# Plain value types shared by the network + database clients.
add_module_library(SharedModels src/SharedModels/SharedModels.cppm)

# --- Sharing (persisted shared state) -----------------------------------------

add_module_library(Sharing
  src/Sharing/Sharing-Strategy.cppm
  src/Sharing/Sharing-Shared.cppm
  src/Sharing/Sharing.cppm
)

add_module_library(AppSettings src/Sharing/AppSettings.cppm)

# The JSON serializer is confined to the implementation unit (.cpp), so nlohmann
# is linked PRIVATE and never leaks into AppSettingsLive's reachable interface.
add_module_library(AppSettingsLive src/Sharing/AppSettingsLive.cppm)
target_sources(AppSettingsLive PRIVATE src/Sharing/AppSettingsLive.cpp)
target_link_libraries(AppSettingsLive PUBLIC AppSettings Sharing PRIVATE nlohmann_json::nlohmann_json)

# --- SQLite database client ---------------------------------------------------

# Low-level SQLite wrapper. Its methods are header-inline, so the sqlite3 symbols
# are needed wherever it is used — link the target PUBLIC so consumers pick it up.
add_module_library(Sqlite src/Sqlite/Sqlite.cppm)
target_link_libraries(Sqlite PUBLIC ${SQLITE_TARGET})

add_module_library(DatabaseClient src/DatabaseClient/DatabaseClient.cppm)
target_link_libraries(DatabaseClient PUBLIC Dependencies SharedModels)

add_module_library(DatabaseClientLive src/DatabaseClient/DatabaseClientLive.cppm)
target_link_libraries(DatabaseClientLive PUBLIC DatabaseClient Sqlite)

# --- Clients ------------------------------------------------------------------

add_module_library(AudioPlayerClient src/AudioPlayerClient/AudioPlayerClient.cppm)
target_link_libraries(AudioPlayerClient PUBLIC Dependencies)

add_module_library(AudioPlayerClientLive src/AudioPlayerClient/AudioPlayerClientLive.cppm)
target_link_libraries(AudioPlayerClientLive PUBLIC AudioPlayerClient PRIVATE ${OPENAL_TARGET})
if(FIFTEEN_STATIC_DEPS)
  target_compile_definitions(AudioPlayerClientLive PRIVATE AL_LIBTYPE_STATIC)  # link openal-soft statically
endif()

add_module_library(SolverClient src/SolverClient/SolverClient.cppm)
target_link_libraries(SolverClient PUBLIC Dependencies)

add_module_library(SolverClientLive src/SolverClient/SolverClientLive.cppm)
target_link_libraries(SolverClientLive PUBLIC SolverClient)

# Network client. curl + JSON are confined to the implementation unit (.cpp),
# so they are linked PRIVATE and never leak into ApiClientLive's interface.
add_module_library(ApiClient src/ApiClient/ApiClient.cppm)
target_link_libraries(ApiClient PUBLIC Dependencies SharedModels)

add_module_library(ApiClientLive src/ApiClient/ApiClientLive.cppm)
target_sources(ApiClientLive PRIVATE src/ApiClient/ApiClientLive.cpp)
target_link_libraries(ApiClientLive PUBLIC ApiClient PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

# --- Features -----------------------------------------------------------------

add_module_library(PuzzleFeature src/Features/Puzzle/PuzzleFeature.cppm)
# Implementation unit: editing reducer logic here recompiles only this object,
# leaving the module's BMI (and therefore its importers) untouched.
target_sources(PuzzleFeature PRIVATE src/Features/Puzzle/PuzzleFeature.cpp)
target_link_libraries(PuzzleFeature PUBLIC ComposableArchitecture AudioPlayerClient SolverClient Sharing AppSettings)

add_module_library(PuzzleFeatureView src/Features/Puzzle/PuzzleFeatureView.cppm)
target_link_libraries(PuzzleFeatureView PUBLIC PuzzleFeature raylib)

add_module_library(LeaderboardFeature src/Features/Leaderboard/LeaderboardFeature.cppm)
target_sources(LeaderboardFeature PRIVATE src/Features/Leaderboard/LeaderboardFeature.cpp)
target_link_libraries(LeaderboardFeature PUBLIC ComposableArchitecture ApiClient DatabaseClient SharedModels)

add_module_library(LeaderboardFeatureView src/Features/Leaderboard/LeaderboardFeatureView.cppm)
target_link_libraries(LeaderboardFeatureView PUBLIC LeaderboardFeature raylib)

add_module_library(AppFeature src/Features/App/AppFeature.cppm)
target_link_libraries(AppFeature PUBLIC PuzzleFeature LeaderboardFeature SharedModels Sharing AppSettings)

add_module_library(AppFeatureView src/Features/App/AppFeatureView.cppm)
target_link_libraries(AppFeatureView PUBLIC AppFeature PuzzleFeatureView LeaderboardFeatureView raylib)

# --- Executable ---------------------------------------------------------------

add_executable(${PROJECT_NAME} main.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(${PROJECT_NAME} PRIVATE
  AppFeatureView AppFeature PuzzleFeature LeaderboardFeature ComposableArchitecture
  AudioPlayerClient AudioPlayerClientLive
  SolverClient SolverClientLive
  ApiClient ApiClientLive
  DatabaseClient DatabaseClientLive
  Sharing AppSettings AppSettingsLive
)

# --- Tests --------------------------------------------------------------------
#
# Tests are EXCLUDE_FROM_ALL, so a normal `cmake --build` (and the Xcode/run
# flow) never recompiles them — they build only when actually requested:
#   cmake --build --preset macos --target PuzzleFeatureTests
#   cmake --workflow --preset test     # configure + build tests + run them
# The `add_test` registration itself is free (no compilation), so CTest still
# knows about the suite.

enable_testing()

add_executable(PuzzleFeatureTests EXCLUDE_FROM_ALL tests/PuzzleFeatureTests.cpp)
set_target_properties(PuzzleFeatureTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(PuzzleFeatureTests PRIVATE PuzzleFeature ComposableArchitecture)

add_test(NAME PuzzleFeatureTests COMMAND PuzzleFeatureTests)

add_executable(SolverClientTests EXCLUDE_FROM_ALL tests/SolverClientTests.cpp)
set_target_properties(SolverClientTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(SolverClientTests PRIVATE SolverClient SolverClientLive)

add_test(NAME SolverClientTests COMMAND SolverClientTests)

add_executable(SharingTests EXCLUDE_FROM_ALL tests/SharingTests.cpp)
set_target_properties(SharingTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(SharingTests PRIVATE Sharing AppSettings AppSettingsLive)

add_test(NAME SharingTests COMMAND SharingTests)

add_executable(DatabaseClientTests EXCLUDE_FROM_ALL tests/DatabaseClientTests.cpp)
set_target_properties(DatabaseClientTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(DatabaseClientTests PRIVATE DatabaseClient DatabaseClientLive)

add_test(NAME DatabaseClientTests COMMAND DatabaseClientTests)

add_executable(LeaderboardFeatureTests EXCLUDE_FROM_ALL tests/LeaderboardFeatureTests.cpp)
set_target_properties(LeaderboardFeatureTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(LeaderboardFeatureTests PRIVATE LeaderboardFeature ComposableArchitecture ApiClient DatabaseClient SharedModels)

add_test(NAME LeaderboardFeatureTests COMMAND LeaderboardFeatureTests)
