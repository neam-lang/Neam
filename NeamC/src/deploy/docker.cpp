#include "neamc/deploy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace neamc
{
void generate_dockerfile(const DeployConfig& config)
{
  namespace fs = std::filesystem;
  fs::create_directories("deploy/docker");

  std::ofstream file("deploy/docker/Dockerfile");
  file << R"(# Build stage
FROM neam/compiler:latest AS builder

WORKDIR /app
COPY . .

RUN neamc build --release

# Runtime stage
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/target/release/*.bundle /app/
COPY --from=builder /app/prompts /app/prompts
COPY --from=builder /app/data /app/data

ENV NEAM_ENV=production
ENV NEAM_LOG_LEVEL=info

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=3s \
    CMD curl -f http://localhost:8080/health || exit 1

CMD ["neam-runtime", "/app/agent.bundle"]
)";

  std::ofstream compose("deploy/docker/docker-compose.yml");
  compose << R"(version: '3.8'

services:
  agent:
    build:
      context: ../..
      dockerfile: deploy/docker/Dockerfile
    ports:
      - "8080:8080"
    environment:
      - NEAM_ENV=development
      - ANTHROPIC_API_KEY=${ANTHROPIC_API_KEY}
    volumes:
      - ./data:/app/data
    restart: unless-stopped

  jaeger:
    image: jaegertracing/all-in-one:latest
    ports:
      - "16686:16686"
      - "4317:4317"
)";

  std::cout << "Generated deploy/docker/Dockerfile\n";
  std::cout << "Generated deploy/docker/docker-compose.yml\n";
  (void)config;
}
}  // namespace neamc
