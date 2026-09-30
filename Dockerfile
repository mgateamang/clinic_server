# Multi-stage Dockerfile for AIDANS Clinic Server
# Stage 1: Build binary using Meson and Ninja on Alpine Linux
FROM alpine:3.20 AS builder

RUN apk add --no-cache \
    g++ \
    meson \
    samurai \
    sqlite-dev \
    nlohmann-json \
    pkgconf

WORKDIR /app

# Copy build definition and source files
COPY meson.build ./
COPY include/ ./include/
COPY src/ ./src/
COPY tests/ ./tests/

# Configure, compile, and run test suite during container build
RUN meson setup build --buildtype=release && \
    meson compile -C build && \
    meson test -C build --verbose

# Stage 2: Minimal, secure runtime container
FROM alpine:3.20

RUN apk add --no-cache \
    libstdc++ \
    sqlite-libs \
    curl

# Create non-root system user
RUN addgroup -S aidans && adduser -S aidans -G aidans

WORKDIR /app

# Copy binary from builder
COPY --from=builder /app/build/clinic_server /usr/local/bin/clinic_server

# Create data directory for SQLite database persistence
RUN mkdir -p /data && chown -R aidans:aidans /data /app

USER aidans

EXPOSE 8080

VOLUME ["/data"]

ENTRYPOINT ["clinic_server"]
CMD ["--host", "0.0.0.0", "--port", "8080", "--db", "/data/aidans.db", "--redis-host", "127.0.0.1", "--redis-port", "6379"]
