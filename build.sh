#!/usr/bin/env bash
# Compiles cpp/terrain.cpp to WebAssembly and drops the result into the React
# app's source tree. Run from the repo root:  ./build.sh
set -e

# Locate Emscripten. In CI, emcc is already on PATH (the setup action puts it
# there), so we only source an emsdk environment when it is missing. EMSDK_DIR
# can override the default for a local install elsewhere.
if ! command -v emcc > /dev/null 2>&1; then
  EMSDK_DIR="${EMSDK_DIR:-$HOME/emsdk}"

  if [ -f "$EMSDK_DIR/emsdk_env.sh" ]; then
    # shellcheck disable=SC1090
    source "$EMSDK_DIR/emsdk_env.sh" > /dev/null 2>&1
  else
    echo "emcc not found, and no emsdk_env.sh at $EMSDK_DIR" >&2
    echo "Install Emscripten, or set EMSDK_DIR to your emsdk checkout." >&2
    exit 1
  fi
fi

# MSYS2's Python ships without a CA bundle, so emcc's first-run downloads fail
# SSL verification. Harmless elsewhere.
if [ -z "$SSL_CERT_FILE" ] && [ -f "/c/msys64/usr/ssl/certs/ca-bundle.crt" ]; then
  export SSL_CERT_FILE="C:\\msys64\\usr\\ssl\\certs\\ca-bundle.crt"
fi

mkdir -p web/src/wasm

emcc cpp/terrain.cpp \
  -O3 \
  -o web/src/wasm/terrain.js \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sENVIRONMENT=web \
  -sSINGLE_FILE=1 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS=_generate,_erode,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPF32,HEAPU8

echo "built web/src/wasm/terrain.js"
