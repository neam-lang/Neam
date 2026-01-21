#!/bin/bash
# Neam One-Click Installer
# Usage: curl -fsSL https://github.com/neam-lang/Neam/releases/download/v0.1/install.sh | bash

set -e

NEAM_VERSION="v0.1"
INSTALL_DIR="${NEAM_INSTALL_DIR:-/usr/local/bin}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_banner() {
    echo -e "${BLUE}"
    echo "  _   _                      "
    echo " | \ | | ___  __ _ _ __ ___  "
    echo " |  \| |/ _ \/ _\` | '_ \` _ \ "
    echo " | |\  |  __/ (_| | | | | | |"
    echo " |_| \_|\___|\__,_|_| |_| |_|"
    echo -e "${NC}"
    echo -e "${GREEN}Neam Programming Language Installer${NC}"
    echo ""
}

detect_platform() {
    local os=$(uname -s | tr '[:upper:]' '[:lower:]')
    local arch=$(uname -m)

    case "$os" in
        darwin)
            case "$arch" in
                arm64|aarch64) echo "macos-arm64" ;;
                x86_64) echo "macos-x64" ;;
                *) echo "unsupported" ;;
            esac
            ;;
        linux)
            case "$arch" in
                x86_64) echo "linux-x64" ;;
                aarch64|arm64) echo "linux-arm64" ;;
                *) echo "unsupported" ;;
            esac
            ;;
        *)
            echo "unsupported"
            ;;
    esac
}

check_dependencies() {
    local missing=()

    for cmd in curl tar; do
        if ! command -v "$cmd" &> /dev/null; then
            missing+=("$cmd")
        fi
    done

    if [ ${#missing[@]} -ne 0 ]; then
        echo -e "${RED}Error: Missing required dependencies: ${missing[*]}${NC}"
        exit 1
    fi
}

main() {
    print_banner

    echo -e "${BLUE}[1/5]${NC} Detecting platform..."
    local platform=$(detect_platform)

    if [ "$platform" = "unsupported" ]; then
        echo -e "${RED}Error: Unsupported platform $(uname -s)/$(uname -m)${NC}"
        echo "Please visit https://github.com/neam-lang/Neam/releases for manual installation."
        exit 1
    fi

    echo -e "       Platform: ${GREEN}$platform${NC}"

    echo -e "${BLUE}[2/5]${NC} Checking dependencies..."
    check_dependencies
    echo -e "       ${GREEN}All dependencies found${NC}"

    # Currently only macOS ARM64 is available
    if [ "$platform" != "macos-arm64" ]; then
        echo -e "${YELLOW}Warning: Only macOS ARM64 binaries are currently available.${NC}"
        echo "Please check https://github.com/neam-lang/Neam/releases for updates."
        exit 1
    fi

    local download_url="https://github.com/neam-lang/Neam/releases/download/${NEAM_VERSION}/neam-${platform}-v1.0.0.tar.gz"
    local tmp_dir=$(mktemp -d)
    local archive="$tmp_dir/neam.tar.gz"

    echo -e "${BLUE}[3/5]${NC} Downloading Neam ${NEAM_VERSION}..."
    if ! curl -fsSL "$download_url" -o "$archive"; then
        echo -e "${RED}Error: Failed to download Neam${NC}"
        rm -rf "$tmp_dir"
        exit 1
    fi
    echo -e "       ${GREEN}Download complete${NC}"

    echo -e "${BLUE}[4/5]${NC} Extracting..."
    tar -xzf "$archive" -C "$tmp_dir"
    echo -e "       ${GREEN}Extraction complete${NC}"

    echo -e "${BLUE}[5/5]${NC} Installing to $INSTALL_DIR..."

    local need_sudo=""
    # Check if we need sudo: directory doesn't exist and parent isn't writable, or directory exists but isn't writable
    if [ -d "$INSTALL_DIR" ]; then
        if [ ! -w "$INSTALL_DIR" ]; then
            need_sudo="sudo"
        fi
    else
        # Directory doesn't exist, check if we can create it
        local parent_dir=$(dirname "$INSTALL_DIR")
        if [ ! -w "$parent_dir" ]; then
            need_sudo="sudo"
        fi
    fi

    if [ -n "$need_sudo" ]; then
        echo -e "       ${YELLOW}Requires sudo access${NC}"
    fi

    # Ensure install directory exists
    $need_sudo mkdir -p "$INSTALL_DIR"

    local binaries=(neamc neam neam-cli neam-api neam-pkg neam-lsp neam-dap neam-gym)
    for bin in "${binaries[@]}"; do
        if [ -f "$tmp_dir/neam-${platform}/$bin" ]; then
            $need_sudo cp "$tmp_dir/neam-${platform}/$bin" "$INSTALL_DIR/"
            $need_sudo chmod +x "$INSTALL_DIR/$bin"
        fi
    done

    # Cleanup
    rm -rf "$tmp_dir"

    echo ""
    echo -e "${GREEN}✓ Neam ${NEAM_VERSION} installed successfully!${NC}"
    echo ""
    echo -e "Installed binaries:"
    echo -e "  ${BLUE}neamc${NC}     - Neam compiler"
    echo -e "  ${BLUE}neam${NC}      - Neam interpreter"
    echo -e "  ${BLUE}neam-cli${NC}  - Interactive REPL with autocomplete"
    echo -e "  ${BLUE}neam-api${NC}  - API server"
    echo -e "  ${BLUE}neam-pkg${NC}  - Package manager"
    echo -e "  ${BLUE}neam-lsp${NC}  - Language server (IDE support)"
    echo -e "  ${BLUE}neam-dap${NC}  - Debug adapter"
    echo -e "  ${BLUE}neam-gym${NC}  - Training environment"
    echo ""
    echo -e "Get started:"
    echo -e "  ${YELLOW}neamc --help${NC}    # Compile programs"
    echo -e "  ${YELLOW}neam-cli${NC}        # Interactive REPL"
    echo ""
    echo -e "Documentation: ${BLUE}https://neam.lovable.app${NC}"
    echo -e "GitHub: ${BLUE}https://github.com/neam-lang/Neam${NC}"
}

main "$@"
