#!/usr/bin/env bash
# Neam v0.9.9 Installer
# Usage: curl -fsSL https://raw.githubusercontent.com/neam-lang/Neam/main/install.sh | bash
set -euo pipefail

VERSION="0.9.9"
REPO="neam-lang/neam-nightly"
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

# Determine asset name (matches CI output naming convention)
case "$PLATFORM" in
    macos)   CI_PLATFORM="darwin" ;;
    linux)   CI_PLATFORM="linux" ;;
    windows) CI_PLATFORM="windows" ;;
esac

case "$ARCH" in
    arm64)   CI_ARCH="arm64" ;;
    x86_64)  CI_ARCH="amd64" ;;
esac

if [ "$PLATFORM" = "windows" ]; then
    ASSET="neam-v${VERSION}-${CI_PLATFORM}-${CI_ARCH}.zip"
else
    ASSET="neam-v${VERSION}-${CI_PLATFORM}-${CI_ARCH}.tar.gz"
fi

# Fallback: try local build naming if CI naming fails
ASSET_ALT="neam-${PLATFORM}-${ARCH}-v${VERSION}.tar.gz"

DOWNLOAD_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${ASSET}"
DOWNLOAD_URL_ALT="https://github.com/${REPO}/releases/download/v${VERSION}/${ASSET_ALT}"

# Create temp directory
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

# Download (try CI naming first, fallback to local build naming)
info "Downloading ${ASSET}..."
DOWNLOADED=false
if command -v curl &>/dev/null; then
    if curl -fSL --progress-bar -o "${TMPDIR}/${ASSET}" "$DOWNLOAD_URL" 2>/dev/null; then
        DOWNLOADED=true
    elif curl -fSL --progress-bar -o "${TMPDIR}/${ASSET_ALT}" "$DOWNLOAD_URL_ALT" 2>/dev/null; then
        ASSET="$ASSET_ALT"
        DOWNLOADED=true
    fi
elif command -v wget &>/dev/null; then
    if wget -q --show-progress -O "${TMPDIR}/${ASSET}" "$DOWNLOAD_URL" 2>/dev/null; then
        DOWNLOADED=true
    elif wget -q --show-progress -O "${TMPDIR}/${ASSET_ALT}" "$DOWNLOAD_URL_ALT" 2>/dev/null; then
        ASSET="$ASSET_ALT"
        DOWNLOADED=true
    fi
else
    fail "Neither curl nor wget found. Please install one and retry."
fi

[ "$DOWNLOADED" = false ] && fail "Download failed. No release asset found for ${PLATFORM}-${ARCH}.\n  Tried: ${DOWNLOAD_URL}\n  Tried: ${DOWNLOAD_URL_ALT}\n  Try building from source: https://github.com/${REPO}#building-from-source"

# Extract
info "Extracting..."
cd "$TMPDIR"
if [ "$PLATFORM" = "windows" ]; then
    unzip -q "$ASSET"
else
    tar xzf "$ASSET"
fi

# Collect binaries — search recursively (archive may have bin/ subdirectory)
BINS=()
for b in neamc neam neam-api neam-pkg neam-lambda neam-cli neam-lsp neam-dap neam-gym neamc.exe neam.exe neam-api.exe neam-pkg.exe neam-lambda.exe; do
    found="$(find . -name "$b" -type f 2>/dev/null | head -1)"
    [ -n "$found" ] && BINS+=("$found")
done
[ ${#BINS[@]} -eq 0 ] && fail "No binaries found in archive"

info "Found ${#BINS[@]} binaries: $(printf '%s ' "${BINS[@]}" | sed 's|.*/||g')"

# Install — try INSTALL_DIR first, fall back to ~/.neam/bin
install_to() {
    local dir="$1"
    mkdir -p "$dir"
    cp -f "${BINS[@]}" "$dir/"
    chmod +x "$dir"/* 2>/dev/null || true
    INSTALL_DIR="$dir"
}

NEEDS_PATH_UPDATE=false

if [ -w "$INSTALL_DIR" ]; then
    info "Installing to ${INSTALL_DIR}..."
    install_to "$INSTALL_DIR"
else
    # Try sudo only if stdin is a terminal (interactive)
    if [ -t 0 ] && command -v sudo &>/dev/null; then
        info "Installing to ${INSTALL_DIR} (requires sudo)..."
        sudo mkdir -p "$INSTALL_DIR"
        sudo cp -f "${BINS[@]}" "$INSTALL_DIR/"
        sudo chmod +x "$INSTALL_DIR"/* 2>/dev/null || true
    else
        # Fall back to ~/.neam/bin (no sudo needed)
        INSTALL_DIR="$HOME/.neam/bin"
        info "Installing to ${INSTALL_DIR}..."
        install_to "$INSTALL_DIR"
        NEEDS_PATH_UPDATE=true
    fi
fi

# Add to PATH if needed
if [ "$NEEDS_PATH_UPDATE" = true ]; then
    SHELL_NAME="$(basename "${SHELL:-/bin/bash}")"
    case "$SHELL_NAME" in
        zsh)  RC_FILE="$HOME/.zshrc" ;;
        bash) RC_FILE="$HOME/.bashrc" ;;
        fish) RC_FILE="$HOME/.config/fish/config.fish" ;;
        *)    RC_FILE="$HOME/.profile" ;;
    esac

    if ! echo "$PATH" | tr ':' '\n' | grep -qx "$INSTALL_DIR"; then
        if [ "$SHELL_NAME" = "fish" ]; then
            PATH_LINE="fish_add_path $INSTALL_DIR"
        else
            PATH_LINE="export PATH=\"$INSTALL_DIR:\$PATH\""
        fi

        if [ -f "$RC_FILE" ] && grep -qF "$INSTALL_DIR" "$RC_FILE" 2>/dev/null; then
            info "PATH already configured in $RC_FILE"
        else
            echo "" >> "$RC_FILE"
            echo "# Neam" >> "$RC_FILE"
            echo "$PATH_LINE" >> "$RC_FILE"
            info "Added $INSTALL_DIR to PATH in $RC_FILE"
        fi

        # Also export for current verification step
        export PATH="$INSTALL_DIR:$PATH"
    fi
fi

# Verify
echo ""
for b in neamc neam neam-api neam-pkg neam-lambda neam-cli neam-lsp neam-dap neam-gym; do
    if command -v "$b" &>/dev/null; then
        ok "$b installed at $(command -v $b)"
    elif [ -f "${INSTALL_DIR}/$b" ]; then
        ok "$b installed to ${INSTALL_DIR}/$b"
    fi
done

echo ""
ok "Neam v${VERSION} installed successfully!"
echo ""
echo "  Quick start:"
echo "    neamc hello.neam -o hello.neamb   # Compile"
echo "    neam hello.neamb                  # Run"
echo "    neam-pkg init my-agent            # New project"
echo ""
if [ "$NEEDS_PATH_UPDATE" = true ]; then
    echo "  Run this to use neam now:"
    echo "    source ${RC_FILE}"
    echo ""
fi
echo "  Documentation: https://neam.dev"
echo "  GitHub:        https://github.com/${REPO}"
echo ""
