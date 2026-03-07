@echo on

cmake -S . -B build ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX="%LIBRARY_PREFIX%" ^
    -DSIGNET_BUILD_TESTS=OFF ^
    -DSIGNET_BUILD_BENCHMARKS=OFF ^
    -DSIGNET_BUILD_EXAMPLES=OFF ^
    -DSIGNET_BUILD_TOOLS=OFF ^
    -DSIGNET_BUILD_PYTHON=OFF ^
    -DSIGNET_BUILD_FUZZ=OFF
if errorlevel 1 exit 1

cmake --install build --prefix "%LIBRARY_PREFIX%"
if errorlevel 1 exit 1
