#!/usr/bin/env bash
#
# Multi-Language Agent Benchmark — Neam v0.6.4 vs Python vs Go vs Rust
#
# Runs identical Q&A agents across 4 languages, all calling AWS Bedrock
# Claude 3.5 Sonnet, measuring runtime performance and packaging metrics.
#
# Prerequisites:
#   - AWS credentials configured (AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_REGION)
#   - neam binary built (cmake --build build-mac-release)
#   - python3 + pip (with boto3)
#   - go 1.22+
#   - cargo (rust 1.77+)
#   - docker (optional, for deployment metrics)
#
# Usage: ./run_benchmark.sh [--skip-docker] [--iterations N]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROMPTS_FILE="$SCRIPT_DIR/prompts.jsonl"
ITERATIONS="${ITERATIONS:-1}"
SKIP_DOCKER=false

# Parse arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --skip-docker) SKIP_DOCKER=true; shift ;;
    --iterations)  ITERATIONS="$2"; shift 2 ;;
    *)             echo "Unknown arg: $1"; exit 1 ;;
  esac
done

mkdir -p "$RESULTS_DIR"

# Colors for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

log()  { echo -e "${BLUE}[benchmark]${NC} $*"; }
ok()   { echo -e "${GREEN}[  OK  ]${NC} $*"; }
warn() { echo -e "${YELLOW}[ WARN ]${NC} $*"; }
fail() { echo -e "${RED}[ FAIL ]${NC} $*"; }

# ---- Validation --------------------------------------------------------------

log "Validating prerequisites..."

check_command() {
  if ! command -v "$1" &>/dev/null; then
    fail "$1 not found — skipping $2 benchmark"
    return 1
  fi
  return 0
}

HAS_NEAM=false
HAS_PYTHON=false
HAS_GO=false
HAS_RUST=false
HAS_DOCKER=false

# Check Neam binary
NEAM_BIN="$PROJECT_ROOT/build-mac-release/neam"
if [[ ! -x "$NEAM_BIN" ]]; then
  NEAM_BIN="$PROJECT_ROOT/build/neam"
fi
if [[ -x "$NEAM_BIN" ]]; then
  HAS_NEAM=true
  ok "Neam binary: $NEAM_BIN"
else
  warn "Neam binary not found — skipping Neam benchmark"
fi

check_command python3 "Python"   && HAS_PYTHON=true
check_command go      "Go"       && HAS_GO=true
check_command cargo   "Rust"     && HAS_RUST=true
check_command docker  "Docker"   && HAS_DOCKER=true

if [[ -z "${AWS_ACCESS_KEY_ID:-}" ]]; then
  fail "AWS_ACCESS_KEY_ID not set — Bedrock calls will fail"
  exit 1
fi

PROMPT_COUNT=$(wc -l < "$PROMPTS_FILE" | tr -d ' ')
log "Using $PROMPT_COUNT prompts from $PROMPTS_FILE"
log "Iterations: $ITERATIONS"

# ---- Helper functions --------------------------------------------------------

# Run a command and capture timing + memory via /usr/bin/time
# Returns: JSON with wall_time_ms, user_time_ms, sys_time_ms, peak_rss_bytes
run_timed() {
  local label="$1"
  shift
  local time_output
  time_output=$(mktemp)
  local stdout_file
  stdout_file=$(mktemp)

  log "Running $label..."

  # macOS /usr/bin/time -l uses a different format than GNU time
  if [[ "$(uname)" == "Darwin" ]]; then
    /usr/bin/time -l "$@" > "$stdout_file" 2> "$time_output" || true
    local wall_time
    wall_time=$(awk '/real/{print $1}' "$time_output" 2>/dev/null || echo "0")
    local user_time
    user_time=$(awk '/user/{print $1}' "$time_output" 2>/dev/null || echo "0")
    local sys_time
    sys_time=$(awk '/sys/{print $1}' "$time_output" 2>/dev/null || echo "0")
    local peak_rss
    peak_rss=$(awk '/maximum resident set size/{print $1}' "$time_output" 2>/dev/null || echo "0")
  else
    /usr/bin/time -v "$@" > "$stdout_file" 2> "$time_output" || true
    local wall_time
    wall_time=$(awk -F: '/wall clock/{print $NF}' "$time_output" 2>/dev/null | tr -d ' ' || echo "0")
    local user_time
    user_time=$(awk -F: '/User time/{print $NF}' "$time_output" 2>/dev/null | tr -d ' ' || echo "0")
    local sys_time
    sys_time=$(awk -F: '/System time/{print $NF}' "$time_output" 2>/dev/null | tr -d ' ' || echo "0")
    local peak_rss
    peak_rss=$(awk -F: '/Maximum resident/{print $NF}' "$time_output" 2>/dev/null | tr -d ' ' || echo "0")
  fi

  cat "$stdout_file"
  echo "---TIME_METADATA---"
  echo "{\"wall_time\": \"$wall_time\", \"user_time\": \"$user_time\", \"sys_time\": \"$sys_time\", \"peak_rss\": \"$peak_rss\"}"

  rm -f "$time_output" "$stdout_file"
}

