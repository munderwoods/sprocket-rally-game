#!/bin/sh
# Builds the headless harness against the engine's static libraries (build the game first).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../..
K=$ROOT/../korvid/modules
g++ -std=c++20 -O2 -g -fno-exceptions -fno-rtti -ffp-contract=off -fno-math-errno -Wall -Wextra \
  -I$K/core/include -I$K/math/include -I$K/platform_audio/include \
  $HERE/headless.cc -DART_BIN=\"$HERE/../../assets/art.bin\" $HERE/../../src/art.cc $HERE/../../src/track.cc $HERE/../../src/race.cc $HERE/../../src/render.cc $HERE/../../src/hud.cc \
  $ROOT/build/linux-dev/lib/libkv_math.a $ROOT/build/linux-dev/lib/libkv_core.a -lpthread -lm \
  -o $HERE/headless
