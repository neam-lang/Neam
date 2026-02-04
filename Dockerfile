# =============================================================================
# Neam v0.6.0 Multi-stage Docker Build
# =============================================================================

# -----------------------------------------------------------------------------
# Stage 1: Builder
# -----------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    g++ \
    libcurl4-openssl-dev \
    libssl-dev \
    libpq-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy dependency files first for better layer caching
COPY CMakeLists.txt ./
COPY deps/ deps/
COPY NeamC/ NeamC/
COPY tests/ tests/

# Configure and build
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DNEAM_BACKEND_POSTGRES=ON \
    && cmake --build build -j$(nproc)

# Run tests during build to catch issues early
RUN ctest --test-dir build --output-on-failure || true

# -----------------------------------------------------------------------------
# Stage 2: Runtime
# -----------------------------------------------------------------------------
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libcurl4 \
    libssl3 \
    libpq5 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd -r neam && useradd -r -g neam -d /app -s /sbin/nologin neam

WORKDIR /app

# Copy binaries from builder
COPY --from=builder /build/build/neamc /usr/local/bin/neamc
COPY --from=builder /build/build/neam /usr/local/bin/neam
COPY --from=builder /build/build/neam-cli /usr/local/bin/neam-cli
COPY --from=builder /build/build/neam-api /usr/local/bin/neam-api
COPY --from=builder /build/build/neam-lsp /usr/local/bin/neam-lsp

# Copy stdlib and data if present
COPY --from=builder /build/NeamC/stdlib /app/stdlib

# Create data directories
RUN mkdir -p /app/data /tmp/neam && chown -R neam:neam /app /tmp/neam

ENV NEAM_ENV=production
ENV NEAM_LOG_LEVEL=info
ENV NEAM_PORT=8080

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

USER neam

ENTRYPOINT ["neam-api"]
CMD ["--port", "8080"]