# Extract agent JSON output (everything before ---TIME_METADATA---)
extract_agent_output() {
  sed '/^---TIME_METADATA---$/,$d'
}

# Extract time metadata (line after ---TIME_METADATA---)
extract_time_metadata() {
  sed -n '/^---TIME_METADATA---$/,$ p' | tail -1
}

# Measure binary/artifact size
artifact_size() {
  local path="$1"
  if [[ -e "$path" ]]; then
    local bytes
    bytes=$(stat -f%z "$path" 2>/dev/null || stat -c%s "$path" 2>/dev/null || echo "0")
    echo "$bytes"
  else
    echo "0"
  fi
}

# ---- Build Phase -------------------------------------------------------------

log ""
log "========================================="
log "  Phase 1: Build / Compile"
log "========================================="

declare -A BUILD_TIME_MS
declare -A ARTIFACT_SIZE

# Build Neam (already built, just measure)
if $HAS_NEAM; then
  ARTIFACT_SIZE[neam]=$(artifact_size "$NEAM_BIN")
  BUILD_TIME_MS[neam]=0  # Already built
  ok "Neam artifact: $(numfmt --to=iec ${ARTIFACT_SIZE[neam]} 2>/dev/null || echo "${ARTIFACT_SIZE[neam]} bytes")"
fi

# Build Python (no compile step, measure venv size)
if $HAS_PYTHON; then
  PYTHON_VENV="$SCRIPT_DIR/python/.venv"
  BUILD_START=$(date +%s%N)
  if [[ ! -d "$PYTHON_VENV" ]]; then
    python3 -m venv "$PYTHON_VENV"
    "$PYTHON_VENV/bin/pip" install -q -r "$SCRIPT_DIR/python/requirements.txt"
  fi
  BUILD_END=$(date +%s%N)
  BUILD_TIME_MS[python]=$(( (BUILD_END - BUILD_START) / 1000000 ))
  ARTIFACT_SIZE[python]=$(du -sb "$PYTHON_VENV" 2>/dev/null | awk '{print $1}' || echo "0")
  ok "Python venv: $(numfmt --to=iec ${ARTIFACT_SIZE[python]} 2>/dev/null || echo "${ARTIFACT_SIZE[python]} bytes")"
fi

# Build Go
if $HAS_GO; then
  GO_BIN="$SCRIPT_DIR/go/bedrock_agent"
  BUILD_START=$(date +%s%N)
  (cd "$SCRIPT_DIR/go" && go build -ldflags="-s -w" -o bedrock_agent .) 2>&1 || warn "Go build failed"
  BUILD_END=$(date +%s%N)
  BUILD_TIME_MS[go]=$(( (BUILD_END - BUILD_START) / 1000000 ))
  ARTIFACT_SIZE[go]=$(artifact_size "$GO_BIN")
  ok "Go binary: $(numfmt --to=iec ${ARTIFACT_SIZE[go]} 2>/dev/null || echo "${ARTIFACT_SIZE[go]} bytes")"
fi

# Build Rust
if $HAS_RUST; then
  RUST_BIN="$SCRIPT_DIR/rust/target/release/neam-benchmark-rust"
  BUILD_START=$(date +%s%N)
  (cd "$SCRIPT_DIR/rust" && cargo build --release 2>&1) || warn "Rust build failed"
  BUILD_END=$(date +%s%N)
  BUILD_TIME_MS[rust]=$(( (BUILD_END - BUILD_START) / 1000000 ))
  ARTIFACT_SIZE[rust]=$(artifact_size "$RUST_BIN")
  ok "Rust binary: $(numfmt --to=iec ${ARTIFACT_SIZE[rust]} 2>/dev/null || echo "${ARTIFACT_SIZE[rust]} bytes")"
fi

# ---- Dependency Count --------------------------------------------------------

log ""
log "Counting dependencies..."

declare -A DEP_COUNT

if $HAS_NEAM; then
  DEP_COUNT[neam]=0  # Neam has zero runtime deps (statically linked)
