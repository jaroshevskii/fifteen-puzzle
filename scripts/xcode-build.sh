#!/bin/sh
# Build entry point for the External Build System Xcode project.
#
# Xcode cannot build this codebase itself: its generator does not support C++20
# modules, and Apple Clang supports neither modules nor `import std;`. So the
# Xcode project delegates here, and we drive the real LLVM + Ninja build through
# the CMake preset. Xcode is used only as an editor / runner.
#
# Invoked as: xcode-build.sh [build|clean]  (ACTION is passed by Xcode)
set -e

# Xcode runs with a minimal PATH; make sure Homebrew tools are found.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

# Xcode exports its own SDK / deployment-target / build-setting variables into
# the environment. They leak into clang and change the target triple (e.g.
# macosx26.2 instead of the terminal default), which makes the prebuilt `std`
# module — shared in build/ with terminal builds — fail to load with a
# "configuration mismatch". Strip them so an Xcode-driven build is byte-for-byte
# the same as a plain `cmake --build` from the shell.
unset MACOSX_DEPLOYMENT_TARGET SDKROOT MACOS_DEPLOYMENT_TARGET \
      CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH SDK_DIR

cd "$(dirname "$0")/.."

action="${1:-build}"

case "$action" in
  clean)
    rm -rf build
    ;;
  *)
    # Configure on first build (or after the preset changes), then build.
    if [ ! -f build/CMakeCache.txt ]; then
      cmake --preset macos
    fi
    cmake --build --preset macos
    ;;
esac
