#!/bin/bash
# Builds the browser simulator:
#   1. compiles the portable firmware core + sim_api.cpp to WebAssembly
#      (clang with the wasm32-wasi target; on Ubuntu: apt install clang lld-18
#      wasi-libc libc++-18-dev-wasm32 libc++abi-18-dev-wasm32 libclang-rt-18-dev-wasm32)
#   2. runs the Node test of the WebAssembly core
#   3. inlines the .wasm and sim_core.js into simulator.html → lwave-sim.html
#
#   tools/sim/build.sh [output-dir]      (default: tools/sim)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FW="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$HERE}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

clang++ --target=wasm32-wasi -std=c++17 -O2 -fno-exceptions -fno-rtti \
  -mexec-model=reactor -Wl,--no-entry -Wl,--strip-debug \
  -I"$FW/lib/lwave/src" \
  "$FW"/lib/lwave/src/lwave/*.cpp "$FW"/lib/lwave/src/lwave/dsp/*.cpp "$HERE/sim_api.cpp" \
  -o "$TMP/lwave_sim.wasm"

node "$HERE/test_sim.js" "$TMP/lwave_sim.wasm"
python3 "$HERE/assemble.py" "$HERE/simulator.html" "$HERE/sim_core.js" "$TMP/lwave_sim.wasm" "$OUT"
