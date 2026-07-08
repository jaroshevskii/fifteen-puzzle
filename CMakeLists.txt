cmake_minimum_required(VERSION 4.0)

# Enable experimental `import std;` support (gate UUID is CMake-version specific).
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

project(FifteenPuzzle LANGUAGES C CXX)

# Default to an optimized build when none is requested. Besides being the sane
# default, it is REQUIRED here: at bare -O0 this Clang + `import std` toolchain
# miscompiles some module code into a corrupted call (a garbage jump that
# crashes at run time but vanishes under any optimization or sanitizer). Any of
# -O1/-Og/-O2 produces correct codegen. Release builds set their own type.
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _is_multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
  message(STATUS "No CMAKE_BUILD_TYPE set; defaulting to RelWithDebInfo")
endif()

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

# On macOS the system libsqlite3 lives in the SDK, so find_package reports
# "<sdk>/usr/include" as its include dir. `Sqlite` links this target PUBLIC, so
# that bare `-isystem <sdk>/usr/include` propagates transitively to every
# consumer (down to the FifteenServer executable), where it puts the SDK ahead
# of Clang's own resource headers and breaks the SDK's `#include_next <stddef.h>`
# chain (e.g. `<signal.h>` fails with "unknown type name 'ptrdiff_t'"). It is
# the same hazard handled for CURL below; sqlite3.h stays reachable via the
# implicit sysroot, so strip the bare "/usr/include" entry from the target.
if(APPLE AND TARGET SQLite::SQLite3)
  get_target_property(_sqlite_includes SQLite::SQLite3 INTERFACE_INCLUDE_DIRECTORIES)
  if(_sqlite_includes)
    list(FILTER _sqlite_includes EXCLUDE REGEX "/usr/include/?$")
    set_target_properties(SQLite::SQLite3 PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${_sqlite_includes}")
  endif()
endif()

# --- CURL ---------------------------------------------------------------------
# HTTP transport for the network client. Always resolved via find_package (even
# for static release builds): system libcurl ships on every CI runner and via
# vcpkg, whereas a from-source static curl would also need a TLS backend built
# and configured — out of scope here. The network client links it dynamically.
find_package(CURL REQUIRED)
message(STATUS "CURL: using ${CURL_VERSION_STRING}")

# On macOS the system libcurl lives in the SDK, so find_package reports
# "<sdk>/usr/include" as its include dir. Propagating that as an explicit
# `-isystem <sdk>/usr/include` puts the SDK ahead of Clang's own resource
# headers in the search order, breaking the SDK's `#include_next <stddef.h>`
# chain (Homebrew Clang then fails with "unknown type name '__int64_t'" while
# parsing <curl/curl.h>). Those headers are already reachable via the implicit
# sysroot, so strip the bare "/usr/include" entry from the imported target.
if(APPLE AND TARGET CURL::libcurl)
  get_target_property(_curl_includes CURL::libcurl INTERFACE_INCLUDE_DIRECTORIES)
  if(_curl_includes)
    list(FILTER _curl_includes EXCLUDE REGEX "/usr/include/?$")
    set_target_properties(CURL::libcurl PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${_curl_includes}")
  endif()
endif()

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
  Sources/Dependencies/Dependencies-Core.cppm
  Sources/Dependencies/Dependencies-DateGenerator.cppm
  Sources/Dependencies/Dependencies-RandomNumberGenerator.cppm
  Sources/Dependencies/Dependencies.cppm
)

add_module_library(ComposableArchitecture
  Sources/ComposableArchitecture/ComposableArchitecture-CasePath.cppm
  Sources/ComposableArchitecture/ComposableArchitecture-Store.cppm
  Sources/ComposableArchitecture/ComposableArchitecture-Feature.cppm
  Sources/ComposableArchitecture/ComposableArchitecture-Runtime.cppm
  Sources/ComposableArchitecture/ComposableArchitecture-Scope.cppm
  Sources/ComposableArchitecture/ComposableArchitecture-Navigation.cppm
  Sources/ComposableArchitecture/ComposableArchitecture-TestStore.cppm
  Sources/ComposableArchitecture/ComposableArchitecture.cppm
)
target_link_libraries(ComposableArchitecture PUBLIC Dependencies)

# Plain value types shared by the network + database clients.
add_module_library(SharedModels Sources/SharedModels/SharedModels.cppm)

# --- Shared client/server modules ----------------------------------------------
# The isowords pattern: game rules, routes and the multiplayer protocol are each
# defined once and imported by both the app and the server, so the two sides can
# never drift apart.

# Pure sliding-puzzle rules (solved layout, adjacency, scramble, slide). The
# client plays with them; the server re-plays multiplayer moves with them.
add_module_library(PuzzleCore Sources/PuzzleCore/PuzzleCore.cppm)
target_link_libraries(PuzzleCore PUBLIC Dependencies)

# The HTTP API surface: ApiClientLive prints routes into requests, the server
# matches requests back into routes. JSON codecs are confined to the impl unit.
add_module_library(ServerRouter Sources/ServerRouter/ServerRouter.cppm)
target_sources(ServerRouter PRIVATE Sources/ServerRouter/ServerRouter.cpp)
target_link_libraries(ServerRouter PUBLIC SharedModels PRIVATE nlohmann_json::nlohmann_json)

# The realtime multiplayer wire protocol (messages + line-JSON codec).
add_module_library(MultiplayerCore Sources/MultiplayerCore/MultiplayerCore.cppm)
target_sources(MultiplayerCore PRIVATE Sources/MultiplayerCore/MultiplayerCore.cpp)
target_link_libraries(MultiplayerCore PRIVATE nlohmann_json::nlohmann_json)

# Minimal blocking TCP wrapper (POSIX/Winsock confined to the impl unit); the
# transport under HttpServer, GameServer and MultiplayerClientLive.
add_module_library(TcpSocket Sources/TcpSocket/TcpSocket.cppm)
target_sources(TcpSocket PRIVATE Sources/TcpSocket/TcpSocket.cpp)
if(WIN32)
  target_link_libraries(TcpSocket PRIVATE ws2_32)
endif()

# --- Sharing (persisted shared state) -----------------------------------------

add_module_library(Sharing
  Sources/Sharing/Sharing-Strategy.cppm
  Sources/Sharing/Sharing-Shared.cppm
  Sources/Sharing/Sharing.cppm
)

add_module_library(AppSettings Sources/Sharing/AppSettings.cppm)

# The JSON serializer is confined to the implementation unit (.cpp), so nlohmann
# is linked PRIVATE and never leaks into AppSettingsLive's reachable interface.
add_module_library(AppSettingsLive Sources/Sharing/AppSettingsLive.cppm)
target_sources(AppSettingsLive PRIVATE Sources/Sharing/AppSettingsLive.cpp)
target_link_libraries(AppSettingsLive PUBLIC AppSettings Sharing PRIVATE nlohmann_json::nlohmann_json)

# Saved in-progress game (for resume / continue).
add_module_library(SavedGame Sources/Sharing/SavedGame.cppm)

add_module_library(SavedGameLive Sources/Sharing/SavedGameLive.cppm)
target_sources(SavedGameLive PRIVATE Sources/Sharing/SavedGameLive.cpp)
target_link_libraries(SavedGameLive PUBLIC SavedGame Sharing PRIVATE nlohmann_json::nlohmann_json)

# --- SQLite database client ---------------------------------------------------

# Low-level SQLite wrapper. Its methods are header-inline, so the sqlite3 symbols
# are needed wherever it is used — link the target PUBLIC so consumers pick it up.
add_module_library(Sqlite Sources/Sqlite/Sqlite.cppm)
target_link_libraries(Sqlite PUBLIC ${SQLITE_TARGET})

add_module_library(DatabaseClient Sources/DatabaseClient/DatabaseClient.cppm)
target_link_libraries(DatabaseClient PUBLIC Dependencies SharedModels)

add_module_library(DatabaseClientLive Sources/DatabaseClient/DatabaseClientLive.cppm)
target_link_libraries(DatabaseClientLive PUBLIC DatabaseClient Sqlite)

# --- Clients ------------------------------------------------------------------

add_module_library(AudioPlayerClient Sources/AudioPlayerClient/AudioPlayerClient.cppm)
target_link_libraries(AudioPlayerClient PUBLIC Dependencies)

add_module_library(AudioPlayerClientLive Sources/AudioPlayerClient/AudioPlayerClientLive.cppm)
target_link_libraries(AudioPlayerClientLive PUBLIC AudioPlayerClient PRIVATE ${OPENAL_TARGET})
if(FIFTEEN_STATIC_DEPS)
  target_compile_definitions(AudioPlayerClientLive PRIVATE AL_LIBTYPE_STATIC)  # link openal-soft statically
endif()

add_module_library(SolverClient Sources/SolverClient/SolverClient.cppm)
target_link_libraries(SolverClient PUBLIC Dependencies)

add_module_library(SolverClientLive Sources/SolverClient/SolverClientLive.cppm)
target_link_libraries(SolverClientLive PUBLIC SolverClient)

# Network client. curl is confined to the implementation unit (.cpp), so it is
# linked PRIVATE and never leaks into ApiClientLive's interface. Requests and
# response bodies come from the shared ServerRouter, never hand-written.
add_module_library(ApiClient Sources/ApiClient/ApiClient.cppm)
target_link_libraries(ApiClient PUBLIC Dependencies SharedModels)

add_module_library(ApiClientLive Sources/ApiClient/ApiClientLive.cppm)
target_sources(ApiClientLive PRIVATE Sources/ApiClient/ApiClientLive.cpp)
target_link_libraries(ApiClientLive PUBLIC ApiClient PRIVATE ServerRouter CURL::libcurl)

# Realtime multiplayer connection: interface + live TCP implementation.
add_module_library(MultiplayerClient Sources/MultiplayerClient/MultiplayerClient.cppm)
target_link_libraries(MultiplayerClient PUBLIC Dependencies MultiplayerCore)

add_module_library(MultiplayerClientLive Sources/MultiplayerClient/MultiplayerClientLive.cppm)
target_link_libraries(MultiplayerClientLive PUBLIC MultiplayerClient MultiplayerCore TcpSocket)

# --- Features -----------------------------------------------------------------

add_module_library(PuzzleFeature Sources/Features/Puzzle/PuzzleFeature.cppm)
# Implementation unit: editing reducer logic here recompiles only this object,
# leaving the module's BMI (and therefore its importers) untouched.
target_sources(PuzzleFeature PRIVATE Sources/Features/Puzzle/PuzzleFeature.cpp)
target_link_libraries(PuzzleFeature PUBLIC ComposableArchitecture AudioPlayerClient PuzzleCore SolverClient Sharing AppSettings)

add_module_library(PuzzleFeatureView Sources/Features/Puzzle/PuzzleFeatureView.cppm)
target_link_libraries(PuzzleFeatureView PUBLIC PuzzleFeature raylib)

add_module_library(SettingsFeature Sources/Features/Settings/SettingsFeature.cppm)
target_sources(SettingsFeature PRIVATE Sources/Features/Settings/SettingsFeature.cpp)
target_link_libraries(SettingsFeature PUBLIC ComposableArchitecture Sharing AppSettings)

add_module_library(SettingsFeatureView Sources/Features/Settings/SettingsFeatureView.cppm)
target_link_libraries(SettingsFeatureView PUBLIC SettingsFeature AppSettings raylib)

# Shared raylib UI kit for the menu-style screens (no feature deps).
add_module_library(MenuView Sources/Features/App/MenuView.cppm)
target_link_libraries(MenuView PUBLIC raylib)

add_module_library(LeaderboardFeature Sources/Features/Leaderboard/LeaderboardFeature.cppm)
target_sources(LeaderboardFeature PRIVATE Sources/Features/Leaderboard/LeaderboardFeature.cpp)
target_link_libraries(LeaderboardFeature PUBLIC ComposableArchitecture ApiClient DatabaseClient SharedModels)

add_module_library(LeaderboardFeatureView Sources/Features/Leaderboard/LeaderboardFeatureView.cppm)
target_link_libraries(LeaderboardFeatureView PUBLIC LeaderboardFeature raylib)

add_module_library(MultiplayerFeature Sources/Features/Multiplayer/MultiplayerFeature.cppm)
target_sources(MultiplayerFeature PRIVATE Sources/Features/Multiplayer/MultiplayerFeature.cpp)
target_link_libraries(MultiplayerFeature PUBLIC ComposableArchitecture MultiplayerClient MultiplayerCore PuzzleCore)

add_module_library(MultiplayerFeatureView Sources/Features/Multiplayer/MultiplayerFeatureView.cppm)
target_link_libraries(MultiplayerFeatureView PUBLIC MultiplayerFeature PuzzleCore raylib)

add_module_library(AppFeature Sources/Features/App/AppFeature.cppm)
target_sources(AppFeature PRIVATE Sources/Features/App/AppFeature.cpp)
target_link_libraries(AppFeature
  PUBLIC PuzzleFeature LeaderboardFeature MultiplayerFeature SettingsFeature SavedGame SharedModels Sharing AppSettings
  PRIVATE DatabaseClient ApiClient)

add_module_library(AppFeatureView Sources/Features/App/AppFeatureView.cppm)
target_link_libraries(AppFeatureView PUBLIC AppFeature PuzzleFeatureView LeaderboardFeatureView MultiplayerFeatureView SettingsFeatureView MenuView raylib)

# --- Server -------------------------------------------------------------------
# The server side of the monorepo (isowords' Sources/server): the HTTP API and
# the realtime multiplayer referee, sharing SharedModels, ServerRouter,
# PuzzleCore, MultiplayerCore and the SQLite DatabaseClient with the app.

# Request-in/response-out server logic — pure, so integration tests can back
# the client's ApiClient with this exact middleware in-process.
add_module_library(SiteMiddleware Sources/SiteMiddleware/SiteMiddleware.cppm)
target_link_libraries(SiteMiddleware PUBLIC DatabaseClient PuzzleCore ServerRouter SharedModels)

add_module_library(HttpServer Sources/HttpServer/HttpServer.cppm)
target_sources(HttpServer PRIVATE Sources/HttpServer/HttpServer.cpp)
target_link_libraries(HttpServer PUBLIC ServerRouter TcpSocket)

# Matchmaking + referee. The Engine is pure logic (tests drive it directly);
# the socket shell lives in the impl unit.
add_module_library(GameServer Sources/GameServer/GameServer.cppm)
target_sources(GameServer PRIVATE Sources/GameServer/GameServer.cpp)
target_link_libraries(GameServer PUBLIC Dependencies MultiplayerCore PuzzleCore SharedModels PRIVATE TcpSocket)

add_module_library(ServerBootstrap Sources/ServerBootstrap/ServerBootstrap.cppm)
target_link_libraries(ServerBootstrap PUBLIC DatabaseClient DatabaseClientLive SiteMiddleware)

add_executable(FifteenServer Sources/server/main.cpp)
set_target_properties(FifteenServer PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(FifteenServer PRIVATE
  Dependencies GameServer HttpServer ServerBootstrap ServerRouter SiteMiddleware
  DatabaseClient DatabaseClientLive MultiplayerCore PuzzleCore SharedModels TcpSocket
)

# --- Executable ---------------------------------------------------------------

add_executable(${PROJECT_NAME} App/main.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(${PROJECT_NAME} PRIVATE
  AppFeatureView AppFeature PuzzleFeature LeaderboardFeature MultiplayerFeature SettingsFeature
  ComposableArchitecture PuzzleCore
  AudioPlayerClient AudioPlayerClientLive
  SolverClient SolverClientLive
  ApiClient ApiClientLive
  MultiplayerClient MultiplayerClientLive MultiplayerCore
  DatabaseClient DatabaseClientLive
  Sharing AppSettings AppSettingsLive
  SavedGame SavedGameLive
  MenuView SettingsFeatureView
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

add_executable(PuzzleFeatureTests EXCLUDE_FROM_ALL Tests/PuzzleFeatureTests.cpp)
set_target_properties(PuzzleFeatureTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(PuzzleFeatureTests PRIVATE PuzzleFeature ComposableArchitecture)

add_test(NAME PuzzleFeatureTests COMMAND PuzzleFeatureTests)

add_executable(SolverClientTests EXCLUDE_FROM_ALL Tests/SolverClientTests.cpp)
set_target_properties(SolverClientTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(SolverClientTests PRIVATE SolverClient SolverClientLive)

add_test(NAME SolverClientTests COMMAND SolverClientTests)

add_executable(SharingTests EXCLUDE_FROM_ALL Tests/SharingTests.cpp)
set_target_properties(SharingTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(SharingTests PRIVATE Sharing AppSettings AppSettingsLive)

add_test(NAME SharingTests COMMAND SharingTests)

add_executable(DatabaseClientTests EXCLUDE_FROM_ALL Tests/DatabaseClientTests.cpp)
set_target_properties(DatabaseClientTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(DatabaseClientTests PRIVATE DatabaseClient DatabaseClientLive)

add_test(NAME DatabaseClientTests COMMAND DatabaseClientTests)

add_executable(LeaderboardFeatureTests EXCLUDE_FROM_ALL Tests/LeaderboardFeatureTests.cpp)
set_target_properties(LeaderboardFeatureTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(LeaderboardFeatureTests PRIVATE LeaderboardFeature ComposableArchitecture ApiClient DatabaseClient SharedModels)

add_test(NAME LeaderboardFeatureTests COMMAND LeaderboardFeatureTests)

add_executable(NavigationTests EXCLUDE_FROM_ALL Tests/NavigationTests.cpp)
set_target_properties(NavigationTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(NavigationTests PRIVATE ComposableArchitecture)

add_test(NAME NavigationTests COMMAND NavigationTests)

add_executable(SavedGameTests EXCLUDE_FROM_ALL Tests/SavedGameTests.cpp)
set_target_properties(SavedGameTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(SavedGameTests PRIVATE SavedGame SavedGameLive Sharing)

add_test(NAME SavedGameTests COMMAND SavedGameTests)

add_executable(SettingsFeatureTests EXCLUDE_FROM_ALL Tests/SettingsFeatureTests.cpp)
set_target_properties(SettingsFeatureTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(SettingsFeatureTests PRIVATE SettingsFeature ComposableArchitecture Sharing AppSettings)

add_test(NAME SettingsFeatureTests COMMAND SettingsFeatureTests)

add_executable(AppFeatureTests EXCLUDE_FROM_ALL Tests/AppFeatureTests.cpp)
set_target_properties(AppFeatureTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(AppFeatureTests PRIVATE AppFeature PuzzleFeature SavedGame ComposableArchitecture Sharing AppSettings)

add_test(NAME AppFeatureTests COMMAND AppFeatureTests)

add_executable(ServerRouterTests EXCLUDE_FROM_ALL Tests/ServerRouterTests.cpp)
set_target_properties(ServerRouterTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(ServerRouterTests PRIVATE ServerRouter SharedModels)

add_test(NAME ServerRouterTests COMMAND ServerRouterTests)

# The isowords integration-test pattern: LeaderboardFeature drives an ApiClient
# whose closures call the real SiteMiddleware against an in-memory database.
add_executable(SiteMiddlewareTests EXCLUDE_FROM_ALL Tests/SiteMiddlewareTests.cpp)
set_target_properties(SiteMiddlewareTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(SiteMiddlewareTests PRIVATE
  SiteMiddleware ServerRouter DatabaseClient DatabaseClientLive SharedModels
  ApiClient LeaderboardFeature ComposableArchitecture)

add_test(NAME SiteMiddlewareTests COMMAND SiteMiddlewareTests)

add_executable(GameServerTests EXCLUDE_FROM_ALL Tests/GameServerTests.cpp)
set_target_properties(GameServerTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(GameServerTests PRIVATE GameServer MultiplayerCore PuzzleCore Dependencies SharedModels)

add_test(NAME GameServerTests COMMAND GameServerTests)

add_executable(MultiplayerFeatureTests EXCLUDE_FROM_ALL Tests/MultiplayerFeatureTests.cpp)
set_target_properties(MultiplayerFeatureTests PROPERTIES CXX_MODULE_STD ON)
target_link_libraries(MultiplayerFeatureTests PRIVATE
  MultiplayerFeature MultiplayerClient MultiplayerCore PuzzleCore ComposableArchitecture)

add_test(NAME MultiplayerFeatureTests COMMAND MultiplayerFeatureTests)
