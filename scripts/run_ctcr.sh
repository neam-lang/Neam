#!/usr/bin/env bash
# run_ctcr.sh — Compile-Time Catch Rate runner for Neam v1.4.5.
#
# Walks the tests/ctcr/ corpus and reports how many "bad" programs are
# correctly rejected at compile time, and how many "accepts" programs
# continue to compile cleanly.
#
# CTCR (Compile-Time Catch Rate) is Neam's unique benchmark dimension:
# no other harness framework (including AgentSPEX) can report this
# because no other harness language is compiled.
#
# Usage:
#   scripts/run_ctcr.sh                 # uses default neamc location
#   scripts/run_ctcr.sh --neamc PATH    # custom neamc binary
#   scripts/run_ctcr.sh --json FILE     # also emit JSON report
#   scripts/run_ctcr.sh --verbose       # show each test result
#
# Exit code:
#   0   if all targets met
#   1   if any target missed (for CI gate)

set -u

# ─── defaults ──────────────────────────────────────────────────────────
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NEAMC="${REPO_ROOT}/build-relwithdebinfo/neamc"
CORPUS="${REPO_ROOT}/tests/ctcr"
JSON_OUT=""
VERBOSE=0

# Per-v1.4.5 impl spec §47 targets; Phase 0-2 enforces validation strictly
# so every rule should hit 1.00.  Lower these if you add fuzz-generated
# variants that are intentionally hard.
TARGET_H001=1.0
TARGET_H015=1.0
TARGET_PFR001=1.0
TARGET_ACCEPTS=1.0

# ─── args ──────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --neamc)    NEAMC="$2"; shift 2 ;;
        --corpus)   CORPUS="$2"; shift 2 ;;
        --json)     JSON_OUT="$2"; shift 2 ;;
        --verbose)  VERBOSE=1; shift ;;
        -h|--help)
            sed -n '2,20p' "$0"
            exit 0
            ;;
        *)
            echo "unknown arg: $1" >&2
            exit 2
            ;;
    esac
done

if [[ ! -x "$NEAMC" ]]; then
    echo "error: neamc not found at $NEAMC" >&2
    echo "  hint: cmake --build build-relwithdebinfo --target neamc" >&2
    exit 2
fi

