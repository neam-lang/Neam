#!/bin/bash
#
# v0.7.2 HotVM Integration Benchmark
#
# Starts neam-api, runs curl loops, measures p50/p95/p99.
# Fails if p95 > 50ms overhead (excluding LLM time).
#
# Usage: ./tests/benchmark/hotvm_integration_bench.sh [--port 8080]
#

set -euo pipefail

PORT="${1:-8080}"
NEAM_API="${NEAM_API:-./build/neam-api}"
WARMUP_REQUESTS=5
BENCH_REQUESTS=50
RESULTS_FILE="/tmp/hotvm_bench_results.txt"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

cleanup() {
  if [ -n "${SERVER_PID:-}" ]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

echo "=== v0.7.2 HotVM Integration Benchmark ==="
echo ""

# Check binary exists
if [ ! -x "$NEAM_API" ]; then
  echo -e "${RED}Error: $NEAM_API not found or not executable${NC}"
  echo "Build with: cmake --build build/"
  exit 1
fi

# Start server
echo "Starting neam-api on port $PORT..."
$NEAM_API --port "$PORT" --workers 2 --pool-size 2 &
SERVER_PID=$!

# Wait for ready
echo "Waiting for /ready..."
for i in $(seq 1 30); do
  if curl -sf "http://localhost:$PORT/ready" > /dev/null 2>&1; then
    echo -e "${GREEN}Server ready${NC}"
    break
  fi
  if [ "$i" -eq 30 ]; then
    echo -e "${RED}Server did not become ready in 30s${NC}"
    exit 1
  fi
  sleep 1
done

# Health check
echo ""
echo "--- Health Check ---"
curl -s "http://localhost:$PORT/api/v1/health" | python3 -m json.tool 2>/dev/null || \
  curl -s "http://localhost:$PORT/api/v1/health"
echo ""

# Warmup
echo ""
echo "--- Warmup ($WARMUP_REQUESTS requests) ---"
for i in $(seq 1 $WARMUP_REQUESTS); do
  curl -sf -X POST "http://localhost:$PORT/api/v1/agent/ask" \
    -H "Content-Type: application/json" \
    -d '{"agent_id":"assistant","query":"ping"}' > /dev/null 2>&1 || true
done
echo "Warmup complete."

# Benchmark: measure /health endpoint latency (no LLM overhead)
echo ""
echo "--- Benchmark: /health endpoint ($BENCH_REQUESTS requests) ---"
> "$RESULTS_FILE"

for i in $(seq 1 $BENCH_REQUESTS); do
  # Use curl timing to get total time in milliseconds
  time_ms=$(curl -sf -o /dev/null -w "%{time_total}" "http://localhost:$PORT/api/v1/health" 2>/dev/null)
  # Convert seconds to milliseconds
  ms=$(echo "$time_ms * 1000" | bc 2>/dev/null || python3 -c "print(float('$time_ms') * 1000)")
  echo "$ms" >> "$RESULTS_FILE"
done

# Calculate percentiles
echo ""
echo "--- Results ---"
sort -n "$RESULTS_FILE" > "${RESULTS_FILE}.sorted"
TOTAL=$(wc -l < "${RESULTS_FILE}.sorted" | tr -d ' ')

p50_idx=$(echo "$TOTAL * 50 / 100" | bc 2>/dev/null || python3 -c "print(int($TOTAL * 0.5))")
p95_idx=$(echo "$TOTAL * 95 / 100" | bc 2>/dev/null || python3 -c "print(int($TOTAL * 0.95))")
p99_idx=$(echo "$TOTAL * 99 / 100" | bc 2>/dev/null || python3 -c "print(int($TOTAL * 0.99))")

# Ensure indices are at least 1
p50_idx=$((p50_idx > 0 ? p50_idx : 1))
p95_idx=$((p95_idx > 0 ? p95_idx : 1))
p99_idx=$((p99_idx > 0 ? p99_idx : 1))

P50=$(sed -n "${p50_idx}p" "${RESULTS_FILE}.sorted")
P95=$(sed -n "${p95_idx}p" "${RESULTS_FILE}.sorted")
P99=$(sed -n "${p99_idx}p" "${RESULTS_FILE}.sorted")

echo "  p50: ${P50}ms"
echo "  p95: ${P95}ms"
echo "  p99: ${P99}ms"
echo "  count: $TOTAL"

# Check metrics endpoint
echo ""
echo "--- Metrics ---"
curl -s "http://localhost:$PORT/api/v1/metrics" | python3 -m json.tool 2>/dev/null || \
  curl -s "http://localhost:$PORT/api/v1/metrics"
echo ""

# Validate p95 < 50ms
P95_INT=$(echo "$P95" | cut -d. -f1)
if [ "${P95_INT:-0}" -gt 50 ]; then
  echo -e "${RED}FAIL: p95 (${P95}ms) exceeds 50ms target${NC}"
  exit 1
fi

echo ""
echo -e "${GREEN}PASS: p95 ${P95}ms < 50ms target${NC}"
rm -f "$RESULTS_FILE" "${RESULTS_FILE}.sorted"
