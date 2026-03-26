# ============================================================
# Stage 1: Build
# ============================================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git \
    libssl-dev libsqlite3-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build/release \
        -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_TESTS=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_DOCS=OFF \
        -DWITH_PCH=ON \
        -DHUB32API_SHARED=OFF \
        -DHUB32_WITH_MEDIASOUP=OFF \
    && cmake --build build/release --parallel $(nproc)

# ============================================================
# Stage 2: Runtime
# ============================================================
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    libssl3 libsqlite3-0 ca-certificates openssl curl \
    && rm -rf /var/lib/apt/lists/*

# Copy binary
COPY --from=builder /src/build/release/bin/hub32api-service /usr/local/bin/hub32api-service

# Copy locales and default config
COPY --from=builder /src/locales /opt/hub32api/locales
COPY --from=builder /src/conf/default.json /opt/hub32api/conf/default.json

# Directories for runtime data
RUN mkdir -p /opt/hub32api/data /opt/hub32api/keys /opt/hub32api/conf

WORKDIR /opt/hub32api

EXPOSE 11081

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -sf http://localhost:11081/api/v1/health || exit 1

ENTRYPOINT ["/usr/local/bin/hub32api-service"]
CMD ["--console", "--config", "/opt/hub32api/conf/production.json"]
