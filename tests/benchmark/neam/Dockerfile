FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*

COPY neam /usr/local/bin/neam
COPY bedrock_agent.neam /app/bedrock_agent.neam
COPY ../prompts.jsonl /app/prompts.jsonl

WORKDIR /app
ENV PROMPTS_FILE=/app/prompts.jsonl
ENTRYPOINT ["neam", "bedrock_agent.neam"]
