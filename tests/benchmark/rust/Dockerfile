FROM rust:1.77-bookworm AS builder

WORKDIR /app
COPY Cargo.toml .
COPY src/ src/
RUN cargo build --release

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=builder /app/target/release/neam-benchmark-rust /bedrock_agent
COPY ../prompts.jsonl /prompts.jsonl

ENV PROMPTS_FILE=/prompts.jsonl
ENTRYPOINT ["/bedrock_agent"]
