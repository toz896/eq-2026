#!/bin/sh
# Pruebas del motor de audio y de la capa VST2 (Linux/macOS/WSL con g++)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -o build/test_dsp  tests/test_dsp.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fsanitize=address,undefined -o build/test_host tests/test_host.cpp src/plugin.cpp src/gui_stub.cpp
./build/test_dsp
./build/test_host
