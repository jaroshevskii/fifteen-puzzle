# 15 Puzzle

[![CI](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/ci.yml/badge.svg)](https://github.com/jaroshevskii/fifteen-puzzle/actions/workflows/ci.yml)

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
- Shuffle — S
- Restart — R
- Toggle tick sound — M
- Near-win shortcut — double-press W
- Restart after victory — Mouse click or R

## Game Logic

- The empty tile is represented by `""`
- Movement is allowed only for tiles adjacent to the empty square
- Shuffles are validated for solvability
- Victory condition: tiles 1–15 are ordered and the empty tile is last

## Building and testing

C++ modules and `import std;` require **upstream LLVM** (Apple Clang supports
neither) plus the **Ninja** generator:

```sh
brew install llvm ninja raylib openal-soft

cmake --preset default        # configure
cmake --build --preset default # build the app (tests are not built here)
```

Tests are `EXCLUDE_FROM_ALL`, so a normal build never recompiles them. Build and
run them on demand:

```sh
cmake --workflow --preset test   # configure + build tests + run them
# or, against an existing build dir:
cmake --build --preset tests && ctest --preset default
```

Dependencies are resolved **system-first**: if `raylib` / `openal-soft` are
installed (above), CMake uses the prebuilt packages via `find_package` and skips
compiling them entirely — a from-scratch build then takes **~2.5 s** instead of
**~65 s**. Without them, it falls back to building pinned sources via
FetchContent, so a fresh checkout still works anywhere. A `ccache` install is
picked up automatically to cache any source builds.

The preset selects Homebrew LLVM, points CMake at libc++'s `std` module, and
links against LLVM's libc++ runtime. `import std;` is gated behind CMake's
experimental flag (`CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`), so the exact CMake
version matters; the gate UUID in `CMakeLists.txt` matches CMake 4.2.x.

### Working in Xcode

`FifteenPuzzle.xcodeproj` is an **External Build System** project: open it and
Build (⌘B) / Run (⌘R) delegate to the CMake + Ninja build via
`scripts/xcode-build.sh`, using Xcode purely as an editor and runner.

This indirection is necessary — Xcode's CMake generator does not support C++20
modules, and Apple Clang supports neither modules nor `import std;`, so Xcode
cannot build this code natively. For the same reason, Xcode's indexer will not
fully understand `import std;` / `import <Module>;`; an editor backed by
`compile_commands.json` (e.g. VS Code or CLion) gives better navigation.

### Other platforms

CI builds and tests on macOS, Linux, and Windows (see
[`.github/workflows/ci.yml`](.github/workflows/ci.yml)); matching presets are
provided:

- **Linux** — upstream LLVM + libc++ (`clang++ -stdlib=libc++`):
  `CC=clang-21 CXX=clang++-21 cmake --preset linux` then build/test with the
  `linux` preset.
- **Windows** — MSVC (ships its own `std` module): from a Visual Studio
  developer prompt, `cmake --preset windows` then build/test with the `windows`
  preset.

## Possible Improvements

- Move counter
- Animation transitions
- Adaptive layout

## License

This project is licensed under the MIT License. See the [LICENSE](./LICENSE.md) file for details.
