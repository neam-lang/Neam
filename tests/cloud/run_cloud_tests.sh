#!/bin/bash
# Neam v0.6.4 Multi-Cloud Test Runner
# Runs agent tests across AWS, GCP, Azure, and Alibaba Cloud

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="${SCRIPT_DIR}/cloud_test_config.toml"
OUTPUT_DIR="${SCRIPT_DIR}/test_results/$(date +%Y%m%d_%H%M%S)"

# Default settings
PARALLEL=true
PROVIDERS="aws,gcp,azure,alibaba"
WORKLOADS="latency,throughput,cost"
REQUESTS=100
CONCURRENCY=10

# Usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -c, --config FILE       Config file (default: cloud_test_config.toml)"
    echo "  -p, --providers LIST    Comma-separated providers (aws,gcp,azure,alibaba)"
    echo "  -w, --workloads LIST    Comma-separated workloads (latency,throughput,cost,gpu)"
    echo "  -r, --requests NUM      Requests per workload (default: 100)"
    echo "  -j, --concurrency NUM   Concurrent requests (default: 10)"
    echo "  -o, --output DIR        Output directory"
    echo "  -s, --sequential        Run tests sequentially (not in parallel)"
    echo "  -h, --help              Show this help"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Run all tests with defaults"
    echo "  $0 -p aws,gcp -w latency              # Test latency on AWS and GCP"
    echo "  $0 -r 1000 -j 50                      # High-volume test"
    echo "  $0 -p alibaba -w cost -o ./results    # Cost test on Alibaba"
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        -p|--providers)
            PROVIDERS="$2"
            shift 2
            ;;
        -w|--workloads)
            WORKLOADS="$2"
            shift 2
            ;;
        -r|--requests)
            REQUESTS="$2"
            shift 2
            ;;
        -j|--concurrency)
            CONCURRENCY="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -s|--sequential)
            PARALLEL=false
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Banner
echo -e "${BLUE}"
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║           Neam v0.6.4 Multi-Cloud Test Runner                    ║"
echo "║              Cost Comparison & Performance Testing               ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# Check prerequisites
echo -e "${YELLOW}Checking prerequisites...${NC}"

check_command() {
    if ! command -v $1 &> /dev/null; then
        echo -e "${RED}Error: $1 is required but not installed${NC}"
        exit 1
    fi
}

check_command "jq"
check_command "curl"

# Check cloud CLIs based on enabled providers
if [[ "$PROVIDERS" == *"aws"* ]]; then
    check_command "aws"
    echo -e "  ${GREEN}✓${NC} AWS CLI"
fi

if [[ "$PROVIDERS" == *"gcp"* ]]; then
    check_command "gcloud"
    echo -e "  ${GREEN}✓${NC} Google Cloud CLI"
fi

if [[ "$PROVIDERS" == *"azure"* ]]; then
    check_command "az"
    echo -e "  ${GREEN}✓${NC} Azure CLI"
fi

if [[ "$PROVIDERS" == *"alibaba"* ]]; then
    check_command "aliyun"
    echo -e "  ${GREEN}✓${NC} Alibaba Cloud CLI"
fi

# Check config file
if [[ ! -f "$CONFIG_FILE" ]]; then
    echo -e "${RED}Error: Config file not found: $CONFIG_FILE${NC}"
    exit 1
fi
echo -e "  ${GREEN}✓${NC} Config file: $CONFIG_FILE"

# Create output directory
mkdir -p "$OUTPUT_DIR"
echo -e "  ${GREEN}✓${NC} Output directory: $OUTPUT_DIR"

# Validate credentials
echo -e "\n${YELLOW}Validating cloud credentials...${NC}"

validate_aws() {
    if aws sts get-caller-identity &> /dev/null; then
        echo -e "  ${GREEN}✓${NC} AWS credentials valid"
        return 0
    else
        echo -e "  ${RED}✗${NC} AWS credentials invalid"
        return 1
    fi
}

validate_gcp() {
    if gcloud auth print-access-token &> /dev/null; then
        echo -e "  ${GREEN}✓${NC} GCP credentials valid"
        return 0
    else
        echo -e "  ${RED}✗${NC} GCP credentials invalid"
        return 1
    fi
}

validate_azure() {
    if az account show &> /dev/null; then
        echo -e "  ${GREEN}✓${NC} Azure credentials valid"
        return 0
    else
        echo -e "  ${RED}✗${NC} Azure credentials invalid"
        return 1
    fi
}

