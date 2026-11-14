#!/bin/bash
#
# Builds the GoatTracker Ultimate plugin on Windows (Git Bash):
#   1. SDL2, static, for the plugin (plugin/cmake/sdl2-plugin.cmake)
#   2. the core library gtcore.dll with llvm-mingw (plugin/core)
#   3. the VST3 (and AAX with AAX_SDK_PATH) with Visual Studio (plugin/)
#
# Needs (set these, or pass them on the command line as NAME=value):
#   LLVM_MINGW   llvm-mingw UCRT x86_64 folder (github.com/mstorsjo/llvm-mingw)
#   SDL2_SRC     SDL2 2.32.x source folder (github.com/libsdl-org/SDL, release-2.32.10)
#   JUCE_DIR     JUCE 8 source folder (github.com/juce-framework/JUCE, tag 8.0.15)
# Optional:
#   AAX_SDK_PATH the AAX SDK (developer.avid.com): also builds the AAX plugin
#   BUILD_DIR    where to build (default: build-plugin next to the repository's src)
#   VS_GENERATOR CMake generator for the plugin (default "Visual Studio 17 2022")
#   TESTHOST=1   also builds the test host gttesthost
#
# The plugin ends up in $BUILD_DIR/shell/GoatTrackerUltimate_artefacts/Release/VST3.
# Copy "GoatTracker Ultimate.vst3" to C:\Program Files\Common Files\VST3.

set -e
for arg in "$@"; do export "$arg"; done

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(dirname "$HERE")"
BUILD_DIR="${BUILD_DIR:-$REPO/build-plugin}"
VS_GENERATOR="${VS_GENERATOR:-Visual Studio 17 2022}"

for v in LLVM_MINGW SDL2_SRC JUCE_DIR; do
  if [ -z "${!v}" ]; then echo "Set $v (see the top of this script)"; exit 1; fi
done
export PATH="$LLVM_MINGW/bin:$PATH"
MINGW_FLAGS=(-G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres
  -DCMAKE_MAKE_PROGRAM=mingw32-make)

echo "== SDL2 (static, for the plugin)"
cmake "${MINGW_FLAGS[@]}" -C "$HERE/cmake/sdl2-plugin.cmake" -S "$SDL2_SRC" -B "$BUILD_DIR/sdl2" \
  -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/sdl2-install" > /dev/null
cmake --build "$BUILD_DIR/sdl2" --parallel 8
cmake --install "$BUILD_DIR/sdl2" > /dev/null

echo "== gtcore.dll"
cmake "${MINGW_FLAGS[@]}" -S "$HERE/core" -B "$BUILD_DIR/core" -DSDL2_DIR="$BUILD_DIR/sdl2-install/lib/cmake/SDL2" > /dev/null
cmake --build "$BUILD_DIR/core" --parallel 8

echo "== Plugin"
EXTRA=()
if [ -n "$AAX_SDK_PATH" ]; then EXTRA+=(-DAAX_SDK_PATH="$(cygpath -m "$AAX_SDK_PATH")"); fi
TARGETS=(GoatTrackerUltimate_VST3)
if [ -n "$AAX_SDK_PATH" ]; then TARGETS+=(GoatTrackerUltimate_AAX); fi
if [ -n "$TESTHOST" ]; then EXTRA+=(-DGT_BUILD_TESTHOST=ON); TARGETS+=(gttesthost); fi
cmake -G "$VS_GENERATOR" -A x64 -S "$HERE" -B "$BUILD_DIR/shell" -DJUCE_DIR="$(cygpath -m "$JUCE_DIR")" \
  -DGTCORE_LIBRARY="$(cygpath -m "$BUILD_DIR/core/gtcore.dll")" "${EXTRA[@]}" > /dev/null
cmake --build "$BUILD_DIR/shell" --config Release --target "${TARGETS[@]}" --parallel 8

echo "== Done: $BUILD_DIR/shell/GoatTrackerUltimate_artefacts/Release"
