FROM golang:1.22-bookworm AS builder

WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download
COPY main.go .
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o /bedrock_agent .

FROM scratch
COPY --from=builder /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/
COPY --from=builder /bedrock_agent /bedrock_agent
COPY ../prompts.jsonl /prompts.jsonl

ENV PROMPTS_FILE=/prompts.jsonl
ENTRYPOINT ["/bedrock_agent"]