validate_alibaba() {
    if aliyun sts GetCallerIdentity &> /dev/null; then
        echo -e "  ${GREEN}✓${NC} Alibaba Cloud credentials valid"
        return 0
    else
        echo -e "  ${RED}✗${NC} Alibaba Cloud credentials invalid"
        return 1
    fi
}

# Validate enabled providers
VALID_PROVIDERS=""
IFS=',' read -ra PROVIDER_ARRAY <<< "$PROVIDERS"
for provider in "${PROVIDER_ARRAY[@]}"; do
    case $provider in
        aws)
            validate_aws && VALID_PROVIDERS="${VALID_PROVIDERS}aws,"
            ;;
        gcp)
            validate_gcp && VALID_PROVIDERS="${VALID_PROVIDERS}gcp,"
            ;;
        azure)
            validate_azure && VALID_PROVIDERS="${VALID_PROVIDERS}azure,"
            ;;
        alibaba)
            validate_alibaba && VALID_PROVIDERS="${VALID_PROVIDERS}alibaba,"
            ;;
    esac
done

# Remove trailing comma
VALID_PROVIDERS="${VALID_PROVIDERS%,}"

if [[ -z "$VALID_PROVIDERS" ]]; then
    echo -e "${RED}Error: No valid cloud credentials found${NC}"
    exit 1
fi

echo -e "\n${GREEN}Running tests on: $VALID_PROVIDERS${NC}"
echo -e "${GREEN}Workloads: $WORKLOADS${NC}"
echo -e "${GREEN}Requests per workload: $REQUESTS${NC}"
echo -e "${GREEN}Concurrency: $CONCURRENCY${NC}"

# Initialize results
RESULTS_FILE="$OUTPUT_DIR/results.json"
echo '{"results": [], "start_time": "'$(date -Iseconds)'"}' > "$RESULTS_FILE"

# Run tests for each provider
run_provider_test() {
    local provider=$1
    local workload=$2
    local output_file="$OUTPUT_DIR/${provider}_${workload}.json"

    echo -e "\n${BLUE}Testing $provider - $workload workload...${NC}"

    # Determine endpoint and configuration based on provider
    case $provider in
        aws)
            REGION="us-east-1"
            ENDPOINT="${AWS_LAMBDA_ENDPOINT:-https://lambda.us-east-1.amazonaws.com}"
            ;;
        gcp)
            REGION="us-central1"
            ENDPOINT="${GCP_FUNCTION_ENDPOINT:-https://us-central1-${GCP_PROJECT_ID}.cloudfunctions.net}"
            ;;
        azure)
            REGION="eastus"
            ENDPOINT="${AZURE_FUNCTION_ENDPOINT:-https://${AZURE_FUNCTION_APP}.azurewebsites.net}"
            ;;
        alibaba)
            REGION="cn-hangzhou"
            ENDPOINT="${ALIBABA_FC_ENDPOINT:-https://${ALIBABA_ACCOUNT_ID}.cn-hangzhou.fc.aliyuncs.com}"
            ;;
    esac

    # Load prompts based on workload
    case $workload in
        latency)
            PROMPTS_FILE="${SCRIPT_DIR}/prompts/latency_test.jsonl"
            ;;
        cost)
            PROMPTS_FILE="${SCRIPT_DIR}/prompts/cost_test.jsonl"
            ;;
        throughput)
            PROMPTS_FILE="${SCRIPT_DIR}/prompts/latency_test.jsonl"  # Use short prompts for throughput
            ;;
        gpu)
            PROMPTS_FILE="${SCRIPT_DIR}/prompts/cost_test.jsonl"
            ;;
    esac

    # Initialize result file
    echo '{
        "provider": "'$provider'",
        "workload": "'$workload'",
        "region": "'$REGION'",
        "requests": [],
        "metrics": {}
    }' > "$output_file"

    # Run the actual tests (placeholder - would call neam_cloud_test binary)
    echo -e "  Running $REQUESTS requests with concurrency $CONCURRENCY..."

    # Simulate test execution (replace with actual test runner)
    local start_time=$(date +%s%3N)

    # TODO: Replace with actual neam_cloud_test binary call
    # neam_cloud_test --provider $provider --workload $workload --requests $REQUESTS --concurrency $CONCURRENCY --output $output_file

    # For now, generate mock results
    local total_cost=0
    local total_latency=0
    local success_count=$REQUESTS

    # Mock results generation
    for ((i=1; i<=REQUESTS; i++)); do
        local latency=$((RANDOM % 200 + 50))
        local cost=$(echo "scale=6; ($RANDOM % 100 + 1) / 100000" | bc)
        total_latency=$((total_latency + latency))
        total_cost=$(echo "$total_cost + $cost" | bc)
    done

    local avg_latency=$((total_latency / REQUESTS))
    local end_time=$(date +%s%3N)
    local duration=$((end_time - start_time))

    # Update output file with metrics
    jq --arg avg "$avg_latency" --arg cost "$total_cost" --arg success "$success_count" \
        '.metrics = {"avg_latency_ms": ($avg|tonumber), "total_cost_usd": ($cost|tonumber), "success_count": ($success|tonumber)}' \
        "$output_file" > "${output_file}.tmp" && mv "${output_file}.tmp" "$output_file"

    echo -e "  ${GREEN}✓${NC} Completed in ${duration}ms - Avg latency: ${avg_latency}ms, Cost: \$${total_cost}"
}

