#!/usr/bin/env bash
set -euo pipefail

VERSION="v0.13.0"
OWNER="Praveengovianalytics"
REPO="NeamC"
ARTIFACT_PREFIX="neam"
INSTALL_ROOT="/usr/local/neam"
OS_OVERRIDE=""
ARCH_OVERRIDE=""
EXECUTE=false
GPG_KEYRING=""
ALLOW_MISSING_MEDIA=false

usage() {
  cat <<'USAGE'
Neam GitHub installer (plan-first, opt-in execute)

Usage:
  neam-install.sh [--version <tag>] [--owner <org>] [--repo <name>] \
                  [--install-dir <path>] [--os <linux|darwin|windows>] \
                  [--arch <amd64|arm64>] [--artifact-prefix <name>] \
                  [--gpg-keyring <path>] [--allow-missing-media] [--execute]

Defaults:
  version      v0.13.0
  owner/repo   Praveengovianalytics/NeamC (override if using a different release host)
  install-dir  /usr/local/neam
  mode         dry-run (plan only)
  media checks enabled (fail if media manifest missing)

Examples:
  ./neam-install.sh --version v0.13.0
  sudo ./neam-install.sh --version v0.13.0 --execute
  ./neam-install.sh --version v0.13.0 --install-dir "$HOME/.local/neam" --execute
USAGE
}

log() { printf '[neam-install] %s\n' "$*"; }
err() { printf '[neam-install][error] %s\n' "$*" >&2; }

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    err "missing required command: $1"
    exit 1
  fi
}

sha256_tool() {
  if command -v sha256sum >/dev/null 2>&1; then
    echo "sha256sum"
  elif command -v shasum >/dev/null 2>&1; then
    echo "shasum -a 256"
  else
    err "neither sha256sum nor shasum found"
    exit 1
  fi
}

os_normalized() {
  if [[ -n "$OS_OVERRIDE" ]]; then
    echo "$OS_OVERRIDE"
    return
  fi
  case "$(uname -s)" in
    Linux*) echo "linux" ;;
    Darwin*) echo "darwin" ;;
    MINGW*|MSYS*|CYGWIN*|Windows*) echo "windows" ;;
    *) err "unsupported OS: $(uname -s)"; exit 1 ;;
  esac
}

arch_normalized() {
  if [[ -n "$ARCH_OVERRIDE" ]]; then
    echo "$ARCH_OVERRIDE"
    return
  fi
  case "$(uname -m)" in
    x86_64|amd64) echo "amd64" ;;
    arm64|aarch64) echo "arm64" ;;
    *) err "unsupported arch: $(uname -m)"; exit 1 ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="$2"; shift 2 ;;
    --owner) OWNER="$2"; shift 2 ;;
    --repo) REPO="$2"; shift 2 ;;
    --install-dir) INSTALL_ROOT="$2"; shift 2 ;;
    --os) OS_OVERRIDE="$2"; shift 2 ;;
    --arch) ARCH_OVERRIDE="$2"; shift 2 ;;
    --artifact-prefix) ARTIFACT_PREFIX="$2"; shift 2 ;;
    --gpg-keyring) GPG_KEYRING="$2"; shift 2 ;;
    --allow-missing-media) ALLOW_MISSING_MEDIA=true; shift ;;
    --execute) EXECUTE=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) err "unknown argument: $1"; usage; exit 1 ;;
  esac
done

OS_NAME="$(os_normalized)"
ARCH_NAME="$(arch_normalized)"

BASE_URL="https://github.com/${OWNER}/${REPO}/releases/download/${VERSION}"
ARTIFACT="${ARTIFACT_PREFIX}-${VERSION}-${OS_NAME}-${ARCH_NAME}.tar.gz"
CHECKSUM_FILE="SHA256SUMS"
SIG_FILE="${CHECKSUM_FILE}.sig"
TARGET_ROOT="${INSTALL_ROOT}/${VERSION}"
BIN_ROOT="${INSTALL_ROOT}/bin"

