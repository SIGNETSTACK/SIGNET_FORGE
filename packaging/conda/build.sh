#!/usr/bin/env bash
set -euxo pipefail

cmake -S . -B build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DSIGNET_BUILD_TESTS=OFF \
    -DSIGNET_BUILD_BENCHMARKS=OFF \
    -DSIGNET_BUILD_EXAMPLES=OFF \
    -DSIGNET_BUILD_TOOLS=OFF \
    -DSIGNET_BUILD_PYTHON=OFF \
    -DSIGNET_BUILD_FUZZ=OFF

cmake --install build --prefix "$PREFIX"
