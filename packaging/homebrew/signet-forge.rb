class SignetForge < Formula
  desc "Standalone C++20 Parquet library with AI-native extensions"
  homepage "https://github.com/SIGNETSTACK/signet-forge"
  url "https://github.com/SIGNETSTACK/signet-forge/archive/refs/tags/v0.1.1.tar.gz"
  sha256 "PLACEHOLDER"  # Updated on release
  license "AGPL-3.0-or-later"
  head "https://github.com/SIGNETSTACK/signet-forge.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DSIGNET_BUILD_TESTS=OFF",
           "-DSIGNET_BUILD_BENCHMARKS=OFF",
           "-DSIGNET_BUILD_EXAMPLES=OFF",
           "-DSIGNET_BUILD_TOOLS=OFF",
           "-DSIGNET_BUILD_PYTHON=OFF",
           "-DSIGNET_BUILD_FUZZ=OFF",
           *std_cmake_args
    system "cmake", "--install", "build"
  end

  test do
    (testpath/"test.cpp").write <<~CPP
      #include "signet/forge.hpp"
      int main() {
          namespace sf = signet::forge;
          auto schema = sf::Schema::build("t", sf::Column<int32_t>{"x"});
          auto w = sf::ParquetWriter::open("test.parquet", schema);
          if (!w) return 1;
          (void)w->write_row({"42"});
          auto cs = w->close();
          return cs ? 0 : 1;
      }
    CPP
    system ENV.cxx, "-std=c++20", "test.cpp",
           "-I#{include}", "-o", "test"
    system "./test"
  end
end
