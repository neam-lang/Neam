# Neam GitHub-Based Installer

This document describes a GitHub-hosted installation workflow for the Neam
programming language/toolchain (spec v0.13 draft). It is meant to accompany the
portable installer script found at `scripts/neam-install.sh` and the GitHub
Release artifacts for the Neam compiler/runtime/CLI published in
[`Praveengovianalytics/NeamC`](https://github.com/Praveengovianalytics/NeamC).

## Goals

- Deterministic, auditable installs using GitHub Releases as the distribution
  channel.
- Defense-in-depth: checksum + (optional) signature verification, TLS-only
  downloads, no `curl | sh`, and opt-in execution (dry-run by default).
- Multi-platform support with explicit artifacts per OS/arch and reproducible
  directory layout for policy-aware sandboxes.
- Alignment with Neam guardrails: deny-by-default capabilities, provenance-rich
  receipts, and reproducible bundles.

## Release artifact model

Each Neam release publishes the following assets to GitHub Releases:

- Tarballs per platform/arch (default prefix `neam`), e.g.,
  `neam-<version>-<os>-<arch>.tar.gz` containing:
  - `bin/neam` (runtime/CLI), `bin/neamc` (compiler), and any shared libs.
  - `share/neam/` with schema snapshots, Construct templates, policy packs, and
    MCP tool schemas for TerminalTool/PythonTool/WebSearchTool.
  - `LICENSE` and `NOTICE` files.
- `SHA256SUMS` file covering every tarball.
- Optional `SHA256SUMS.sig` signed with a maintainers’ key.
- Optional SBOM and provenance attestations.

## Installer script overview

The installer script is intentionally small and auditable:

- Platform detection: Linux/macOS/Windows (WSL) with arch normalization
  (x86_64/amd64, arm64/aarch64).
- Plan-first: without `--execute` the script prints the computed URLs,
  install target, and required tools. This prevents accidental partial installs
  when artifacts are not yet published.
- Verification steps (when `--execute` is provided):
  1. Download `SHA256SUMS` (+ optional signature) from the release.
  2. Verify checksums for the selected tarball.
  3. Verify signature if a keyring is provided.
  4. Extract to a versioned directory (default: `/usr/local/neam/<version>`).
  5. Create deterministic symlinks (`neam`, `neamc`) in `bin/`.
  6. Emit a minimal receipt (paths, checksum used, timestamp) for auditability.

## Usage

```bash
# Dry-run: preview URLs and steps for v0.13.0
./scripts/neam-install.sh --version v0.13.0

# Execute install to /usr/local/neam/v0.13.0 (may require sudo)
./scripts/neam-install.sh --version v0.13.0 --execute

# Install to a custom prefix (no sudo if within $HOME)
./scripts/neam-install.sh --version v0.13.0 --install-dir "$HOME/.local/neam" --execute
```

Key flags:

- `--version`: release tag (e.g., `v0.13.0`).
- `--owner` / `--repo`: GitHub org/repo hosting the release (defaults align with
  `Praveengovianalytics/NeamC`).
- `--artifact-prefix`: tarball prefix (default `neam`; change if the upstream
  release uses a different naming convention).
- `--os` / `--arch`: override platform detection when cross-installing.
- `--execute`: perform the installation; omit to keep dry-run mode.
- `--gpg-keyring`: optional path to a keyring for verifying `SHA256SUMS.sig`.

## Threat model and mitigations

- **Supply chain tampering**: mitigate with pinned checksums, optional signature
  verification, and explicit release owners. Encourage downloading releases via
  `https://github.com/<owner>/<repo>/releases/download/<tag>/...` only.
- **Command injection**: the script prefers argv-style execution, avoids
  eval/`curl | sh`, and validates inputs. Shell is used only for orchestrating
  trusted tooling (curl, tar, sha256sum/shasum).
- **Prompt injection from tool outputs**: installer receipts are data-only; they
  must be treated as evidence lane inputs if ingested later.
- **Privilege minimization**: installs to user-writable prefixes by default;
  `sudo` is only needed for system prefixes.

## Directory layout after install

```
<install-dir>/
  └── v0.13.0/
      ├── bin/
      │   ├── neam
      │   └── neamc
      ├── share/neam/
      │   ├── construct-templates/
      │   ├── policy-profiles/
      │   ├── schemas/ (MCP snapshots, AsyncAPI/CloudEvents/A2A, tool schemas)
      │   └── skills-index/
      └── receipts/
          └── install-<timestamp>.json
```

`bin/neam` exposes the CLI matrix from Appendix B (init/build/run/test/eval/
trace/ingest/deploy/publish/event/a2a/mesh). `bin/neamc` is the compiler.

## Operational notes

- **Reproducibility**: checksum-verified tarballs and versioned directories make
  installs replayable. Keep `SHA256SUMS` alongside receipts for auditability.
- **Sandboxing**: TerminalTool and PythonTool should run under sandbox profiles
  with network disabled by default; the installer does not alter system-wide
  sandboxes and instead ships profile templates in `share/neam/policy-profiles`.
- **Profiles**: include `dev`, `stage`, and `prod` security profiles with sane
  defaults; users can select via `neam run --profile <name>`.
- **Removal**: delete the versioned directory and update symlinks; the script
  can be extended with an `--uninstall` flag if desired.

## Next steps

- Publish initial release artifacts to GitHub with checksums/signatures.
- Wire CI to build NeamC/Neam for supported platforms and upload to releases.
- Add SBOM/provenance attestations and automated signature verification tests.
- Extend the installer with `--uninstall` and `--list-versions` helpers once
  multiple releases are available.
