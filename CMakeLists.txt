cmake_minimum_required(VERSION 4.0)

# Enable experimental `import std;` support (gate UUID is CMake-version specific).
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

project(FifteenPuzzle LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The `std` module CMake builds for `import std;` uses the compiler's default
# dialect (gnu++26); our module units must match it to load the same std.pcm.
set(CMAKE_CXX_EXTENSIONS ON)

include(FetchContent)

FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE
)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(raylib)

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

# Workarounds for building openal-soft 1.24.2 with upstream Clang against a very
# new Apple SDK, applied to this subtree only and restored before our own
# (module) targets are defined:
#   * -include cstdlib   : bundled fmt 11.1.1 relies on a transitive <cstdlib>
#                          that newer libc++ no longer provides.
#   * -Wno-elaborated-enum-base : Apple's CF_ENUM macros trip a diagnostic that
#                          upstream Clang treats as an error.
set(_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include cstdlib -Wno-elaborated-enum-base")
FetchContent_MakeAvailable(openal-soft)
set(CMAKE_CXX_FLAGS "${_saved_cxx_flags}")

# Helper: declare a C++20-module library from a list of module interface units.
# `import std;` is enabled per target (not globally) so it does not leak into the
# fetched third-party dependencies.
function(add_module_library name)
  add_library(${name})
  target_sources(${name} PUBLIC FILE_SET CXX_MODULES FILES ${ARGN})
  target_compile_features(${name} PUBLIC cxx_std_26)
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
  src/ComposableArchitecture/ComposableArchitecture-Effect.cppm
  src/ComposableArchitecture/ComposableArchitecture-Reducer.cppm
  src/ComposableArchitecture/ComposableArchitecture-Store.cppm
  src/ComposableArchitecture/ComposableArchitecture-TestStore.cppm
  src/ComposableArchitecture/ComposableArchitecture.cppm
)
target_link_libraries(ComposableArchitecture PUBLIC Dependencies)

# --- Clients ------------------------------------------------------------------

add_module_library(AudioPlayerClient src/AudioPlayerClient/AudioPlayerClient.cppm)
target_link_libraries(AudioPlayerClient PUBLIC Dependencies)

add_module_library(AudioPlayerClientLive src/AudioPlayerClient/AudioPlayerClientLive.cppm)
target_link_libraries(AudioPlayerClientLive PUBLIC AudioPlayerClient PRIVATE OpenAL)

# --- Features -----------------------------------------------------------------

add_module_library(PuzzleFeature src/Features/Puzzle/PuzzleFeature.cppm)
target_link_libraries(PuzzleFeature PUBLIC ComposableArchitecture AudioPlayerClient)

add_module_library(PuzzleFeatureView src/Features/Puzzle/PuzzleFeatureView.cppm)
target_link_libraries(PuzzleFeatureView PUBLIC PuzzleFeature raylib)

add_module_library(AppFeature src/Features/App/AppFeature.cppm)
target_link_libraries(AppFeature PUBLIC PuzzleFeature)

add_module_library(AppFeatureView src/Features/App/AppFeatureView.cppm)
target_link_libraries(AppFeatureView PUBLIC AppFeature PuzzleFeatureView raylib)

# --- Executable ---------------------------------------------------------------

add_executable(${PROJECT_NAME} main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_26)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(${PROJECT_NAME} PRIVATE
  AppFeatureView AppFeature PuzzleFeature ComposableArchitecture
  AudioPlayerClient AudioPlayerClientLive
)

# --- Tests --------------------------------------------------------------------

enable_testing()

add_executable(PuzzleFeatureTests tests/PuzzleFeatureTests.cpp)
target_compile_features(PuzzleFeatureTests PRIVATE cxx_std_26)
set_target_properties(PuzzleFeatureTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(PuzzleFeatureTests PRIVATE PuzzleFeature ComposableArchitecture)

add_test(NAME PuzzleFeatureTests COMMAND PuzzleFeatureTests)
