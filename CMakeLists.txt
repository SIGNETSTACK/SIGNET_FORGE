cmake_minimum_required(VERSION 3.21)

project(signet_forge
    VERSION 0.1.0
    DESCRIPTION "SignetStack — Standalone C++20 Parquet library with AI-native extensions"
    HOMEPAGE_URL "https://github.com/SIGNETSTACK/signet-forge"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(SIGNET_BUILD_TESTS       "Build unit tests"          OFF)
option(SIGNET_BUILD_BENCHMARKS  "Build Catch2 benchmarks"   OFF)
option(SIGNET_BUILD_EXAMPLES "Build example programs"    OFF)
option(SIGNET_BUILD_TOOLS    "Build CLI tools"           OFF)
option(SIGNET_ENABLE_ZSTD    "Enable ZSTD compression"   OFF)
option(SIGNET_ENABLE_LZ4     "Enable LZ4 compression"    OFF)
option(SIGNET_ENABLE_GZIP    "Enable Gzip compression"   OFF)
option(SIGNET_ENABLE_CRYPTO  "Enable AES/PME encryption"  OFF)
option(SIGNET_ENABLE_PQ      "Enable post-quantum crypto" OFF)
option(SIGNET_BUILD_PYTHON    "Build pybind11 Python module"              OFF)
option(SIGNET_ENABLE_COMMERCIAL "Enable AGPL-3.0 commercial tier (encryption + compliance reporters)" OFF)
option(SIGNET_BUILD_AI_AUDIT "Deprecated alias for SIGNET_ENABLE_COMMERCIAL" OFF)
option(SIGNET_BUILD_FUZZ      "Build libFuzzer fuzz harnesses" OFF)
option(SIGNET_REQUIRE_COMMERCIAL_LICENSE "Require runtime commercial license validation (SIGNET_COMMERCIAL_LICENSE_KEY)" OFF)
option(SIGNET_REQUIRE_REAL_PQ "Disallow bundled PQ stubs; require liboqs-backed Kyber/Dilithium" OFF)
set(SIGNET_EVAL_MAX_ROWS_MONTH "50000000" CACHE STRING
    "Default evaluation-tier monthly processed-row cap (0 disables row cap)")
set(SIGNET_EVAL_MAX_USERS "3" CACHE STRING
    "Default evaluation-tier unique user cap (0 disables user cap)")
set(SIGNET_EVAL_MAX_NODES "1" CACHE STRING
    "Default evaluation-tier unique node cap (0 disables node cap)")
set(SIGNET_EVAL_MAX_DAYS "30" CACHE STRING
    "Default evaluation-tier duration cap in days from first use (0 disables day cap)")
set(SIGNET_EVAL_WARN_PCT_1 "80" CACHE STRING
    "Default evaluation-tier first warning threshold percentage")
set(SIGNET_EVAL_WARN_PCT_2 "90" CACHE STRING
    "Default evaluation-tier second warning threshold percentage")
set(SIGNET_COMMERCIAL_LICENSE_HASH "" CACHE STRING
    "64-bit FNV-1a hash literal for SIGNET_COMMERCIAL_LICENSE_KEY (example: 0x0123abcd)")
option(SIGNET_BUILD_WASM     "Build WASM target via Emscripten embind" OFF)
option(SIGNET_MINIMAL_DEPS    "Disable ALL optional external dependencies (for embedded/microcontroller builds)" OFF)

if(SIGNET_BUILD_AI_AUDIT)
    message(WARNING "SIGNET_BUILD_AI_AUDIT is deprecated; use SIGNET_ENABLE_COMMERCIAL instead.")
    set(SIGNET_ENABLE_COMMERCIAL ON CACHE BOOL
        "Enable AGPL-3.0 commercial tier (encryption + compliance reporters)" FORCE)
endif()

if(SIGNET_ENABLE_CRYPTO)
    message(WARNING "SIGNET_ENABLE_CRYPTO now maps to the commercial tier; enabling SIGNET_ENABLE_COMMERCIAL.")
    set(SIGNET_ENABLE_COMMERCIAL ON CACHE BOOL
        "Enable AGPL-3.0 commercial tier (encryption + compliance reporters)" FORCE)
endif()

if(SIGNET_ENABLE_PQ AND NOT SIGNET_ENABLE_COMMERCIAL)
    message(FATAL_ERROR "SIGNET_ENABLE_PQ requires SIGNET_ENABLE_COMMERCIAL=ON.")
endif()

foreach(_eval_var
        SIGNET_EVAL_MAX_ROWS_MONTH
        SIGNET_EVAL_MAX_USERS
        SIGNET_EVAL_MAX_NODES
        SIGNET_EVAL_MAX_DAYS
        SIGNET_EVAL_WARN_PCT_1
        SIGNET_EVAL_WARN_PCT_2)
    if(NOT ${_eval_var} MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${_eval_var} must be an unsigned integer.")
    endif()
endforeach()
if(SIGNET_EVAL_WARN_PCT_1 GREATER 100 OR SIGNET_EVAL_WARN_PCT_2 GREATER 100)
    message(FATAL_ERROR "SIGNET_EVAL_WARN_PCT_1 and SIGNET_EVAL_WARN_PCT_2 must be <= 100.")
endif()
if(SIGNET_EVAL_WARN_PCT_1 GREATER SIGNET_EVAL_WARN_PCT_2)
    message(FATAL_ERROR "SIGNET_EVAL_WARN_PCT_1 must be <= SIGNET_EVAL_WARN_PCT_2.")
endif()

if(SIGNET_REQUIRE_REAL_PQ AND NOT SIGNET_ENABLE_COMMERCIAL)
    message(FATAL_ERROR "SIGNET_REQUIRE_REAL_PQ requires SIGNET_ENABLE_COMMERCIAL=ON.")
endif()

if(SIGNET_REQUIRE_REAL_PQ AND NOT SIGNET_ENABLE_PQ)
    message(FATAL_ERROR "SIGNET_REQUIRE_REAL_PQ requires SIGNET_ENABLE_PQ=ON (liboqs-backed PQ).")
endif()

# SIGNET_MINIMAL_DEPS override — one flag to strip all external libs
if(SIGNET_MINIMAL_DEPS)
    set(SIGNET_ENABLE_ZSTD     OFF CACHE BOOL "Overridden by SIGNET_MINIMAL_DEPS" FORCE)
    set(SIGNET_ENABLE_LZ4      OFF CACHE BOOL "Overridden by SIGNET_MINIMAL_DEPS" FORCE)
    set(SIGNET_ENABLE_GZIP     OFF CACHE BOOL "Overridden by SIGNET_MINIMAL_DEPS" FORCE)
    set(SIGNET_ENABLE_PQ       OFF CACHE BOOL "Overridden by SIGNET_MINIMAL_DEPS" FORCE)
    set(SIGNET_ENABLE_COMMERCIAL OFF CACHE BOOL "Overridden by SIGNET_MINIMAL_DEPS" FORCE)
    set(SIGNET_BUILD_AI_AUDIT  OFF CACHE BOOL "Overridden by SIGNET_MINIMAL_DEPS" FORCE)
    message(STATUS "SIGNET_MINIMAL_DEPS=ON: all optional external dependencies disabled")
endif()

# ---------------------------------------------------------------------------
# Core header-only library
# ---------------------------------------------------------------------------
add_library(signet_forge INTERFACE)
target_include_directories(signet_forge INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(signet_forge INTERFACE cxx_std_20)

# Suppress MSVC deprecation warnings for standard C functions (fopen, strerror, getenv, etc.)
if(MSVC)
    target_compile_definitions(signet_forge INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()

# Alias for FetchContent consumers
add_library(signet::forge ALIAS signet_forge)

# ---------------------------------------------------------------------------
# Optional external dependencies
# ---------------------------------------------------------------------------

# ZSTD compression (BSD-3, ~640KB — install: brew install zstd / apt install libzstd-dev)
if(SIGNET_ENABLE_ZSTD)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ZSTD REQUIRED libzstd)
        target_compile_definitions(signet_forge INTERFACE SIGNET_HAS_ZSTD=1)
        target_include_directories(signet_forge INTERFACE ${ZSTD_INCLUDE_DIRS})
        target_link_libraries(signet_forge INTERFACE ${ZSTD_LIBRARIES})
    else()
        find_package(zstd REQUIRED
            HINTS /usr/local /opt/homebrew /usr
        )
        if(TARGET zstd::libzstd_static)
            target_link_libraries(signet_forge INTERFACE zstd::libzstd_static)
        elseif(TARGET zstd::libzstd_shared)
            target_link_libraries(signet_forge INTERFACE zstd::libzstd_shared)
        else()
            message(FATAL_ERROR
                "SIGNET_ENABLE_ZSTD=ON but libzstd not found.\n"
                "Install via:  brew install zstd        (macOS)\n"
                "              apt install libzstd-dev  (Ubuntu/Debian)\n"
                "              vcpkg install zstd       (vcpkg)\n"
                "Or disable:   -DSIGNET_ENABLE_ZSTD=OFF\n"
                "Or use:       -DSIGNET_MINIMAL_DEPS=ON (disables all optional deps)")
        endif()
        target_compile_definitions(signet_forge INTERFACE SIGNET_HAS_ZSTD=1)
    endif()
    message(STATUS "SIGNET_ENABLE_ZSTD=ON: ZSTD compression enabled")
endif()

# LZ4 compression (BSD-2, ~250KB — install: brew install lz4 / apt install liblz4-dev)
if(SIGNET_ENABLE_LZ4)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LZ4 REQUIRED liblz4)
        target_compile_definitions(signet_forge INTERFACE SIGNET_HAS_LZ4=1)
        target_include_directories(signet_forge INTERFACE ${LZ4_INCLUDE_DIRS})
        target_link_libraries(signet_forge INTERFACE ${LZ4_LIBRARIES})
    else()
        find_path(LZ4_INCLUDE_DIR lz4.h)
        find_library(LZ4_LIBRARY NAMES lz4)
        if(NOT LZ4_INCLUDE_DIR OR NOT LZ4_LIBRARY)
            message(FATAL_ERROR
                "SIGNET_ENABLE_LZ4=ON but liblz4 not found.\n"
                "Install via:  brew install lz4         (macOS)\n"
                "              apt install liblz4-dev   (Ubuntu/Debian)\n"
                "Or disable:   -DSIGNET_ENABLE_LZ4=OFF")
        endif()
        target_include_directories(signet_forge INTERFACE ${LZ4_INCLUDE_DIR})
        target_link_libraries(signet_forge INTERFACE ${LZ4_LIBRARY})
        target_compile_definitions(signet_forge INTERFACE SIGNET_HAS_LZ4=1)
    endif()
    message(STATUS "SIGNET_ENABLE_LZ4=ON: LZ4 compression enabled")
endif()

# Gzip compression via zlib (zlib license — pre-installed on macOS/Linux)
if(SIGNET_ENABLE_GZIP)
    find_package(ZLIB REQUIRED)
    target_link_libraries(signet_forge INTERFACE ZLIB::ZLIB)
    target_compile_definitions(signet_forge INTERFACE SIGNET_HAS_GZIP=1)
    message(STATUS "SIGNET_ENABLE_GZIP=ON: Gzip/zlib compression enabled")
endif()

# Post-quantum crypto via liboqs (MIT, ~10MB — install: brew install liboqs / apt install liboqs-dev)
# Enables real NIST FIPS 203 Kyber-768 and NIST FIPS 204 Dilithium-3.
# Without this flag, X25519 is real (RFC 7748) but Kyber/Dilithium use SHA-256 stubs.
if(SIGNET_ENABLE_PQ)
    find_package(liboqs QUIET
        HINTS /usr/local /opt/homebrew /usr /usr/local/opt/liboqs
    )
    if(NOT liboqs_FOUND)
        # Try pkg-config fallback
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBOQS liboqs)
        endif()
        if(NOT LIBOQS_FOUND)
            # Try manual discovery
            find_path(OQS_INCLUDE_DIR oqs/oqs.h
                HINTS /usr/local/include /opt/homebrew/include /usr/include)
            find_library(OQS_LIBRARY NAMES oqs
                HINTS /usr/local/lib /opt/homebrew/lib /usr/lib)
            if(NOT OQS_INCLUDE_DIR OR NOT OQS_LIBRARY)
                message(FATAL_ERROR
                    "SIGNET_ENABLE_PQ=ON but liboqs not found.\n"
                    "Install via:  brew install liboqs             (macOS)\n"
                    "              apt install liboqs-dev          (Ubuntu 24.04+)\n"
                    "Or build from source:\n"
                    "  git clone https://github.com/open-quantum-safe/liboqs\n"
                    "  cmake -S liboqs -B liboqs/build -DOQS_BUILD_ONLY_LIB=ON\n"
                    "  cmake --build liboqs/build && sudo cmake --install liboqs/build\n"
                    "Or disable: -DSIGNET_ENABLE_PQ=OFF\n"
                    "Note: Without liboqs, X25519 (classical DH) is always available.\n"
                    "      Only Kyber-768 and Dilithium-3 require liboqs.")
            endif()
            target_include_directories(signet_forge INTERFACE ${OQS_INCLUDE_DIR})
            target_link_libraries(signet_forge INTERFACE ${OQS_LIBRARY})
        else()
            target_include_directories(signet_forge INTERFACE ${LIBOQS_INCLUDE_DIRS})
            target_link_libraries(signet_forge INTERFACE ${LIBOQS_LIBRARIES})
        endif()
    else()
        target_link_libraries(signet_forge INTERFACE OQS::oqs)
    endif()
    target_compile_definitions(signet_forge INTERFACE SIGNET_HAS_LIBOQS=1)
    message(STATUS "SIGNET_ENABLE_PQ=ON: Real Kyber-768 + Dilithium-3 via liboqs enabled")
endif()

# Propagate commercial gate to all consumers
if(SIGNET_ENABLE_COMMERCIAL)
    target_compile_definitions(signet_forge INTERFACE
        SIGNET_ENABLE_COMMERCIAL=1
        SIGNET_BUILD_AI_AUDIT=1
        SIGNET_EVAL_MAX_ROWS_MONTH_U64=${SIGNET_EVAL_MAX_ROWS_MONTH}
        SIGNET_EVAL_MAX_USERS_U32=${SIGNET_EVAL_MAX_USERS}
        SIGNET_EVAL_MAX_NODES_U32=${SIGNET_EVAL_MAX_NODES}
        SIGNET_EVAL_MAX_DAYS_U32=${SIGNET_EVAL_MAX_DAYS}
        SIGNET_EVAL_WARN_PCT_1_U32=${SIGNET_EVAL_WARN_PCT_1}
        SIGNET_EVAL_WARN_PCT_2_U32=${SIGNET_EVAL_WARN_PCT_2}
    )

    if(SIGNET_REQUIRE_COMMERCIAL_LICENSE)
        if(SIGNET_COMMERCIAL_LICENSE_HASH STREQUAL "")
            message(FATAL_ERROR
                "SIGNET_ENABLE_COMMERCIAL=ON requires SIGNET_COMMERCIAL_LICENSE_HASH.\n"
                "Provide a 64-bit FNV-1a hash literal for SIGNET_COMMERCIAL_LICENSE_KEY,\n"
                "for example: -DSIGNET_COMMERCIAL_LICENSE_HASH=0x1234abcd")
        endif()
        if(NOT SIGNET_COMMERCIAL_LICENSE_HASH MATCHES "^0[xX][0-9A-Fa-f]+$")
            message(FATAL_ERROR
                "SIGNET_COMMERCIAL_LICENSE_HASH must be a hex literal (e.g. 0x1234abcd)")
        endif()
        target_compile_definitions(signet_forge INTERFACE
            SIGNET_REQUIRE_COMMERCIAL_LICENSE=1
            SIGNET_COMMERCIAL_LICENSE_HASH_U64=${SIGNET_COMMERCIAL_LICENSE_HASH}
        )
    else()
        target_compile_definitions(signet_forge INTERFACE SIGNET_REQUIRE_COMMERCIAL_LICENSE=0)
    endif()

    message(STATUS "SIGNET_ENABLE_COMMERCIAL=ON: AGPL-3.0 commercial tier enabled")
else()
    target_compile_definitions(signet_forge INTERFACE
        SIGNET_ENABLE_COMMERCIAL=0
        SIGNET_BUILD_AI_AUDIT=0
        SIGNET_REQUIRE_COMMERCIAL_LICENSE=0
    )
endif()

if(SIGNET_REQUIRE_REAL_PQ)
    target_compile_definitions(signet_forge INTERFACE SIGNET_REQUIRE_REAL_PQ=1)
else()
    target_compile_definitions(signet_forge INTERFACE SIGNET_REQUIRE_REAL_PQ=0)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SIGNET_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        URL            https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
        URL_HASH       SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
    )
    FetchContent_MakeAvailable(Catch2)
    include(CTest)
    include(Catch)

    set(SIGNET_TEST_SOURCES
        tests/test_roundtrip.cpp
        tests/test_csv.cpp
        tests/test_encoding.cpp
        tests/test_compression.cpp
        tests/test_bloom.cpp
        tests/test_interop.cpp
        tests/test_vector_type.cpp
        tests/test_tensor_bridge.cpp
        tests/test_wal.cpp
        tests/test_feature_store.cpp
        tests/test_event_bus.cpp
        tests/test_ticks_pipeline.cpp
        tests/test_z_order.cpp
        tests/test_page_index.cpp
        tests/test_property_based.cpp
        tests/test_api_compatibility.cpp
        tests/test_resilience.cpp
        tests/test_encoding_vectors.cpp
        tests/test_thrift_snappy_vectors.cpp
        tests/test_bloom_vectors.cpp
        tests/test_thrift_types.cpp
    )
    if(SIGNET_ENABLE_COMMERCIAL)
        list(APPEND SIGNET_TEST_SOURCES
            tests/test_encryption.cpp
            tests/test_audit_chain.cpp
            tests/test_compliance_reports.cpp
            tests/test_gap_pass12.cpp
            tests/test_crypto_vectors.cpp
        )
    endif()
    add_executable(signet_tests ${SIGNET_TEST_SOURCES})
    target_link_libraries(signet_tests PRIVATE signet::forge Catch2::Catch2WithMain)
    if(MSVC)
        target_compile_options(signet_tests PRIVATE /W4)
    else()
        target_compile_options(signet_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(SIGNET_ENABLE_PQ)
        target_compile_definitions(signet_tests PRIVATE SIGNET_HAS_LIBOQS=1)
    endif()
    catch_discover_tests(signet_tests
        PROPERTIES TIMEOUT 120)  # 2 min per test — prevents CI hangs
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(SIGNET_BUILD_BENCHMARKS)
    if(NOT TARGET Catch2::Catch2WithMain)
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            URL            https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
            URL_HASH       SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
        )
        FetchContent_MakeAvailable(Catch2)
        include(CTest)
        include(Catch)
    endif()

    add_executable(signet_benchmarks
        benchmarks/bench_write.cpp
        benchmarks/bench_read.cpp
        benchmarks/bench_wal.cpp
        benchmarks/bench_encodings.cpp
        benchmarks/bench_feature_store.cpp
        benchmarks/bench_event_bus.cpp
    )
    target_link_libraries(signet_benchmarks PRIVATE signet::forge Catch2::Catch2WithMain)
    if(MSVC)
        target_compile_options(signet_benchmarks PRIVATE /W4 /O2)
    else()
        target_compile_options(signet_benchmarks PRIVATE -Wall -Wextra -Wpedantic -O2)
    endif()
    catch_discover_tests(signet_benchmarks)

    # Enterprise benchmark suite (59 cases across 9 phases)
    add_subdirectory(Benchmarking_Protocols)
