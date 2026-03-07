vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO SIGNETSTACK/signet-forge
    REF "v${VERSION}"
    SHA512 0  # Updated on submission to vcpkg registry
    HEAD_REF main
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        zstd    SIGNET_ENABLE_ZSTD
        lz4     SIGNET_ENABLE_LZ4
        gzip    SIGNET_ENABLE_GZIP
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSIGNET_BUILD_TESTS=OFF
        -DSIGNET_BUILD_BENCHMARKS=OFF
        -DSIGNET_BUILD_EXAMPLES=OFF
        -DSIGNET_BUILD_TOOLS=OFF
        -DSIGNET_BUILD_PYTHON=OFF
        -DSIGNET_BUILD_FUZZ=OFF
        ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/signet_forge")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