# ─── run one category ──────────────────────────────────────────────────
# Usage: run_category <dir> <expected_error_code_or_ACCEPTS>
# Sets globals: CAT_TOTAL, CAT_CAUGHT; prints per-file output.
run_category() {
    local dir="$1" expected="$2"
    CAT_TOTAL=0
    CAT_CAUGHT=0

    for src in "$dir"/*.neam; do
        [[ -f "$src" ]] || continue
        CAT_TOTAL=$((CAT_TOTAL + 1))
        # Run neamc to a tempfile; remove any stale output first.
        rm -f /tmp/_ctcr.neamb
        local out
        out=$("$NEAMC" "$src" -o /tmp/_ctcr.neamb 2>&1) || true

        # neamc is known to print "Compilation failed" even when exit code is 0,
        # so determine success by two signals:
        #   (a) the string "Compilation failed" is absent from stdout/stderr
        #   (b) the .neamb output was created
        local compiled=0
        if [[ "$out" != *"Compilation failed"* && "$out" != *"Parse error"* && -s /tmp/_ctcr.neamb ]]; then
            compiled=1
        fi

        if [[ "$expected" == "ACCEPTS" ]]; then
            if [[ $compiled -eq 1 ]]; then
                CAT_CAUGHT=$((CAT_CAUGHT + 1))
                [[ $VERBOSE -eq 1 ]] && echo "  [OK] $(basename "$src")"
            else
                echo "  [FAIL-accepts] $(basename "$src"): $(echo "$out" | head -1)"
            fi
        else
            if [[ $compiled -eq 0 && "$out" == *"$expected"* ]]; then
                CAT_CAUGHT=$((CAT_CAUGHT + 1))
                [[ $VERBOSE -eq 1 ]] && echo "  [OK] $(basename "$src")"
            elif [[ $compiled -eq 1 ]]; then
                echo "  [FAIL-rejects] $(basename "$src"): expected $expected but compiled"
            else
                echo "  [FAIL-wrongcode] $(basename "$src"): wanted $expected, got: $(echo "$out" | head -1)"
            fi
        fi
    done
}

# ─── main ──────────────────────────────────────────────────────────────
echo "═══════════════════════════════════════════════════════════════"
echo "  Neam v1.4.5 — Compile-Time Catch Rate (CTCR) Report"
echo "  neamc:   $NEAMC"
echo "  corpus:  $CORPUS"
echo "═══════════════════════════════════════════════════════════════"
echo ""

H001_TOTAL=0 H001_CAUGHT=0
H015_TOTAL=0 H015_CAUGHT=0
PFR_TOTAL=0  PFR_CAUGHT=0
ACC_TOTAL=0  ACC_CAUGHT=0

echo "--- H-001 (empty harness rejected) ---"
run_category "$CORPUS/H-001" "H-001"
H001_TOTAL=$CAT_TOTAL; H001_CAUGHT=$CAT_CAUGHT
echo "  caught: $CAT_CAUGHT / $CAT_TOTAL"
echo ""

echo "--- H-015 (handoff schema_version required) ---"
run_category "$CORPUS/H-015" "H-015"
H015_TOTAL=$CAT_TOTAL; H015_CAUGHT=$CAT_CAUGHT
echo "  caught: $CAT_CAUGHT / $CAT_TOTAL"
echo ""

echo "--- P-FR-001 (forge agent role validation) ---"
run_category "$CORPUS/P-FR-001" "P-FR-001"
PFR_TOTAL=$CAT_TOTAL; PFR_CAUGHT=$CAT_CAUGHT
echo "  caught: $CAT_CAUGHT / $CAT_TOTAL"
echo ""

echo "--- accepts (positive controls) ---"
run_category "$CORPUS/accepts" "ACCEPTS"
ACC_TOTAL=$CAT_TOTAL; ACC_CAUGHT=$CAT_CAUGHT
echo "  accepted: $CAT_CAUGHT / $CAT_TOTAL"
echo ""

# ─── summary ───────────────────────────────────────────────────────────
pct() {
    local c="$1" t="$2"
    if [[ "$t" -eq 0 ]]; then echo "0.000"; return; fi
    python3 -c "print(f'{$c/$t:.3f}')"
}

H001_RATE=$(pct $H001_CAUGHT $H001_TOTAL)
H015_RATE=$(pct $H015_CAUGHT $H015_TOTAL)
PFR_RATE=$(pct  $PFR_CAUGHT  $PFR_TOTAL)
ACC_RATE=$(pct  $ACC_CAUGHT  $ACC_TOTAL)

TOTAL_BAD=$((H001_TOTAL + H015_TOTAL + PFR_TOTAL))
TOTAL_CAUGHT=$((H001_CAUGHT + H015_CAUGHT + PFR_CAUGHT))
OVERALL_RATE=$(pct $TOTAL_CAUGHT $TOTAL_BAD)

echo "═══════════════════════════════════════════════════════════════"
echo "  CTCR SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
printf "  H-001      : %s  (%d/%d)   target: %.2f\n" "$H001_RATE" $H001_CAUGHT $H001_TOTAL "$TARGET_H001"
printf "  H-015      : %s  (%d/%d)   target: %.2f\n" "$H015_RATE" $H015_CAUGHT $H015_TOTAL "$TARGET_H015"
printf "  P-FR-001   : %s  (%d/%d)   target: %.2f\n" "$PFR_RATE"  $PFR_CAUGHT  $PFR_TOTAL  "$TARGET_PFR001"
printf "  accepts    : %s  (%d/%d)   target: %.2f\n" "$ACC_RATE"  $ACC_CAUGHT  $ACC_TOTAL  "$TARGET_ACCEPTS"
echo "  ────────────────────────────────────────────────"
printf "  overall    : %s  (%d/%d bad caught)\n" "$OVERALL_RATE" "$TOTAL_CAUGHT" "$TOTAL_BAD"
echo ""

# ─── JSON emit ────────────────────────────────────────────────────────
if [[ -n "$JSON_OUT" ]]; then
    cat > "$JSON_OUT" <<EOF
{
  "neam_version": "v1.4.5-feature",
  "corpus_path": "$CORPUS",
  "neamc_path": "$NEAMC",
  "per_rule": {
    "H-001":    { "caught": $H001_CAUGHT, "total": $H001_TOTAL, "rate": $H001_RATE, "target": $TARGET_H001 },
    "H-015":    { "caught": $H015_CAUGHT, "total": $H015_TOTAL, "rate": $H015_RATE, "target": $TARGET_H015 },
    "P-FR-001": { "caught": $PFR_CAUGHT,  "total": $PFR_TOTAL,  "rate": $PFR_RATE,  "target": $TARGET_PFR001 }
  },
  "positive_control": {
    "accepted": $ACC_CAUGHT, "total": $ACC_TOTAL, "rate": $ACC_RATE, "target": $TARGET_ACCEPTS
  },
  "overall_ctcr": $OVERALL_RATE,
  "overall_caught": $TOTAL_CAUGHT,
  "overall_total": $TOTAL_BAD
}
EOF
    echo "  JSON written to $JSON_OUT"
fi

# ─── CI gate ───────────────────────────────────────────────────────────
meets() { python3 -c "import sys; sys.exit(0 if float('$1') >= float('$2') else 1)"; }
fail=0
meets "$H001_RATE" "$TARGET_H001" || { echo "  FAIL: H-001 below target";   fail=1; }
meets "$H015_RATE" "$TARGET_H015" || { echo "  FAIL: H-015 below target";   fail=1; }
meets "$PFR_RATE"  "$TARGET_PFR001" || { echo "  FAIL: P-FR-001 below target"; fail=1; }
meets "$ACC_RATE"  "$TARGET_ACCEPTS" || { echo "  FAIL: positive-control rate below target"; fail=1; }

echo ""
if [[ $fail -eq 0 ]]; then
    echo "  All targets met."
else
    echo "  One or more targets missed."
fi

exit $fail
