#!/usr/bin/env bash
# Compiles cpp/terrain.cpp to WebAssembly and drops the result into the React
# app's source tree. Run from the repo root in Git Bash:  ./build.sh
set -e

source /c/Users/catzh/emsdk/emsdk_env.sh > /dev/null 2>&1
export SSL_CERT_FILE="C:\msys64\usr\ssl\certs\ca-bundle.crt"

mkdir -p web/src/wasm

emcc cpp/terrain.cpp \
  -O2 \
  -o web/src/wasm/terrain.js \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sENVIRONMENT=web \
  -sSINGLE_FILE=1 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS=_add,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPF32,HEAPU8

echo "built web/src/wasm/terrain.js"
