#!/bin/sh
# Builds and runs the core benchmarks on the development machine, with the optimisation flags of the Android build.
#
# Usage: app/src/test/native/bench/run.sh [runs]
# Set CXX to pick a compiler (for example CXX="clang++") and CXXFLAGS for extra flags.
# The timings are for this machine's processor, not a phone's.
set -e
here=$(cd "$(dirname "$0")" && pwd)
native="$here/.."
core="$here/../../../main/cpp/core"
out="${TMPDIR:-/tmp}/aimbuddy-core-bench"
mkdir -p "$out"
${CXX:-c++} -std=c++17 -O3 -fno-math-errno -funroll-loops -DNDEBUG -pthread ${CXXFLAGS:-} \
  -I"$native" -I"$core/.." \
  "$here/core_bench.cpp" "$core"/*.cpp \
  -o "$out/core_bench"
"$out/core_bench" "$@"
