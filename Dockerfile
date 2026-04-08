FROM ubuntu:24.04

# ── APT reliability hardening ─────────────────────────────────────────────────
RUN echo 'Acquire::Retries "5";'                  >  /etc/apt/apt.conf.d/99retry \
    && echo 'Acquire::http::Timeout "120";'       >> /etc/apt/apt.conf.d/99retry \
    && echo 'Acquire::https::Timeout "120";'      >> /etc/apt/apt.conf.d/99retry \
    && echo 'Acquire::http::Pipeline-Depth "0";'  >> /etc/apt/apt.conf.d/99retry \
    && echo 'Acquire::http::No-Cache "true";'     >> /etc/apt/apt.conf.d/99retry

# ── System dependencies ───────────────────────────────────────────────────────
RUN apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get \
        -o Acquire::AllowInsecureRepositories=true \
        -o Acquire::AllowDowngradeToInsecureRepositories=true \
        update \
    && apt-get install -y \
        --no-install-recommends \
        --allow-unauthenticated \
        ca-certificates \
        git \
        clang-18 \
        libclang-rt-18-dev \
        gcc \
        cmake \
        ninja-build \
        libzstd-dev \
        liblz4-dev \
        zlib1g-dev \
        wget \
        perl \
        make \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /usr/bin/clang-18    /usr/local/bin/clang \
    && ln -sf /usr/bin/clang++-18  /usr/local/bin/clang++

# ── OpenSSL 3.4.1 — build from source WITH FIPS module ────────────────────────
RUN wget -q https://www.openssl.org/source/openssl-3.4.1.tar.gz -O /tmp/openssl.tar.gz \
    && tar xzf /tmp/openssl.tar.gz -C /tmp \
    && cd /tmp/openssl-3.4.1 \
    && ./Configure \
        --prefix=/usr/local \
        --openssldir=/usr/local/ssl \
        linux-x86_64 \
        shared \
        enable-fips \
        no-docs \
        no-apps \
    && make -j"$(nproc)" \
    && make install_sw \
    && make install_fips \
    && ldconfig \
    && mkdir -p /usr/local/ssl \
    && printf '%s\n' \
        'openssl_conf = openssl_init' \
        '' \
        '.include /usr/local/ssl/fipsmodule.cnf' \
        '' \
        '[openssl_init]' \
        'providers = provider_sect' \
        '' \
        '[provider_sect]' \
        'default = default_sect' \
        'fips = fips_sect' \
        '' \
        '[default_sect]' \
        'activate = 1' \
        > /usr/local/ssl/openssl.cnf \
    && rm -rf /tmp/openssl-3.4.1 /tmp/openssl.tar.gz

# ── liboqs 0.15.0 — post-quantum crypto (ML-KEM-768 + ML-DSA-65) ─────────────
RUN git clone --depth 1 --branch 0.15.0 \
        https://github.com/open-quantum-safe/liboqs.git /tmp/liboqs \
    && cmake -S /tmp/liboqs -B /tmp/liboqs-build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DBUILD_SHARED_LIBS=OFF \
        -DOQS_BUILD_ONLY_LIB=ON \
        -DOQS_USE_OPENSSL=ON \
        -DOPENSSL_ROOT_DIR=/usr/local \
    && cmake --build /tmp/liboqs-build --parallel \
    && cmake --install /tmp/liboqs-build --prefix /usr/local \
    && rm -rf /tmp/liboqs /tmp/liboqs-build

# ── Copy ALL source ──────────────────────────────────────────────────────────
WORKDIR /src
COPY include/ include/
COPY tests/ tests/
COPY benchmarks/ benchmarks/
COPY Benchmarking_Protocols/ Benchmarking_Protocols/
COPY python/ python/
COPY examples/ examples/
COPY CMakeLists.txt CMakePresets.json ./
COPY LICENSE LICENSE_COMMERCIAL ./

ENV CC=clang
ENV CXX=clang++

# ── Single unified build: Release, ALL features, tests + benchmarks ───────────
# One build with everything enabled — commercial, PQ, all codecs, tests, benchmarks.
# ASan/LSan run separately via ctest environment variables at runtime.
RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DSIGNET_BUILD_TESTS=ON \
        -DSIGNET_BUILD_BENCHMARKS=ON \
        -DSIGNET_ENABLE_COMMERCIAL=ON \
        -DSIGNET_ENABLE_PQ=ON \
        -DSIGNET_ENABLE_ZSTD=ON \
        -DSIGNET_ENABLE_LZ4=ON \
        -DSIGNET_ENABLE_GZIP=ON \
    && cmake --build build --parallel

# ── Default: run tests + benchmarks ──────────────────────────────────────────
CMD ["sh", "-c", "\
echo '=== Signet Forge — Ubuntu Docker CI ===' && \
echo '=== OpenSSL 3.4.1 FIPS + liboqs 0.15.0 + Clang 18 ===' && \
echo '' && \
echo '=== Release tests (830 expected) ===' && \
cd /src/build && ctest --output-on-failure && \
echo '' && \
echo '=== Benchmarks (50 samples) ===' && \
./signet_benchmarks '[bench]' --benchmark-samples 50 && \
echo '' && \
echo 'ALL TESTS AND BENCHMARKS PASSED' \
"]
