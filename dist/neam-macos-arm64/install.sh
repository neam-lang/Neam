#!/bin/bash
# Neam Installation Script

INSTALL_DIR="${1:-/usr/local/bin}"

echo "Installing Neam v0.7.1 to $INSTALL_DIR..."

if [ ! -w "$INSTALL_DIR" ]; then
    echo "Need sudo access to install to $INSTALL_DIR"
    SUDO="sudo"
else
    SUDO=""
fi

for exe in neamc neam neam-cli neam-api neam-pkg neam-lsp neam-dap neam-gym; do
    if [ -f "$exe" ]; then
        $SUDO cp "$exe" "$INSTALL_DIR/"
        $SUDO chmod +x "$INSTALL_DIR/$exe"
        echo "  Installed: $exe"
    fi
done

echo ""
echo "Installation complete!"
echo "Run 'neamc --help' to compile programs."
echo "Run 'neam-cli' for the interactive REPL with autocomplete."
