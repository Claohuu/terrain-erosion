#!/usr/bin/env bash
# Builds and runs the native test suite. No Emscripten, no browser -- the
# simulation is plain C++ and can be tested as such.
set -e

mkdir -p build

# AddressSanitizer catches the out-of-bounds writes this code could plausibly
# make, but MinGW does not ship the runtime. Use it where it exists (Linux CI)
# and fall back to the guard-band checks in the suite itself.
SANITIZE=""

if echo 'int main(){return 0;}' | g++ -fsanitize=address,undefined -x c++ - -o /dev/null 2> /dev/null; then
  SANITIZE="-fsanitize=address,undefined"
  echo "building with sanitizers"
else
  echo "sanitizers unavailable on this toolchain, relying on guard bands"
fi

rm -f /dev/null.exe

g++ -std=c++17 -O2 -Wall -Wextra $SANITIZE \
  -o build/test_terrain \
  cpp/test_terrain.cpp

./build/test_terrain
