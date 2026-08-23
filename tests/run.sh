#!/bin/sh
# Configure and run the host tests. See README.md for what they cover.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug > /dev/null
cmake --build build --parallel
ctest --test-dir build --output-on-failure