log "plan for Neam ${VERSION} (${OS_NAME}/${ARCH_NAME})"
log "release base: ${BASE_URL}"
log "artifact: ${ARTIFACT}"
log "install root: ${INSTALL_ROOT}"
log "target dir: ${TARGET_ROOT}"
log "bin dir: ${BIN_ROOT}"

if [[ "${EXECUTE}" != true ]]; then
  log "dry-run complete. Add --execute to perform the download and install."
  exit 0
fi

require_cmd curl
require_cmd tar
CHECKSUM_CMD="$(sha256_tool)"

TMPDIR="$(mktemp -d)"
cleanup() { rm -rf "${TMPDIR}"; }
trap cleanup EXIT

log "downloading checksums..."
curl -fsSL "${BASE_URL}/${CHECKSUM_FILE}" -o "${TMPDIR}/${CHECKSUM_FILE}"

if [[ -n "${GPG_KEYRING}" ]]; then
  require_cmd gpg
  log "downloading checksum signature..."
  curl -fsSL "${BASE_URL}/${SIG_FILE}" -o "${TMPDIR}/${SIG_FILE}"
  log "verifying signature using keyring ${GPG_KEYRING}"
  gpg --no-default-keyring --keyring "${GPG_KEYRING}" --verify "${TMPDIR}/${SIG_FILE}" "${TMPDIR}/${CHECKSUM_FILE}" || {
    err "signature verification failed"; exit 1; }
fi

log "downloading artifact..."
curl -fsSL "${BASE_URL}/${ARTIFACT}" -o "${TMPDIR}/${ARTIFACT}"

log "verifying checksum..."
EXPECTED_LINE=$(grep "${ARTIFACT}" "${TMPDIR}/${CHECKSUM_FILE}" || true)
if [[ -z "${EXPECTED_LINE}" ]]; then
  err "artifact not found in checksum file"
  exit 1
fi
EXPECTED_SUM=$(echo "${EXPECTED_LINE}" | awk '{print $1}')
ACTUAL_SUM=$(${CHECKSUM_CMD} "${TMPDIR}/${ARTIFACT}" | awk '{print $1}')
if [[ "${EXPECTED_SUM}" != "${ACTUAL_SUM}" ]]; then
  err "checksum mismatch: expected ${EXPECTED_SUM}, got ${ACTUAL_SUM}"
  exit 1
fi
log "checksum verified (${EXPECTED_SUM})"

log "installing to ${TARGET_ROOT}"
mkdir -p "${TARGET_ROOT}" "${BIN_ROOT}" "${TARGET_ROOT}/receipts"

tar -xzf "${TMPDIR}/${ARTIFACT}" -C "${TARGET_ROOT}" --strip-components=1

ln -sfn "${TARGET_ROOT}/bin/neam" "${BIN_ROOT}/neam"
ln -sfn "${TARGET_ROOT}/bin/neamc" "${BIN_ROOT}/neamc"

MEDIA_MANIFEST="${TARGET_ROOT}/share/neam/media"
if [[ ! -d "${MEDIA_MANIFEST}" ]]; then
  if [[ "${ALLOW_MISSING_MEDIA}" == true ]]; then
    log "warning: media capability assets not found at ${MEDIA_MANIFEST}; continuing due to --allow-missing-media"
  else
    err "media capability assets not found at ${MEDIA_MANIFEST}; rerun with --allow-missing-media to bypass"
    exit 1
  fi
fi

RECEIPT="${TARGET_ROOT}/receipts/install-$(date -u +%Y%m%dT%H%M%SZ).json"
cat <<RECEIPT > "${RECEIPT}"
{
  "version": "${VERSION}",
  "os": "${OS_NAME}",
  "arch": "${ARCH_NAME}",
  "owner": "${OWNER}",
  "repo": "${REPO}",
  "artifact": "${ARTIFACT}",
  "checksum": "${EXPECTED_SUM}",
  "installed_at_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "install_root": "${INSTALL_ROOT}"
}
RECEIPT

log "installation complete"
log "add ${BIN_ROOT} to your PATH if not already present"