endif()

# ---------------------------------------------------------------------------
# Python bindings (pybind11)
# ---------------------------------------------------------------------------
if(SIGNET_BUILD_PYTHON)
    if(NOT TARGET pybind11::pybind11)
        include(FetchContent)
        FetchContent_Declare(
            pybind11
            URL            https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.tar.gz
            URL_HASH       SHA256=e08cb87f4773da97fa7b5f035de8763abc656d87d5773e62f6da0587d1f0ec20
        )
        FetchContent_MakeAvailable(pybind11)
    endif()

    pybind11_add_module(_bindings
        python/signet_forge/_bindings.cpp
    )
    target_link_libraries(_bindings PRIVATE signet::forge)
    target_compile_features(_bindings PRIVATE cxx_std_20)
    if(SIGNET_ENABLE_COMMERCIAL)
        target_compile_definitions(_bindings PRIVATE SIGNET_HAS_AI_AUDIT=1)
    endif()

    # Install the .so next to __init__.py so `import signet_forge` works
    install(TARGETS _bindings
        LIBRARY DESTINATION python/signet_forge
        COMPONENT python
    )
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(SIGNET_BUILD_EXAMPLES)
    add_executable(example_basic_write examples/basic_write.cpp)
    target_link_libraries(example_basic_write PRIVATE signet::forge)

    add_executable(example_basic_read examples/basic_read.cpp)
    target_link_libraries(example_basic_read PRIVATE signet::forge)

    add_executable(example_csv_to_parquet examples/csv_to_parquet.cpp)
    target_link_libraries(example_csv_to_parquet PRIVATE signet::forge)

    # Financial tick data examples — demonstrate Signet with real HFT market data
    # Input: gzip-compressed CSV with 9-column tick schema (timestamp_ms, symbol,
    #        exchange, bid_price, ask_price, bid_qty, ask_qty, spread_bps, mid_price)
    add_executable(example_ticks_import examples/ticks_import.cpp)
    target_link_libraries(example_ticks_import PRIVATE signet::forge)
    if(NOT MSVC)
        target_compile_options(example_ticks_import PRIVATE -O2)
    endif()

    add_executable(example_ticks_query examples/ticks_query.cpp)
    target_link_libraries(example_ticks_query PRIVATE signet::forge)
    if(NOT MSVC)
        target_compile_options(example_ticks_query PRIVATE -O2)
    endif()

    add_executable(example_ticks_wal_stream examples/ticks_wal_stream.cpp)
    target_link_libraries(example_ticks_wal_stream PRIVATE signet::forge)
    if(NOT MSVC)
        target_compile_options(example_ticks_wal_stream PRIVATE -O2)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(SIGNET_BUILD_TOOLS)
    add_executable(signet_cli tools/signet_cli.cpp)
    target_link_libraries(signet_cli PRIVATE signet::forge)
    if(MSVC)
        target_compile_options(signet_cli PRIVATE /W4)
    else()
        target_compile_options(signet_cli PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(SIGNET_ENABLE_COMMERCIAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/signet_license_gen.cpp")
        add_executable(signet_license_gen tools/signet_license_gen.cpp)
        target_link_libraries(signet_license_gen PRIVATE signet::forge)
        if(MSVC)
            target_compile_options(signet_license_gen PRIVATE /W4)
        else()
            target_compile_options(signet_license_gen PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# FFI — C API static library for Rust/other FFI consumers
# ---------------------------------------------------------------------------
option(SIGNET_BUILD_FFI "Build C FFI static library (libsignet_forge_c)" OFF)
if(SIGNET_BUILD_FFI)
    add_library(signet_forge_c STATIC ffi/signet_forge_c.cpp)
    target_include_directories(signet_forge_c
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ffi>
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(signet_forge_c PRIVATE signet::forge)
    set_target_properties(signet_forge_c PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
    if(MSVC)
        target_compile_options(signet_forge_c PRIVATE /W4)
    else()
        target_compile_options(signet_forge_c PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    include(GNUInstallDirs)
    install(TARGETS signet_forge_c
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(FILES ffi/signet_forge.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    message(STATUS "SIGNET_BUILD_FFI=ON: C FFI static library enabled")
endif()

# ---------------------------------------------------------------------------
# WASM — Emscripten embind target (browser-side Parquet preview)
# ---------------------------------------------------------------------------
if(SIGNET_BUILD_WASM AND EMSCRIPTEN)
    add_executable(signet_wasm wasm/signet_wasm.cpp)
    target_link_libraries(signet_wasm PRIVATE signet::forge)
    target_compile_features(signet_wasm PRIVATE cxx_std_20)
    target_link_options(signet_wasm PRIVATE
        --bind
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1
        -sMODULARIZE=1
        -sEXPORT_NAME=SignetForge
        -sENVIRONMENT=web
        -sFORCE_FILESYSTEM=1
        "-sEXPORTED_RUNTIME_METHODS=['FS']"
        -fexceptions
    )
    target_compile_options(signet_wasm PRIVATE -fexceptions)
    set_target_properties(signet_wasm PROPERTIES
        SUFFIX ".js"
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/signet_wasm.js
        ${CMAKE_CURRENT_BINARY_DIR}/signet_wasm.wasm
        DESTINATION share/signet_forge/wasm
    )
    message(STATUS "SIGNET_BUILD_WASM=ON: Emscripten WASM target enabled")
endif()

# ---------------------------------------------------------------------------
# Fuzz harnesses (libFuzzer — requires Clang)
# ---------------------------------------------------------------------------
if(SIGNET_BUILD_FUZZ)
    set(SIGNET_FUZZ_TARGETS
        fuzz_parquet_reader
        fuzz_thrift_decoder
        fuzz_wal_reader
        fuzz_rle_decoder
        fuzz_delta_decoder
        fuzz_arrow_import
        fuzz_aes_gcm
        fuzz_pme
        fuzz_key_metadata
        fuzz_hkdf
        fuzz_x25519
    )
    foreach(_target ${SIGNET_FUZZ_TARGETS})
        add_executable(${_target} fuzz/${_target}.cpp)
        target_link_libraries(${_target} PRIVATE signet::forge)
        target_compile_options(${_target} PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer -g)
        target_link_options(${_target} PRIVATE -fsanitize=fuzzer,address)
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS signet_forge EXPORT signet_forge-targets)
install(DIRECTORY include/signet DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT signet_forge-targets
    FILE signet_forge-config.cmake
    NAMESPACE signet::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/signet_forge
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/signet_forgeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/signet_forgeConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/signet_forge
)
