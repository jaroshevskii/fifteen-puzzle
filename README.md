# 15 Puzzle

[![macOS](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/macos.yml/badge.svg)](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/macos.yml)
[![Linux](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/linux.yml/badge.svg)](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/linux.yml)
[![Windows](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/windows.yml/badge.svg)](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/windows.yml)
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

The app is built on a C++ port of [the Composable Architecture (TCA) 2.0][tca] and
the [Dependencies][deps] library, following the modular, feature-per-library
structure of [isowords][isowords]. The TCA concepts and naming are carried over
faithfully to C++:

| Concept | Type |
| --- | --- |
| Unidirectional state container | `ComposableArchitecture::Store<State, Action>` |
| Side effects as values | `Effect<Action>` (`none` / `send` / `run` / `merge`) |
| Reducer building blocks | `Reduce`, `Scope`, `combine` |
| Follow-up action callback | `Send<Action>` |
| Controlled dependencies | `Dependency<Key>`, `DependencyValues`, `withDependencies`, `prepareDependencies` |
| Exhaustive feature testing | `TestStore<State, Action>` |

State is mutated only inside reducers, which stay pure by returning effects
instead of performing work inline. Time (`\.date`) and randomness
(`\.withRandomNumberGenerator`) flow through controlled dependencies, so the
puzzle logic is fully deterministic under test. Audio is an injected
`AudioPlayerClient` (interface + live OpenAL backend), wired in at launch via
`prepareDependencies`.

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

- The reducer returns an **async effect** (`Effect::task`) that the `Store` runs
  on a `std::jthread`. The planner streams its result back as an action through a
  thread-safe `Send`; state is still mutated only on the main thread, so there
  are no locks or data races in feature code.
- It is **cancellable**: `Effect::cancel` requests the worker's `std::stop_token`,
  and any interaction (tap, shuffle, restart, resize, or pressing H again)
  cancels an in-flight solve instantly.
- It stays **fully testable**: tests inject a stub `SolverClient` and a pinned
  clock, and `TestStore` runs the async effect inline — deterministic, no threads
  (see `tests/`). `SolverClientTests` checks the planner across sizes 4×4–13×13.

`std::expected` carries the result/cancellation, and `std::stop_token` drives
cooperative cancellation — all under `-std=c++26`.

[tca]: https://www.pointfree.co/blog/posts/206-beta-preview-composablearchitecture-2-0
[deps]: https://github.com/pointfreeco/swift-dependencies
[isowords]: https://github.com/pointfreeco/isowords

### Modules (`src/`)

- `ComposableArchitecture` — core module (`:CasePath`, `:Effect`, `:Reducer`, `:Store`, `:TestStore` partitions)
- `Dependencies` — dependency container module (`:Core`, `:DateGenerator`, `:RandomNumberGenerator` partitions)
- `AudioPlayerClient` / `AudioPlayerClientLive` — audio dependency interface module and its live OpenAL implementation
- `PuzzleFeature` / `PuzzleFeatureView` — puzzle reducer module and its raylib view module
- `AppFeature` / `AppFeatureView` — composition-root reducer module scoping the puzzle, and its view

## Controls

- Move tile — Left mouse click or arrow keys
- Resize board — keys `0` (4×4) … `9` (13×13)
- Shuffle — S
- Restart — R
- Toggle tick sound — M
- Auto-solve (toggle) — H
- Near-win shortcut — double-press W
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

MSVC (Visual Studio 2022) ships its own `std` module. From a *Developer
Command Prompt for VS 2022* (so `cl` and Ninja are on `PATH`):

```bat
cmake --preset windows
cmake --build --preset windows
```

### Dependencies (fast vs. self-contained)

- **Default (dev/CI):** raylib + openal-soft are resolved **system-first** via
  `find_package` (the packages installed above). This skips compiling ~170
  third-party translation units and links them dynamically — a from-scratch
  build drops from **~65 s to ~2.5 s**. If a package is missing, CMake falls back
  to building it from pinned sources (FetchContent), so a fresh checkout still
  works anywhere. A `ccache` install is picked up automatically.
- **Release (`-DFIFTEEN_STATIC_DEPS=ON`):** the deps are built from source and
  linked **statically** into a self-contained, optimized binary. This is what
  the release workflow uses.

## Testing

Tests are `EXCLUDE_FROM_ALL`, so a normal build never recompiles them. Build and
run them on demand:

```sh
cmake --workflow --preset test          # macOS: configure + build tests + run
# or, against an existing build dir (any platform):
cmake --build --preset tests && ctest --preset macos   # tests / tests-linux / tests-windows
```

## Releases

Pushing a published GitHub Release triggers
[`release.yml`](.github/workflows/release.yml), which builds an optimized,
statically-linked binary (`FIFTEEN_STATIC_DEPS=ON`, `Release`) for macOS, Linux,
and Windows and attaches them to the release. Debug/CI builds instead link the
dependencies dynamically (faster to link). *(Binaries still depend on the
toolchain's C++ runtime — libc++ on Clang, the MSVC runtime on Windows.)*

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
