# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026 Johnson Ogundeji
# Benchmarking_Protocols/CMakeLists.txt — Enterprise benchmark suite (59 cases)

set(SIGNET_EBENCH_SOURCES
    bench_phase2_write.cpp
    bench_phase3_read.cpp
    bench_phase4_predicate.cpp
    bench_phase5_wal.cpp
    bench_phase6_features.cpp
    bench_phase7_ai.cpp
    bench_phase8_compliance.cpp
    bench_phase9_interop.cpp
    bench_phase10_roundtrip.cpp
)

add_executable(signet_enterprise_bench ${SIGNET_EBENCH_SOURCES})

target_link_libraries(signet_enterprise_bench
    PRIVATE
        signet::forge
        Catch2::Catch2WithMain
)

target_compile_options(signet_enterprise_bench
    PRIVATE
        -O2
        -Wall
        -Wextra
        -Wpedantic
)

# Pass the data directory path so common.hpp can find tick subsets
target_compile_definitions(signet_enterprise_bench
    PRIVATE
        SIGNET_BENCH_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

if(SIGNET_ENABLE_PQ)
    target_compile_definitions(signet_enterprise_bench PRIVATE SIGNET_HAS_LIBOQS=1)
endif()

catch_discover_tests(signet_enterprise_bench)

# --- Compression size report tool ---
add_executable(compression_sizes compression_sizes.cpp)
target_link_libraries(compression_sizes PRIVATE signet::forge)
target_compile_options(compression_sizes PRIVATE -O2 -Wall)
target_compile_definitions(compression_sizes
    PRIVATE
        SIGNET_BENCH_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
