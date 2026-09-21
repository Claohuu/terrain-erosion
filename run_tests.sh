#!/usr/bin/env bash
set -e

mkdir -p build

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
