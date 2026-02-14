cmake_minimum_required(VERSION 3.20)
project(RaylibFetch LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FetchContent для Raylib (скачає з GitHub, збудує)
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5  # Останній реліз
    GIT_SHALLOW TRUE  # Скачає тільки останню версію для швидкості
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)  # Не будуємо приклади
FetchContent_MakeAvailable(raylib)

add_executable(raylib_app main.cpp)
target_link_libraries(raylib_app PRIVATE raylib)

if(APPLE)
    target_link_libraries(raylib_app PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework OpenGL"
    )
endif()