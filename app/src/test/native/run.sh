#!/bin/sh
# Builds and runs the pure C++ core tests on the development machine.
#
# Usage: app/src/test/native/run.sh
# Set CXX to pick a compiler (for example CXX="clang++") and CXXFLAGS for extra flags.
set -e
here=$(cd "$(dirname "$0")" && pwd)
core="$here/../../main/cpp/core"
settings="$here/../../main/cpp/settings"
out="${TMPDIR:-/tmp}/aimbuddy-core-tests"
mkdir -p "$out"
${CXX:-c++} -std=c++17 -O2 -g -Wall -Wextra -Werror -pthread ${CXXFLAGS:-} \
  -I"$here" -I"$core/.." \
  "$here/main.cpp" "$here"/*_test.cpp "$core"/*.cpp "$settings"/*.cpp \
  -o "$out/core_tests"
"$out/core_tests"
