#!/usr/bin/env bash

set -euo pipefail

# Usage: ./scripts/run_gym.sh <agent> <workout>

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <agent> <workout>" >&2
  exit 1
fi

if [[ -x "./build/neam-gym" ]]; then
  ./build/neam-gym --agent "$1" --dataset "$2"
elif command -v neam >/dev/null 2>&1; then
  neam gym run "$1" --dataset "$2"
else
  echo "Error: neam-gym binary not found in ./build and 'neam' not on PATH." >&2
  exit 1
fi