fi
if $HAS_PYTHON; then
  DEP_COUNT[python]=$("$PYTHON_VENV/bin/pip" list 2>/dev/null | tail -n +3 | wc -l | tr -d ' ')
fi
if $HAS_GO; then
  DEP_COUNT[go]=$(cd "$SCRIPT_DIR/go" && go list -m all 2>/dev/null | wc -l | tr -d ' ')
fi
if $HAS_RUST; then
  DEP_COUNT[rust]=$(cd "$SCRIPT_DIR/rust" && cargo tree --depth 1 2>/dev/null | wc -l | tr -d ' ')
fi

# ---- Runtime Phase -----------------------------------------------------------

log ""
log "========================================="
log "  Phase 2: Runtime Benchmarks"
log "========================================="

declare -A AGENT_OUTPUT

# Run Neam agent
if $HAS_NEAM; then
  FULL_OUTPUT=$(run_timed "Neam agent" env PROMPTS_FILE="$PROMPTS_FILE" "$NEAM_BIN" "$SCRIPT_DIR/neam/bedrock_agent.neam" 2>&1 || true)
  AGENT_OUTPUT[neam]=$(echo "$FULL_OUTPUT" | extract_agent_output)
  TIME_META=$(echo "$FULL_OUTPUT" | extract_time_metadata)
  echo "${AGENT_OUTPUT[neam]}" > "$RESULTS_DIR/neam_raw.json"
  ok "Neam agent completed"
fi

# Run Python agent
if $HAS_PYTHON; then
  FULL_OUTPUT=$(run_timed "Python agent" env PROMPTS_FILE="$PROMPTS_FILE" "$PYTHON_VENV/bin/python" "$SCRIPT_DIR/python/bedrock_agent.py" 2>&1 || true)
  AGENT_OUTPUT[python]=$(echo "$FULL_OUTPUT" | extract_agent_output)
  echo "${AGENT_OUTPUT[python]}" > "$RESULTS_DIR/python_raw.json"
  ok "Python agent completed"
fi

# Run Go agent
if $HAS_GO; then
  FULL_OUTPUT=$(run_timed "Go agent" env PROMPTS_FILE="$PROMPTS_FILE" "$SCRIPT_DIR/go/bedrock_agent" 2>&1 || true)
  AGENT_OUTPUT[go]=$(echo "$FULL_OUTPUT" | extract_agent_output)
  echo "${AGENT_OUTPUT[go]}" > "$RESULTS_DIR/go_raw.json"
  ok "Go agent completed"
fi

# Run Rust agent
if $HAS_RUST; then
  FULL_OUTPUT=$(run_timed "Rust agent" env PROMPTS_FILE="$PROMPTS_FILE" "$SCRIPT_DIR/rust/target/release/neam-benchmark-rust" 2>&1 || true)
  AGENT_OUTPUT[rust]=$(echo "$FULL_OUTPUT" | extract_agent_output)
  echo "${AGENT_OUTPUT[rust]}" > "$RESULTS_DIR/rust_raw.json"
  ok "Rust agent completed"
fi

# ---- Docker Phase (optional) -------------------------------------------------

declare -A DOCKER_IMAGE_SIZE
declare -A DOCKER_BUILD_TIME

if ! $SKIP_DOCKER && $HAS_DOCKER; then
  log ""
  log "========================================="
  log "  Phase 3: Docker / Deployment Metrics"
  log "========================================="

  build_docker() {
    local name="$1"
    local context="$2"
    local dockerfile="$3"

    log "Building Docker image: benchmark-$name"
    BUILD_START=$(date +%s%N)
    docker build -t "benchmark-$name" -f "$dockerfile" "$context" 2>&1 || { warn "Docker build failed for $name"; return; }
    BUILD_END=$(date +%s%N)
    DOCKER_BUILD_TIME[$name]=$(( (BUILD_END - BUILD_START) / 1000000 ))
    DOCKER_IMAGE_SIZE[$name]=$(docker images "benchmark-$name" --format '{{.Size}}' 2>/dev/null || echo "0")
    ok "Docker benchmark-$name: ${DOCKER_IMAGE_SIZE[$name]}"
  }

  $HAS_PYTHON && build_docker "python" "$SCRIPT_DIR" "$SCRIPT_DIR/python/Dockerfile"
  $HAS_GO     && build_docker "go"     "$SCRIPT_DIR" "$SCRIPT_DIR/go/Dockerfile"
  $HAS_RUST   && build_docker "rust"   "$SCRIPT_DIR" "$SCRIPT_DIR/rust/Dockerfile"
  $HAS_NEAM   && build_docker "neam"   "$SCRIPT_DIR" "$SCRIPT_DIR/neam/Dockerfile"
