#!/usr/bin/env bash
# Neam v0.6.7 Installer
# Usage: curl -fsSL https://github.com/neam-lang/Neam/releases/download/v0.6.7/install.sh | bash
set -euo pipefail

VERSION="0.6.7"
REPO="neam-lang/Neam"
INSTALL_DIR="${NEAM_INSTALL_DIR:-/usr/local/bin}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[info]${NC} $*"; }
ok()    { echo -e "${GREEN}[ok]${NC} $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC} $*"; }
fail()  { echo -e "${RED}[error]${NC} $*"; exit 1; }

echo ""
echo -e "${CYAN}  _   _"
echo -e " | \ | | ___  __ _ _ __ ___"
echo -e " |  \| |/ _ \/ _\` | '_ \` _ \\"
echo -e " | |\  |  __/ (_| | | | | | |"
echo -e " |_| \_|\___|\__,_|_| |_| |_|${NC}"
echo ""
echo -e " The Programming Language for AI Agents"
echo -e " Version ${VERSION}"
echo ""

# Detect OS and architecture
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Darwin)  PLATFORM="macos" ;;
    Linux)   PLATFORM="linux" ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
    *)       fail "Unsupported OS: $OS" ;;
esac

case "$ARCH" in
    x86_64|amd64)   ARCH="x86_64" ;;
    arm64|aarch64)   ARCH="arm64" ;;
    *)               fail "Unsupported architecture: $ARCH" ;;
esac

info "Detected platform: ${PLATFORM}-${ARCH}"

# Determine asset name
if [ "$PLATFORM" = "windows" ]; then
    ASSET="neam-v${VERSION}-windows-${ARCH}.zip"
else
    ASSET="neam-v${VERSION}-${PLATFORM}-${ARCH}.tar.gz"
fi

DOWNLOAD_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${ASSET}"

# Create temp directory
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

# Download
info "Downloading ${ASSET}..."
if command -v curl &>/dev/null; then
    curl -fSL --progress-bar -o "${TMPDIR}/${ASSET}" "$DOWNLOAD_URL" || fail "Download failed. Asset '${ASSET}' may not exist for your platform.\n  URL: ${DOWNLOAD_URL}\n  Try building from source: https://github.com/${REPO}#building-from-source"
elif command -v wget &>/dev/null; then
    wget -q --show-progress -O "${TMPDIR}/${ASSET}" "$DOWNLOAD_URL" || fail "Download failed."
else
    fail "Neither curl nor wget found. Please install one and retry."
fi

# Extract
info "Extracting..."
cd "$TMPDIR"
if [ "$PLATFORM" = "windows" ]; then
    unzip -q "$ASSET"
else
    tar xzf "$ASSET"
fi

# Install
info "Installing to ${INSTALL_DIR}..."
if [ -w "$INSTALL_DIR" ]; then
    cp -f neamc neam "$INSTALL_DIR/" 2>/dev/null || cp -f neamc.exe neam.exe "$INSTALL_DIR/" 2>/dev/null
else
    warn "Need elevated permissions for ${INSTALL_DIR}"
    sudo cp -f neamc neam "$INSTALL_DIR/" 2>/dev/null || sudo cp -f neamc.exe neam.exe "$INSTALL_DIR/" 2>/dev/null
fi

chmod +x "${INSTALL_DIR}/neamc" "${INSTALL_DIR}/neam" 2>/dev/null || true

# Verify
if command -v neamc &>/dev/null; then
    ok "neamc installed at $(which neamc)"
else
    warn "neamc installed to ${INSTALL_DIR}/neamc but not in PATH"
fi

if command -v neam &>/dev/null; then
    ok "neam installed at $(which neam)"
else
    warn "neam installed to ${INSTALL_DIR}/neam but not in PATH"
fi

echo ""
ok "Neam v${VERSION} installed successfully!"
echo ""
echo "  Quick start:"
echo "    neamc hello.neam -o hello.nbc    # Compile"
echo "    neam hello.nbc                   # Run"
echo ""
echo "  Documentation: https://github.com/${REPO}"
echo ""
