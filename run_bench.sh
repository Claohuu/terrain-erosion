#!/usr/bin/env bash
# Builds and runs the native scaling benchmark. Uses -O3 to match the
# WebAssembly build, so the numbers are comparable in shape if not absolutely.
set -e

mkdir -p build

g++ -std=c++17 -O3 -Wall -Wextra \
  -o build/bench_terrain \
  cpp/bench_terrain.cpp

./build/bench_terrain