else
  log ""
  log "Skipping Docker phase (--skip-docker or docker not available)"
fi

# ---- Generate Report ---------------------------------------------------------

log ""
log "========================================="
log "  Generating Report"
log "========================================="

# Build JSON report
REPORT="$RESULTS_DIR/report.json"

python3 -c "
import json, os, sys

results = {}
results_dir = '$RESULTS_DIR'

for lang in ['neam', 'python', 'go', 'rust']:
    raw_file = os.path.join(results_dir, f'{lang}_raw.json')
    if os.path.exists(raw_file):
        try:
            with open(raw_file) as f:
                content = f.read().strip()
                if content:
                    results[lang] = json.loads(content)
        except (json.JSONDecodeError, IOError) as e:
            print(f'Warning: Could not parse {raw_file}: {e}', file=sys.stderr)

# Packaging metrics from env
build_times = {
    'neam':   int('${BUILD_TIME_MS[neam]:-0}'),
    'python': int('${BUILD_TIME_MS[python]:-0}'),
    'go':     int('${BUILD_TIME_MS[go]:-0}'),
    'rust':   int('${BUILD_TIME_MS[rust]:-0}'),
}
artifact_sizes = {
    'neam':   int('${ARTIFACT_SIZE[neam]:-0}'),
    'python': int('${ARTIFACT_SIZE[python]:-0}'),
    'go':     int('${ARTIFACT_SIZE[go]:-0}'),
    'rust':   int('${ARTIFACT_SIZE[rust]:-0}'),
}
dep_counts = {
    'neam':   int('${DEP_COUNT[neam]:-0}'),
    'python': int('${DEP_COUNT[python]:-0}'),
    'go':     int('${DEP_COUNT[go]:-0}'),
    'rust':   int('${DEP_COUNT[rust]:-0}'),
}

report = {
    'benchmark': 'Multi-Language Agent Benchmark',
    'model': 'anthropic.claude-3-5-sonnet-20241022-v2:0',
    'prompt_count': int('$PROMPT_COUNT'),
    'iterations': int('$ITERATIONS'),
    'runtime_metrics': {},
    'packaging_metrics': {},
    'relative_performance': {},
}

for lang, data in results.items():
    report['runtime_metrics'][lang] = {
        'total_requests': data.get('total_requests', 0),
        'total_time_ms': data.get('total_time_ms', 0),
        'avg_latency_ms': data.get('avg_latency_ms', 0),
        'p50_latency_ms': data.get('p50_latency_ms', 0),
        'p95_latency_ms': data.get('p95_latency_ms', 0),
        'p99_latency_ms': data.get('p99_latency_ms', 0),
        'peak_memory_bytes': data.get('peak_memory_bytes', 0),
    }
    report['packaging_metrics'][lang] = {
        'build_time_ms': build_times.get(lang, 0),
        'artifact_size_bytes': artifact_sizes.get(lang, 0),
        'dependency_count': dep_counts.get(lang, 0),
    }

# Compute relative performance (Neam as baseline = 1.0x)
if 'neam' in report['runtime_metrics']:
    neam_avg = report['runtime_metrics']['neam'].get('avg_latency_ms', 1)
    neam_mem = report['runtime_metrics']['neam'].get('peak_memory_bytes', 1)
    neam_art = artifact_sizes.get('neam', 1)
    if neam_avg <= 0: neam_avg = 1
    if neam_mem <= 0: neam_mem = 1
    if neam_art <= 0: neam_art = 1

    for lang in results:
        metrics = report['runtime_metrics'][lang]
        report['relative_performance'][lang] = {
            'latency_vs_neam': round(metrics.get('avg_latency_ms', 0) / neam_avg, 2),
            'memory_vs_neam': round(metrics.get('peak_memory_bytes', 0) / neam_mem, 2),
            'artifact_size_vs_neam': round(artifact_sizes.get(lang, 0) / neam_art, 2),
        }

with open('$REPORT', 'w') as f:
    json.dump(report, f, indent=2)

print(json.dumps(report, indent=2))
" 2>&1 || warn "Report generation encountered issues"

ok "Report saved to $REPORT"

# ---- Print Summary Table -----------------------------------------------------

