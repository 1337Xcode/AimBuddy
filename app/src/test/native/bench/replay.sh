#!/bin/sh
# Builds the clip replay tool and replays prepared clips through the app's decoder, camera motion and tracker.
#
# Usage: app/src/test/native/bench/replay.sh <detections per second> <latency ms> <confidence> <clip model folder> [...]
# The clips come from training/universal/clip_frames.py; training/universal/score_clips.py scores the result.
# Set CXX to pick a compiler (for example CXX="clang++") and CXXFLAGS for extra flags.
set -e
here=$(cd "$(dirname "$0")" && pwd)
core="$here/../../../main/cpp/core"
out="${TMPDIR:-/tmp}/aimbuddy-clip-replay"
mkdir -p "$out"
${CXX:-c++} -std=c++17 -O2 -DNDEBUG -pthread ${CXXFLAGS:-} \
  -I"$core/.." \
  "$here/clip_replay.cpp" "$core"/*.cpp \
  -o "$out/clip_replay"
"$out/clip_replay" "$@"
