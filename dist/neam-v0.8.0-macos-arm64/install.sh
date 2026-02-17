#!/bin/bash
# Neam v0.8.0 Installation Script
INSTALL_DIR="${1:-/usr/local/bin}"
STDLIB_DIR="${2:-/usr/local/share/neam/stdlib}"
echo "Installing Neam v0.8.0 to $INSTALL_DIR..."
if [ ! -w "$INSTALL_DIR" ]; then
    echo "Need sudo access to install to $INSTALL_DIR"
    SUDO="sudo"
else
    SUDO=""
fi
$SUDO mkdir -p "$INSTALL_DIR" "$STDLIB_DIR"
for exe in neamc neam neam-cli neam-api neam-pkg neam-lambda neam-forge neam-lsp neam-dap neam-gym; do
    if [ -f "$exe" ]; then
        $SUDO cp "$exe" "$INSTALL_DIR/"
        $SUDO chmod +x "$INSTALL_DIR/$exe"
        echo "  Installed: $exe"
    fi
done
if [ -d "stdlib" ]; then
    $SUDO cp -r stdlib/* "$STDLIB_DIR/" 2>/dev/null
    echo "  Installed: stdlib -> $STDLIB_DIR"
fi
echo ""
echo "Installation complete!"
echo "  neamc --version     # Check compiler version"
echo "  neamc --help        # Show compiler help"
echo "  neam-cli            # Interactive REPL"
echo "  neam-api --port 8080  # Start API server"
echo "  neam-forge --help   # Forge agent CLI"
