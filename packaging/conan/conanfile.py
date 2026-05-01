from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy, get
import os


class SignetForgeConan(ConanFile):
    name = "signet_forge"
    version = "0.1.1"
    license = "AGPL-3.0-or-later"
    author = "SIGNETSTACK"
    url = "https://github.com/SIGNETSTACK/signet-forge"
    homepage = "https://github.com/SIGNETSTACK/signet-forge"
    description = "Standalone C++20 Parquet library with AI-native extensions"
    topics = ("parquet", "columnar", "header-only", "ai", "c++20")

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "zstd": [True, False],
        "lz4": [True, False],
        "gzip": [True, False],
        "ai_audit": [True, False],
    }
    default_options = {
        "zstd": False,
        "lz4": False,
        "gzip": False,
        "ai_audit": False,
    }

    exports_sources = (
        "include/*",
        "CMakeLists.txt",
        "CMakePresets.json",
        "cmake/*",
        "LICENSE",
    )
    no_copy_source = True

    def requirements(self):
        if self.options.zstd:
            self.requires("zstd/1.5.6")
        if self.options.lz4:
            self.requires("lz4/1.10.0")
        if self.options.gzip:
            self.requires("zlib/1.3.1")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["SIGNET_BUILD_TESTS"] = False
        tc.variables["SIGNET_BUILD_BENCHMARKS"] = False
        tc.variables["SIGNET_BUILD_EXAMPLES"] = False
        tc.variables["SIGNET_BUILD_TOOLS"] = False
        tc.variables["SIGNET_BUILD_PYTHON"] = False
        tc.variables["SIGNET_BUILD_FUZZ"] = False
        tc.variables["SIGNET_ENABLE_ZSTD"] = bool(self.options.zstd)
        tc.variables["SIGNET_ENABLE_LZ4"] = bool(self.options.lz4)
        tc.variables["SIGNET_ENABLE_GZIP"] = bool(self.options.gzip)
        tc.variables["SIGNET_ENABLE_COMMERCIAL"] = bool(self.options.ai_audit)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))

    def package_id(self):
        self.info.clear()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "signet_forge")
        self.cpp_info.set_property("cmake_target_name", "signet::forge")
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        if self.options.zstd:
            self.cpp_info.defines.append("SIGNET_ENABLE_ZSTD")
        if self.options.lz4:
            self.cpp_info.defines.append("SIGNET_ENABLE_LZ4")
        if self.options.gzip:
            self.cpp_info.defines.append("SIGNET_ENABLE_GZIP")
        if self.options.ai_audit:
            self.cpp_info.defines.append("SIGNET_ENABLE_COMMERCIAL")