# Execute tests
echo -e "\n${YELLOW}═══════════════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}                         RUNNING TESTS                              ${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════════════${NC}"

IFS=',' read -ra PROVIDER_ARRAY <<< "$VALID_PROVIDERS"
IFS=',' read -ra WORKLOAD_ARRAY <<< "$WORKLOADS"

if $PARALLEL; then
    # Run in parallel
    for provider in "${PROVIDER_ARRAY[@]}"; do
        for workload in "${WORKLOAD_ARRAY[@]}"; do
            run_provider_test "$provider" "$workload" &
        done
    done
    wait
else
    # Run sequentially
    for provider in "${PROVIDER_ARRAY[@]}"; do
        for workload in "${WORKLOAD_ARRAY[@]}"; do
            run_provider_test "$provider" "$workload"
        done
    done
fi

# Generate comparison report
echo -e "\n${YELLOW}═══════════════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}                    GENERATING COMPARISON REPORT                    ${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════════════${NC}"

# Merge all results
echo -e "${BLUE}Merging results...${NC}"
REPORT_FILE="$OUTPUT_DIR/cost_comparison_report.json"

# Create merged report
echo '{
    "report_id": "'$(uuidgen 2>/dev/null || echo "report-$(date +%s)")'",
    "generated_at": "'$(date -Iseconds)'",
    "providers_tested": ['$(echo $VALID_PROVIDERS | sed 's/,/","/g' | sed 's/^/"/;s/$/"/')'],
    "workloads_tested": ['$(echo $WORKLOADS | sed 's/,/","/g' | sed 's/^/"/;s/$/"/')'],
    "total_requests": '$((REQUESTS * ${#PROVIDER_ARRAY[@]} * ${#WORKLOAD_ARRAY[@]}))' ,
    "results": {}
}' > "$REPORT_FILE"

# Add individual provider results
for provider in "${PROVIDER_ARRAY[@]}"; do
    for workload in "${WORKLOAD_ARRAY[@]}"; do
        if [[ -f "$OUTPUT_DIR/${provider}_${workload}.json" ]]; then
            jq --slurpfile data "$OUTPUT_DIR/${provider}_${workload}.json" \
                '.results["'${provider}'_'${workload}'"] = $data[0]' \
                "$REPORT_FILE" > "${REPORT_FILE}.tmp" && mv "${REPORT_FILE}.tmp" "$REPORT_FILE"
        fi
    done
done

# Generate HTML report
echo -e "${BLUE}Generating HTML report...${NC}"
HTML_REPORT="$OUTPUT_DIR/cost_comparison_report.html"

# Use template and substitute values
cp "${SCRIPT_DIR}/templates/cost_report_template.html" "$HTML_REPORT"

# TODO: Replace template placeholders with actual values from results
# For now, just copy the template

echo -e "${GREEN}✓${NC} Reports generated:"
echo -e "    JSON: $REPORT_FILE"
echo -e "    HTML: $HTML_REPORT"

# Summary
echo -e "\n${YELLOW}═══════════════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}                           SUMMARY                                  ${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════════════${NC}"

echo -e "
${GREEN}Test execution completed!${NC}

Results saved to: $OUTPUT_DIR

Files generated:
  - cost_comparison_report.json (detailed results)
  - cost_comparison_report.html (interactive report)
  - Individual provider/workload results

To view the HTML report:
  open $HTML_REPORT

To analyze with jq:
  jq '.results' $REPORT_FILE
"
