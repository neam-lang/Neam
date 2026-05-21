# CTCR — Compile-Time Catch Rate Corpus

The CTCR corpus measures how many malformed v1.4.5 harness programs are rejected by `neamc` at compile time, before any LLM call is made.

This is Neam's unique benchmark dimension: no YAML- or Python-based harness framework (AgentSPEX, LangGraph, DSPy, CrewAI) can report this metric, because no other harness language is compiled.

## Layout

```
tests/ctcr/
├── H-001/        20 programs that must fail with H-001 (empty harness)
├── H-015/        20 programs that must fail with H-015 (handoff schema_version)
├── P-FR-001/     20 programs that must fail with P-FR-001 (forge role)
├── accepts/      20 positive-control programs that must compile
├── baseline.json CI regression gate — current CTCR numbers
└── README.md     this file
```

## Running

```bash
# Build neamc first
cmake --build build-relwithdebinfo --target neamc

# Run the suite
scripts/run_ctcr.sh

# With JSON output (for CI comparison)
scripts/run_ctcr.sh --json build/ctcr.json

# Verbose (shows per-file results)
scripts/run_ctcr.sh --verbose
```

Exit code `0` if all targets met, `1` if any rule is below target (for CI gate).

## Targets

| Rule | Meaning | Target |
|---|---|---|
| H-001 | empty harness rejected | 1.00 |
| H-015 | handoff missing schema_version rejected | 1.00 |
| P-FR-001 | invalid forge agent role rejected | 1.00 |
| accepts | positive controls compile | 1.00 |

## Current results (2026-04-24, Phase 2 complete)

```
H-001    : 1.000  (20/20)
H-015    : 1.000  (20/20)
P-FR-001 : 1.000  (20/20)
accepts  : 1.000  (20/20)
────────────────────────────
overall  : 1.000  (60/60 bad caught)
```

## Growing the corpus

Per the v1.4.5 implementation spec §47, every new compiler validation rule should add ≥ 10 variants to the corpus. The target of 500+ variants total is reached when all 18 rules (H-001 through H-018) are implemented.

Pattern for a new rule:

1. Create `tests/ctcr/<CODE>/` directory.
2. Drop 10+ `.neam` files that should fail with `<CODE>`.
3. Each file starts with `// CTCR expects: <CODE>` as a comment for human readers.
4. Extend `run_ctcr.sh` with a new `run_category` call for that dir.
5. Add a target constant in the runner.
6. Re-run; commit updated `baseline.json`.
