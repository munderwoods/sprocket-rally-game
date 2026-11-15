#!/bin/sh
# Builds ui_shots (the menu/HUD screenshot tool) against the engine's static libraries
# (build the game first). Usage: tools/headless/ui_shots <out-dir>, then convert the PPMs.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../..
K=$ROOT/../korvid/modules
S=$HERE/../../src
g++ -std=c++20 -O2 -g -fno-exceptions -fno-rtti -ffp-contract=off -fno-math-errno -Wall -Wextra -Werror \
  -I$K/core/include -I$K/math/include -I$K/platform_audio/include \
  $HERE/ui_shots.cc -DART_BIN=\"$HERE/../../assets/art.bin\" $S/art.cc $S/track.cc $S/race.cc $S/render.cc $S/hud.cc $S/menu.cc \
  $ROOT/build/linux-dev/lib/libkv_math.a $ROOT/build/linux-dev/lib/libkv_core.a -lpthread -lm \
  -o $HERE/ui_shots
