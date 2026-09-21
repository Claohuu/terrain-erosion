#!/usr/bin/env bash
set -e

mkdir -p build

g++ -std=c++17 -O3 -Wall -Wextra \
  -o build/bench_terrain \
  cpp/bench_terrain.cpp

./build/bench_terrain
