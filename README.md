# 15 Puzzle

[![macOS](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/macos.yml/badge.svg)](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/macos.yml)
[![Linux](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/linux.yml/badge.svg)](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/linux.yml)
[![Windows](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/windows.yml/badge.svg)](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/windows.yml)
[![Format](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/format.yml/badge.svg)](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/format.yml)
[![Release](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/release.yml/badge.svg)](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/release.yml)

A implementation of the classic 15 Puzzle built with C++ and raylib.  
The project focuses on clarity, deterministic layout, and straightforward rendering logic.

<img width="488" height="520" alt="image" src="https://github.com/user-attachments/assets/8b4296a7-e054-4c21-8e27-a9fba1c9a2e4" />

# Demo

https://github.com/user-attachments/assets/014517ab-ebbd-4619-8409-10c19ffa2edd

## Overview

This project implements:

- 4×4 sliding puzzle grid
- Mouse and keyboard interaction
- Solvable shuffle logic
- Win state detection
- Elapsed/victory timer with optional tick sound

## Architecture

The app is built on a C++ port of [the Composable Architecture (TCA) **2.0**][tca]
and the [Dependencies][deps] library, following the modular, feature-per-library
structure of [isowords][isowords]. The 2.0 concepts and naming are carried over
faithfully to C++:

| TCA 2.0 concept | C++ port |
| --- | --- |
| Feature definition | `ComposableArchitecture::Feature<State, Action>` (a `body()`) |
| Synchronous state mutation | `Update { state, action, store in … }` (no effect returns) |
| Implicit feature store | `Store<State, Action>` — `addTask`, `send`, `modify`, `snapshot` |
| Async work on a background task | `store.addTask([](store, stop){ … }, cancelID)` |
| Lifecycle hooks | `.onMount(...)`, `.onDismount(...)` |
| Composition | `Scope(&State::child, casePath, Child::body())` |
| Controlled dependencies | `Dependency<Key>`, `DependencyValues`, `withDependencies`, `prepareDependencies` |
| Exhaustive feature testing | `TestStore<State, Action>` |

State is mutated only on the store's thread: `Update` blocks mutate synchronously,
and async work enqueued with `store.addTask` reports back via `store.send` /
`store.modify` (serialized on the main thread, so feature code has no locks or
data races). There are no `Effect` return values — the 2.0 redesign. The first
shuffle and timer start happen in **`onMount`**, not an `AppLaunched` action.
Time (`\.date`) and randomness (`\.withRandomNumberGenerator`) flow through
controlled dependencies, so the puzzle logic is fully deterministic under test.
Audio is an injected `AudioPlayerClient` (interface + live OpenAL backend), wired
in at launch via `prepareDependencies`.

