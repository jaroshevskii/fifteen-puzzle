cmake_minimum_required(VERSION 4.2.3)
project(FifteenPuzzle LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

FetchContent_MakeAvailable(openal-soft)

add_library(
  PuzzleFeature
  src/Features/Puzzle/PuzzleFeature.cpp
  src/Features/Puzzle/PuzzleFeatureAudio.cpp
  src/Features/Puzzle/PuzzleFeatureView.cpp
)

target_sources(
  PuzzleFeature
  PRIVATE
    src/Features/Puzzle/PuzzleFeature.hpp
    src/Features/Puzzle/PuzzleFeatureAudio.hpp
    src/Features/Puzzle/PuzzleFeatureView.hpp
)

target_include_directories(PuzzleFeature PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(PuzzleFeature PUBLIC raylib OpenAL)

add_library(
  AppFeature
  src/Features/App/AppFeature.cpp
  src/Features/App/AppFeatureView.cpp
)

target_sources(
  AppFeature
  PRIVATE
    src/Features/App/AppFeature.hpp
    src/Features/App/AppFeatureView.hpp
)

target_include_directories(AppFeature PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(AppFeature PUBLIC PuzzleFeature)

add_executable(${PROJECT_NAME} main.cpp)
target_sources(${PROJECT_NAME} PRIVATE src/Core/Store.hpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE AppFeature)
