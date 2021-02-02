#!/usr/bin/env bash
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$IDF_PATH/tools/cmake/toolchain-esp32.cmake -DTARGET=esp32 -GNinja
cmake --build .