*Not ported (2.0 features that don't fit a small raylib game): SwiftUI bindings,
preferences, triggers, delegate closures, `spawn`, `@FeatureLocal`, events, and
full `@MainActor` actor isolation (we use a single-threaded loop with a
main-thread-guarded store instead).*

The code targets **C++26** and is organized as **C++20 named modules** (one
module per concept, with partitions for the core libraries). The standard
library is consumed via **`import std;`**, and the C audio/graphics libraries are
`#include`d only in each module's global module fragment.

### Asynchronous auto-solver

Pressing **H** auto-solves the board and animates it to solved, at any size. The
game generates every board by legal slides from the solved state and records
that move history, so a solution is just the **inverse of the history**
(`SolverClient`). That is `O(moves)` and independent of board size — a 13×13
solves as instantly as a 4×4, with no search. It showcases what the
effect/dependency architecture makes tractable:

- An `Update` calls **`store.addTask`**, which the runtime runs on a
  `std::jthread`. The planner reports its result back via `store.send`; state is
  still mutated only on the main thread, so there are no locks or data races in
  feature code.
- It is **cancellable**: `store.cancel(id)` requests the task's `std::stop_token`,
  and any interaction (tap, shuffle, restart, resize, or pressing H again)
  cancels an in-flight solve instantly.
- It stays **fully testable**: tests inject a stub `SolverClient` and a pinned
  clock, and `TestStore` runs the task inline — deterministic, no threads
  (see `tests/`). `SolverClientTests` checks the planner across sizes 4×4–13×13.

`std::expected` carries the result/cancellation, and `std::stop_token` drives
cooperative cancellation — all under `-std=c++26`.

[tca]: https://www.pointfree.co/blog/posts/206-beta-preview-composablearchitecture-2-0
[deps]: https://github.com/pointfreeco/swift-dependencies
[isowords]: https://github.com/pointfreeco/isowords

### Persistence, networking & sharing

Mirroring the "real app" surface of isowords, three dependencies extend the game
beyond the board:

- **Sharing** (`Shared<T>`) — a port of Point-Free's [Sharing][sharing] library.
  A `Shared<T>` is a plain value (so it lives in feature `State` and stays
  `operator==`-comparable) backed by a pluggable `PersistenceStrategy`:
  `inMemory` for tests, or a JSON `fileStorage` that atomically writes a file.
  App settings (sound, last board size, player name) persist across launches;
  saves run off the main thread via `store.addTask`.
- **DatabaseClient** (SQLite) — a port of isowords' `LocalDatabaseClient`. A thin
  `Sqlite` wrapper + a `DatabaseClient` dependency record every completed game and
  answer best-scores / stats queries (`PRAGMA user_version` migrations).
- **ApiClient** (network) — a port of isowords' `ApiClient`, over **libcurl** +
  **nlohmann/json**. It submits scores and fetches a remote leaderboard from a
  configurable endpoint, honoring `std::stop_token` for cancellation and
  returning `std::expected` so it degrades gracefully when offline.

`LeaderboardFeature` ties them together: on appear it loads the local (database)
and remote (API) leaderboards concurrently and merges them; if the network is
unreachable it shows the local scores with an "offline" hint. Winning a game is
detected in `AppFeature` (keeping `PuzzleFeature` unaware of the leaderboard) and
routed as a `ScoreSubmitted`, which persists locally and pushes to the server
best-effort.

Heavy C++ third-party headers (nlohmann/json) are confined to module
*implementation units* (`.cpp`), never a reachable interface, so they don't clash
with `import std;` — the same global-module-fragment discipline used for the C
audio/SQLite/curl headers.

**Configuring the server.** No backend is bundled. Point the client at a real
deployment via the `FIFTEEN_API_BASE_URL` environment variable (REST contract:
`GET {base}/leaderboard?size=N`, `POST {base}/scores`). With it unset or
unreachable, the leaderboard simply shows local scores.

A per-user data directory (`~/Library/Application Support/FifteenPuzzle` on
macOS, `$XDG_DATA_HOME` on Linux, `%APPDATA%` on Windows) holds `settings.json`
and `games.sqlite3`.

[sharing]: https://github.com/pointfreeco/swift-sharing

### Modules (`src/`)

- `ComposableArchitecture` — core module (`:CasePath`, `:Effect`, `:Reducer`, `:Store`, `:TestStore` partitions)
- `Dependencies` — dependency container module (`:Core`, `:DateGenerator`, `:RandomNumberGenerator` partitions)
- `SharedModels` — plain value types shared by the network and database clients (`LeaderboardEntry`, `ScoreSubmission`, `Stats`)
- `Sharing` / `AppSettings` / `AppSettingsLive` — persisted shared state: a `Shared<T>` value with `inMemory` / JSON `fileStorage` strategies, used for app settings (sound, last board size, player name)
- `Sqlite` / `DatabaseClient` / `DatabaseClientLive` — SQLite wrapper and the local leaderboard/stats database dependency
- `ApiClient` / `ApiClientLive` — remote leaderboard dependency interface and its live libcurl + JSON implementation
- `AudioPlayerClient` / `AudioPlayerClientLive` — audio dependency interface module and its live OpenAL implementation
- `SolverClient` / `SolverClientLive` — auto-solve planner dependency and its live (history-reversing) implementation
- `PuzzleFeature` / `PuzzleFeatureView` — puzzle reducer module and its raylib view module
- `LeaderboardFeature` / `LeaderboardFeatureView` — leaderboard reducer (merges local + remote) and its raylib overlay view
- `AppFeature` / `AppFeatureView` — composition-root reducer module scoping the puzzle + leaderboard, and its view

## Controls

- Move tile — Left mouse click or arrow keys
- Resize board — keys `0` (4×4) … `9` (13×13)
- Shuffle — S
- Restart — R
- Toggle tick sound — M
- Auto-solve (toggle) — H
- Near-win shortcut — double-press W
- Leaderboard overlay (toggle) — L
- Restart after victory — Mouse click or R

The board is resizable from 4×4 up to 13×13. The window grows with the board up
to a cap (3× the base 4×4 board); beyond that, tiles shrink to fit.

## Game Logic

- The empty tile is represented by `""`
- Movement is allowed only for tiles adjacent to the empty square
- Shuffles are validated for solvability
- Victory condition: tiles 1–15 are ordered and the empty tile is last

## Building

The project uses **C++ modules** and **`import std;`**, which needs a recent
toolchain and the **Ninja** generator. C++ standard: **C++26** on Clang/GCC,
**C++23** on MSVC (CMake 4.2 doesn't support the C++26 dialect or `import std`
at C++26 for MSVC — the code is C++23-compatible, so nothing is lost). CMake is
pinned to **4.2.x** because `import std;` is gated behind
`CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`, whose UUID is CMake-version specific.

A matching configure preset is provided per platform (`macos`, `linux`,
`windows`); `default` aliases `macos`. CI runs each platform in its own
workflow — see the badges above.

### macOS

Apple Clang supports neither C++ modules nor `import std;`, so use Homebrew LLVM:

```sh
brew install llvm ninja raylib openal-soft
cmake --preset macos
cmake --build --preset macos
```

### Linux

Upstream LLVM + libc++ (Clang 21 here, e.g. via <https://apt.llvm.org>):

```sh
sudo ./llvm.sh 21
sudo apt-get install -y clang-21 lld-21 libc++-21-dev libc++abi-21-dev \
  libopenal-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev
sudo apt-get install -y libraylib-dev || true   # optional; built from source if absent

CC=clang-21 CXX=clang++-21 cmake --preset linux \
  -DCMAKE_CXX_STDLIB_MODULES_JSON="$(clang++-21 -stdlib=libc++ -print-file-name=libc++.modules.json)"
cmake --build --preset linux
```

### Windows

MSVC (Visual Studio 2022+) ships its own `std` module. From a *Developer Command
Prompt* (so `cl` and Ninja are on `PATH`). raylib + openal-soft come from
**vcpkg** (no system package exists); pass its toolchain so `find_package` finds
them — otherwise CMake falls back to building them from source:

```bat
vcpkg install raylib openal-soft sqlite3 curl nlohmann-json --triplet x64-windows
cmake --preset windows -DCMAKE_TOOLCHAIN_FILE=%VCPKG_INSTALLATION_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build --preset windows
```

### Dependencies (fast vs. self-contained)

- **Default (dev/CI):** raylib, openal-soft, SQLite3, libcurl, and nlohmann/json
  are resolved **prebuilt** via `find_package` — from brew (macOS), apt (Linux),
  or vcpkg (Windows). On macOS/Linux, SQLite3 and libcurl ship with the SDK. This
  skips compiling ~170 third-party translation units and links them dynamically —
  a from-scratch build drops from **~65 s to ~2.5 s**. If a package is missing,
  CMake falls back to building it from pinned sources (FetchContent), so a fresh
  checkout still works anywhere. A `ccache` install is picked up automatically to
  cache those source builds.
- **Release (`-DFIFTEEN_STATIC_DEPS=ON`):** raylib + openal-soft are built from
  source and linked **statically**; SQLite is vendored from the amalgamation and
  nlohmann/json is header-only. **libcurl is the exception** — it is always linked
  dynamically (a from-source static curl + TLS backend is out of scope), so a
  released Windows binary needs the curl DLL alongside it. This is what the
  release workflow uses.

## Testing

Tests are `EXCLUDE_FROM_ALL`, so a normal build never recompiles them. Build and
run them on demand:

```sh
cmake --workflow --preset test          # macOS: configure + build tests + run
# or, against an existing build dir (any platform):
cmake --build --preset tests && ctest --preset macos   # tests / tests-linux / tests-windows
```

## Code quality

- **clang-format** — style is `.clang-format` (LLVM, the clang-format default).
  The [Format workflow](.github/workflows/format.yml) fails CI on any drift;
  format locally with `clang-format -i $(git ls-files '*.cpp' '*.cppm')`.
- **clang-tidy** — `.clang-tidy` ships a curated check set. It runs against a
  compile database (`cmake … -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, build, then
  `clang-tidy -p build …`). It is **not** enforced in CI — it parses the modules
  fine, but the findings need curation and it can't run as a per-TU build launcher
  without breaking module compilation.
- **CI hardening** — each workflow uses least-privilege `permissions:
  contents: read` and `concurrency` (superseded runs are cancelled). Dependabot
  keeps the Actions up to date.

## Releases

Pushing a published GitHub Release triggers
[`release.yml`](.github/workflows/release.yml), which builds an optimized,
statically-linked binary (`FIFTEEN_STATIC_DEPS=ON`, `Release`) for macOS, Linux,
and Windows and attaches them to the release. Debug/CI builds instead link the
dependencies dynamically (faster to link).

Assets are packaged as archives (`.tar.gz` for macOS/Linux, `.zip` for Windows)
so the executable bit survives download — a bare binary served over HTTP loses
it and macOS would treat it as a non-runnable "Document". To run:

```sh
tar -xzf FifteenPuzzle-macos-arm64.tar.gz
xattr -dr com.apple.quarantine FifteenPuzzle-macos-arm64   # macOS: unsigned binary
./FifteenPuzzle-macos-arm64
```

*(Binaries still depend on the toolchain's C++ runtime — libc++ on Clang, the
MSVC runtime on Windows — and are unsigned.)*

### Working in Xcode

`FifteenPuzzle.xcodeproj` is an **External Build System** project: open it and
Build (⌘B) / Run (⌘R) delegate to the CMake + Ninja build via
`scripts/xcode-build.sh`, using Xcode purely as an editor and runner.

This indirection is necessary — Xcode's CMake generator does not support C++20
modules, and Apple Clang supports neither modules nor `import std;`, so Xcode
cannot build this code natively. For the same reason, Xcode's indexer will not
fully understand `import std;` / `import <Module>;`; an editor backed by
`compile_commands.json` (e.g. VS Code or CLion) gives better navigation.

## Possible Improvements

- Move counter
- Animation transitions
- Adaptive layout

## License

This project is licensed under the MIT License. See the [LICENSE](./LICENSE.md) file for details.