log ""
echo -e "${BOLD}${CYAN}"
echo "╔══════════════════════════════════════════════════════════════════════════╗"
echo "║           Multi-Language Agent Benchmark — Summary                      ║"
echo "╠══════════════════════════════════════════════════════════════════════════╣"
echo -e "║  Model: anthropic.claude-3-5-sonnet-20241022-v2:0                      ║"
echo -e "║  Prompts: $PROMPT_COUNT  │  Iterations: $ITERATIONS                                          ║"
echo "╠══════════════╦═══════════╦═══════════╦═══════════╦═══════════╦══════════╣"
echo "║ Metric       ║   Neam    ║  Python   ║    Go     ║   Rust    ║  Unit    ║"
echo "╠══════════════╬═══════════╬═══════════╬═══════════╬═══════════╬══════════╣"
echo -e "${NC}"

print_row() {
  local metric="$1"
  local unit="$2"
  local neam_val="${3:---}"
  local py_val="${4:---}"
  local go_val="${5:---}"
  local rs_val="${6:---}"
  printf "║ %-12s ║ %9s ║ %9s ║ %9s ║ %9s ║ %8s ║\n" "$metric" "$neam_val" "$py_val" "$go_val" "$rs_val" "$unit"
}

# Extract values from raw JSON files
get_json_val() {
  local file="$1"
  local key="$2"
  if [[ -f "$file" ]]; then
    python3 -c "import json; d=json.load(open('$file')); print(d.get('$key', '--'))" 2>/dev/null || echo "--"
  else
    echo "--"
  fi
}

N_AVG=$(get_json_val "$RESULTS_DIR/neam_raw.json" "avg_latency_ms")
P_AVG=$(get_json_val "$RESULTS_DIR/python_raw.json" "avg_latency_ms")
G_AVG=$(get_json_val "$RESULTS_DIR/go_raw.json" "avg_latency_ms")
R_AVG=$(get_json_val "$RESULTS_DIR/rust_raw.json" "avg_latency_ms")

N_P50=$(get_json_val "$RESULTS_DIR/neam_raw.json" "p50_latency_ms")
P_P50=$(get_json_val "$RESULTS_DIR/python_raw.json" "p50_latency_ms")
G_P50=$(get_json_val "$RESULTS_DIR/go_raw.json" "p50_latency_ms")
R_P50=$(get_json_val "$RESULTS_DIR/rust_raw.json" "p50_latency_ms")

N_P95=$(get_json_val "$RESULTS_DIR/neam_raw.json" "p95_latency_ms")
P_P95=$(get_json_val "$RESULTS_DIR/python_raw.json" "p95_latency_ms")
G_P95=$(get_json_val "$RESULTS_DIR/go_raw.json" "p95_latency_ms")
R_P95=$(get_json_val "$RESULTS_DIR/rust_raw.json" "p95_latency_ms")

N_MEM=$(get_json_val "$RESULTS_DIR/neam_raw.json" "peak_memory_bytes")
P_MEM=$(get_json_val "$RESULTS_DIR/python_raw.json" "peak_memory_bytes")
G_MEM=$(get_json_val "$RESULTS_DIR/go_raw.json" "peak_memory_bytes")
R_MEM=$(get_json_val "$RESULTS_DIR/rust_raw.json" "peak_memory_bytes")

print_row "Avg Latency"  "ms"    "$N_AVG" "$P_AVG" "$G_AVG" "$R_AVG"
print_row "P50 Latency"  "ms"    "$N_P50" "$P_P50" "$G_P50" "$R_P50"
print_row "P95 Latency"  "ms"    "$N_P95" "$P_P95" "$G_P95" "$R_P95"
print_row "Peak Memory"  "bytes" "$N_MEM" "$P_MEM" "$G_MEM" "$R_MEM"
print_row "Artifact"     "bytes" "${ARTIFACT_SIZE[neam]:-0}" "${ARTIFACT_SIZE[python]:-0}" "${ARTIFACT_SIZE[go]:-0}" "${ARTIFACT_SIZE[rust]:-0}"
print_row "Build Time"   "ms"    "${BUILD_TIME_MS[neam]:-0}" "${BUILD_TIME_MS[python]:-0}" "${BUILD_TIME_MS[go]:-0}" "${BUILD_TIME_MS[rust]:-0}"
print_row "Dependencies" "count" "${DEP_COUNT[neam]:-0}" "${DEP_COUNT[python]:-0}" "${DEP_COUNT[go]:-0}" "${DEP_COUNT[rust]:-0}"

echo "╚══════════════╩═══════════╩═══════════╩═══════════╩═══════════╩══════════╝"
echo ""

log "Full report: $REPORT"
log "Raw outputs: $RESULTS_DIR/*_raw.json"
echo ""
ok "Benchmark complete!"
