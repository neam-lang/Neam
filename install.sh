#!/bin/bash
#===============================================================================
# Neam v0.6.2 - macOS/Linux Quick Install Script
#===============================================================================
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/neam-lang/Neam/main/install.sh | bash
#
# Or download and run:
#   chmod +x install.sh && ./install.sh
#===============================================================================

set -e

VERSION="v0.6.2"
INSTALL_DIR="$HOME/.neam"
BIN_DIR="$INSTALL_DIR/bin"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Neam ${VERSION} - Quick Install${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Detect OS and architecture
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Darwin)
        case "$ARCH" in
            arm64)
                PLATFORM="macos-arm64"
                ;;
            x86_64)
                PLATFORM="macos-x64"
                ;;
            *)
                echo -e "${RED}Error: Unsupported architecture: $ARCH${NC}"
                exit 1
                ;;
        esac
        ;;
    Linux)
        case "$ARCH" in
            x86_64)
                PLATFORM="linux-x64"
                ;;
            aarch64)
                PLATFORM="linux-arm64"
                ;;
            *)
                echo -e "${RED}Error: Unsupported architecture: $ARCH${NC}"
                exit 1
                ;;
        esac
        ;;
    *)
        echo -e "${RED}Error: Unsupported OS: $OS${NC}"
        echo "For Windows, please use install.bat or download from:"
        echo "https://github.com/neam-lang/Neam/releases/latest"
        exit 1
        ;;
esac

DOWNLOAD_URL="https://github.com/neam-lang/Neam/releases/download/${VERSION}/neam-${VERSION}-${PLATFORM}.tar.gz"

echo -e "${GREEN}[1/5]${NC} Detected: $OS ($ARCH)"
echo ""

# Create install directory
echo -e "${GREEN}[2/5]${NC} Creating install directory: $INSTALL_DIR"
mkdir -p "$BIN_DIR"

# Download release
echo -e "${GREEN}[3/5]${NC} Downloading Neam ${VERSION}..."
TEMP_FILE=$(mktemp)
if command -v curl &> /dev/null; then
    curl -fsSL "$DOWNLOAD_URL" -o "$TEMP_FILE" || {
        echo -e "${RED}Error: Download failed. Check the release URL:${NC}"
        echo "$DOWNLOAD_URL"
        exit 1
    }
elif command -v wget &> /dev/null; then
    wget -q "$DOWNLOAD_URL" -O "$TEMP_FILE" || {
        echo -e "${RED}Error: Download failed.${NC}"
        exit 1
    }
else
    echo -e "${RED}Error: Neither curl nor wget found. Please install one.${NC}"
    exit 1
fi

# Extract directly to bin directory
echo -e "${GREEN}[4/5]${NC} Extracting to $BIN_DIR..."
tar -xzf "$TEMP_FILE" -C "$BIN_DIR"
rm -f "$TEMP_FILE"

# Make binaries executable
chmod +x "$BIN_DIR"/* 2>/dev/null || true

# Detect shell and update PATH
echo -e "${GREEN}[5/5]${NC} Configuring PATH..."

SHELL_NAME="$(basename "$SHELL")"
case "$SHELL_NAME" in
    zsh)
        RC_FILE="$HOME/.zshrc"
        ;;
    bash)
        if [[ "$OS" == "Darwin" ]]; then
            RC_FILE="$HOME/.bash_profile"
        else
            RC_FILE="$HOME/.bashrc"
        fi
        ;;
    *)
        RC_FILE="$HOME/.profile"
        ;;
esac

# Add to PATH if not already present
if ! grep -q "NEAM_HOME" "$RC_FILE" 2>/dev/null; then
    echo "" >> "$RC_FILE"
    echo "# Neam Language" >> "$RC_FILE"
    echo "export NEAM_HOME=\"$INSTALL_DIR\"" >> "$RC_FILE"
    echo "export PATH=\"\$NEAM_HOME/bin:\$PATH\"" >> "$RC_FILE"
    echo -e "  Added to ${YELLOW}$RC_FILE${NC}"
else
    echo -e "  PATH already configured in ${YELLOW}$RC_FILE${NC}"
fi

# Export for current session
export NEAM_HOME="$INSTALL_DIR"
export PATH="$BIN_DIR:$PATH"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Installation Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Verify installation
echo "Verifying installation..."
if [ -f "$BIN_DIR/neamc" ]; then
    echo -e "  ${GREEN}[OK]${NC} neamc (compiler)"
else
    echo -e "  ${RED}[FAIL]${NC} neamc not found"
fi

if [ -f "$BIN_DIR/neam" ]; then
    echo -e "  ${GREEN}[OK]${NC} neam (runtime)"
else
    echo -e "  ${RED}[FAIL]${NC} neam not found"
fi

if [ -f "$BIN_DIR/neam-cli" ]; then
    echo -e "  ${GREEN}[OK]${NC} neam-cli (REPL)"
else
    echo -e "  ${RED}[FAIL]${NC} neam-cli not found"
fi

echo ""
echo -e "${YELLOW}IMPORTANT:${NC} Restart your terminal or run:"
echo -e "  ${BLUE}source $RC_FILE${NC}"
echo ""
echo -e "${BLUE}Quick Start:${NC}"
echo ""
echo "  # Create a new project"
echo "  neam-pkg init my-agent"
echo "  cd my-agent"
echo ""
echo "  # Or run Hello World directly"
echo "  echo '{ emit \"Hello from Neam!\"; }' > hello.neam"
echo "  neamc hello.neam -o hello.neamb"
echo "  neam hello.neamb"
echo ""
echo "  # Start interactive REPL"
echo "  neam-cli"
echo ""
echo -e "${BLUE}For AI Agents:${NC}"
echo ""
echo "  # Option 1: Local LLM with Ollama (free)"
echo "  brew install ollama      # or download from ollama.com"
echo "  ollama pull llama3.2:3b"
echo ""
echo "  # Option 2: OpenAI API"
echo "  export OPENAI_API_KEY=\"sk-...\""
echo ""
echo -e "Documentation: ${BLUE}https://github.com/neam-lang/Neam${NC}"
echo ""
