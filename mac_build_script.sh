#!/bin/bash
#===============================================================================
# Neam macOS Build & Release Script
#
# This script builds Neam from source on macOS and packages it for distribution.
# Supports both Apple Silicon (arm64) and Intel (x86_64) architectures.
#
# Usage:
#   ./mac_build_script.sh [OPTIONS]
#
# Options:
#   -v, --version VERSION   Set version number (default: 1.0.0)
#   -a, --arch ARCH         Target architecture: arm64, x86_64, universal (default: native)
#   -t, --test              Run tests after build
#   -c, --clean             Clean build directory before building
#   -r, --release           Create GitHub release after build
#   -s, --skip-tests        Skip building test targets (faster build)
#   -h, --help              Show this help message
#
# Examples:
#   ./mac_build_script.sh                           # Build for native architecture
#   ./mac_build_script.sh -v 1.0.0 -t              # Build v1.0.0 and run tests
#   ./mac_build_script.sh -a universal -c          # Clean build universal binary
#   ./mac_build_script.sh -v 1.0.0 -r              # Build and create GitHub release
#   ./mac_build_script.sh -s                       # Build without test targets
#
#===============================================================================

set -e  # Exit on error

#-------------------------------------------------------------------------------
# Setup Homebrew PATH (required for cmake, openssl, etc.)
#-------------------------------------------------------------------------------
if [ -d "/opt/homebrew/bin" ]; then
    # Apple Silicon
    export PATH="/opt/homebrew/bin:$PATH"
elif [ -d "/usr/local/bin" ]; then
    # Intel Mac
    export PATH="/usr/local/bin:$PATH"
fi

#-------------------------------------------------------------------------------
# Configuration
#-------------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="neam"
VERSION="1.0.0"
ARCH="native"
RUN_TESTS=false
CLEAN_BUILD=false
CREATE_RELEASE=false
SKIP_TESTS=false
BUILD_TYPE="Release"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

#-------------------------------------------------------------------------------
# Helper Functions
#-------------------------------------------------------------------------------
print_header() {
    echo ""
    echo -e "${BLUE}==============================================================================${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}==============================================================================${NC}"
    echo ""
}

print_step() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

show_help() {
    head -30 "$0" | tail -25
    exit 0
}

#-------------------------------------------------------------------------------
# Parse Arguments
#-------------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--version)
            VERSION="$2"
            shift 2
            ;;
        -a|--arch)
            ARCH="$2"
            shift 2
            ;;
        -t|--test)
            RUN_TESTS=true
            shift
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -r|--release)
            CREATE_RELEASE=true
            shift
            ;;
        -s|--skip-tests)
            SKIP_TESTS=true
            shift
            ;;
        -h|--help)
            show_help
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            ;;
    esac
done

#-------------------------------------------------------------------------------
# Determine Architecture
#-------------------------------------------------------------------------------
NATIVE_ARCH=$(uname -m)

if [ "$ARCH" = "native" ]; then
    ARCH="$NATIVE_ARCH"
fi

case $ARCH in
    arm64|x86_64)
        CMAKE_ARCH_FLAG="-DCMAKE_OSX_ARCHITECTURES=$ARCH"
        DIST_SUFFIX="macos-$ARCH"
        ;;
    universal)
        CMAKE_ARCH_FLAG="-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
        DIST_SUFFIX="macos-universal"
        ;;
    *)
        print_error "Invalid architecture: $ARCH. Use arm64, x86_64, or universal"
        exit 1
        ;;
esac

#-------------------------------------------------------------------------------
# Print Configuration
#-------------------------------------------------------------------------------
print_header "Neam macOS Build Script"

echo "Configuration:"
echo "  Project:      $PROJECT_NAME"
echo "  Version:      $VERSION"
echo "  Architecture: $ARCH"
echo "  Build Type:   $BUILD_TYPE"
echo "  Run Tests:    $RUN_TESTS"
echo "  Skip Tests:   $SKIP_TESTS"
echo "  Clean Build:  $CLEAN_BUILD"
echo "  Create Release: $CREATE_RELEASE"
echo "  Script Dir:   $SCRIPT_DIR"
echo ""

#-------------------------------------------------------------------------------
# Check Prerequisites
#-------------------------------------------------------------------------------
print_header "Checking Prerequisites"

# Check for required tools
MISSING_TOOLS=()

if ! command -v cmake &> /dev/null; then
    MISSING_TOOLS+=("cmake")
fi

if ! command -v git &> /dev/null; then
    MISSING_TOOLS+=("git")
fi

if ! command -v brew &> /dev/null; then
    print_warning "Homebrew not found. OpenSSL detection may fail."
else
    print_step "Homebrew found"
fi

if [ ${#MISSING_TOOLS[@]} -ne 0 ]; then
    print_error "Missing required tools: ${MISSING_TOOLS[*]}"
    echo ""
    echo "Install with:"
    echo "  brew install ${MISSING_TOOLS[*]}"
    exit 1
fi

print_step "CMake found: $(cmake --version | head -1)"
print_step "Git found: $(git --version)"

# Check for OpenSSL
if command -v brew &> /dev/null; then
    OPENSSL_ROOT=$(brew --prefix openssl 2>/dev/null || echo "")
    if [ -n "$OPENSSL_ROOT" ] && [ -d "$OPENSSL_ROOT" ]; then
        print_step "OpenSSL found: $OPENSSL_ROOT"
        CMAKE_OPENSSL_FLAG="-DOPENSSL_ROOT_DIR=$OPENSSL_ROOT"
    else
        print_warning "OpenSSL not found via Homebrew. Trying system OpenSSL."
        CMAKE_OPENSSL_FLAG=""
    fi
else
    CMAKE_OPENSSL_FLAG=""
fi

# Check for curl development headers
if ! pkg-config --exists libcurl 2>/dev/null; then
    print_warning "libcurl pkg-config not found. Build may fail."
    echo "  Install with: brew install curl"
fi

#-------------------------------------------------------------------------------
# Clean Build (if requested)
#-------------------------------------------------------------------------------
if [ "$CLEAN_BUILD" = true ]; then
    print_header "Cleaning Build Directory"

    if [ -d "$SCRIPT_DIR/build" ]; then
        rm -rf "$SCRIPT_DIR/build"
        print_step "Removed build directory"
    fi

    if [ -d "$SCRIPT_DIR/dist" ]; then
        rm -rf "$SCRIPT_DIR/dist"
        print_step "Removed dist directory"
    fi
fi

#-------------------------------------------------------------------------------
# Create Build Directory
#-------------------------------------------------------------------------------
print_header "Configuring Build"

mkdir -p "$SCRIPT_DIR/build"
cd "$SCRIPT_DIR/build"

# Configure CMake
echo "Running CMake configure..."

if [ "$SKIP_TESTS" = true ]; then
    CMAKE_TESTING_FLAG="-DBUILD_TESTING=OFF"
else
    CMAKE_TESTING_FLAG="-DBUILD_TESTING=ON"
fi

cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    $CMAKE_ARCH_FLAG \
    $CMAKE_OPENSSL_FLAG \
    $CMAKE_TESTING_FLAG \
    -Wno-dev

print_step "CMake configuration complete"

#-------------------------------------------------------------------------------
# Build
#-------------------------------------------------------------------------------
print_header "Building Neam"

# Get number of CPU cores
if [ "$(uname)" = "Darwin" ]; then
    NUM_CORES=$(sysctl -n hw.ncpu)
else
    NUM_CORES=$(nproc)
fi

# Define core executables to build
CORE_TARGETS="neamc neam neam-cli neam-api neam-pkg neam-lambda neam-lsp neam-dap neam-gym"

echo "Building with $NUM_CORES parallel jobs..."

if [ "$SKIP_TESTS" = true ]; then
    # Build only core targets (faster, avoids test compilation issues)
    echo "Building core targets only (tests skipped)..."
    BUILD_FAILED=false
    for target in $CORE_TARGETS; do
        echo "  Building $target..."
        if ! cmake --build . --target "$target" --parallel "$NUM_CORES" 2>&1; then
            print_warning "Failed to build $target"
            BUILD_FAILED=true
        fi
    done
    if [ "$BUILD_FAILED" = true ]; then
        print_warning "Some targets failed to build, continuing with available executables"
    fi
else
    # Build all targets (including tests)
    # Use || true to continue even if some test targets fail
    if ! cmake --build . --parallel "$NUM_CORES" 2>&1; then
        print_warning "Full build had some failures (likely test targets)"
        print_step "Attempting to build core executables individually..."
        for target in $CORE_TARGETS; do
            if [ ! -f "$target" ]; then
                echo "  Building $target..."
                cmake --build . --target "$target" --parallel "$NUM_CORES" 2>&1 || true
            fi
        done
    fi
fi

print_step "Build complete"

# List built executables
echo ""
echo "Built executables:"
BUILT_COUNT=0
for exe in neamc neam neam-cli neam-api neam-pkg neam-lambda neam-lsp neam-dap neam-gym; do
    if [ -f "$exe" ]; then
        SIZE=$(ls -lh "$exe" | awk '{print $5}')
        echo "  - $exe ($SIZE)"
        ((BUILT_COUNT++))
    else
        print_warning "Not built: $exe"
    fi
done

if [ "$BUILT_COUNT" -eq 0 ]; then
    print_error "No executables were built. Build failed."
    exit 1
fi

print_step "Built $BUILT_COUNT executables"

#-------------------------------------------------------------------------------
# Run Tests (if requested)
#-------------------------------------------------------------------------------
if [ "$RUN_TESTS" = true ]; then
    print_header "Running Tests"

    ctest --output-on-failure --parallel "$NUM_CORES" || {
        print_error "Some tests failed"
        echo "Continue anyway? (y/n)"
        read -r response
        if [ "$response" != "y" ]; then
            exit 1
        fi
    }

    print_step "Tests complete"
fi

#-------------------------------------------------------------------------------
# Package Distribution
#-------------------------------------------------------------------------------
print_header "Packaging Distribution"

DIST_NAME="${PROJECT_NAME}-${DIST_SUFFIX}"
DIST_DIR="$SCRIPT_DIR/dist/$DIST_NAME"
ARCHIVE_NAME="${DIST_NAME}-v${VERSION}.tar.gz"

# Create dist directory
mkdir -p "$DIST_DIR"

# Copy executables
echo "Copying executables..."
EXECUTABLES=(neamc neam neam-cli neam-api neam-pkg neam-lambda neam-lsp neam-dap neam-gym)
COPIED_COUNT=0

for exe in "${EXECUTABLES[@]}"; do
    if [ -f "$SCRIPT_DIR/build/$exe" ]; then
        cp "$SCRIPT_DIR/build/$exe" "$DIST_DIR/"
        chmod +x "$DIST_DIR/$exe"
        ((COPIED_COUNT++))
    else
        print_warning "Executable not found: $exe"
    fi
done

print_step "Copied $COPIED_COUNT executables"

# Copy documentation
echo "Copying documentation..."
[ -f "$SCRIPT_DIR/readme.md" ] && cp "$SCRIPT_DIR/readme.md" "$DIST_DIR/"
[ -f "$SCRIPT_DIR/README.md" ] && cp "$SCRIPT_DIR/README.md" "$DIST_DIR/"
[ -f "$SCRIPT_DIR/BUILD_README.md" ] && cp "$SCRIPT_DIR/BUILD_README.md" "$DIST_DIR/"
[ -f "$SCRIPT_DIR/LICENSE" ] && cp "$SCRIPT_DIR/LICENSE" "$DIST_DIR/"

print_step "Copied documentation"

# Create version file
echo "Neam v${VERSION}" > "$DIST_DIR/VERSION"
echo "Built: $(date)" >> "$DIST_DIR/VERSION"
echo "Architecture: $ARCH" >> "$DIST_DIR/VERSION"
echo "Build Type: $BUILD_TYPE" >> "$DIST_DIR/VERSION"

# Create install script
cat > "$DIST_DIR/install.sh" << 'INSTALL_SCRIPT'
#!/bin/bash
# Neam Installation Script

INSTALL_DIR="${1:-/usr/local/bin}"

echo "Installing Neam to $INSTALL_DIR..."

if [ ! -w "$INSTALL_DIR" ]; then
    echo "Need sudo access to install to $INSTALL_DIR"
    SUDO="sudo"
else
    SUDO=""
fi

for exe in neamc neam neam-cli neam-api neam-pkg neam-lambda neam-lsp neam-dap neam-gym; do
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
INSTALL_SCRIPT
chmod +x "$DIST_DIR/install.sh"

print_step "Created install script"

# Create archive
echo "Creating archive..."
cd "$SCRIPT_DIR/dist"
tar -czvf "$ARCHIVE_NAME" "$DIST_NAME" > /dev/null

print_step "Created archive: $ARCHIVE_NAME"

# Generate checksum
CHECKSUM=$(shasum -a 256 "$ARCHIVE_NAME" | awk '{print $1}')
echo "$CHECKSUM  $ARCHIVE_NAME" > "$ARCHIVE_NAME.sha256"

print_step "Generated checksum: $CHECKSUM"

#-------------------------------------------------------------------------------
# Create GitHub Release (if requested)
#-------------------------------------------------------------------------------
if [ "$CREATE_RELEASE" = true ]; then
    print_header "Creating GitHub Release"

    if ! command -v gh &> /dev/null; then
        print_error "GitHub CLI (gh) not found. Install with: brew install gh"
        print_warning "Skipping GitHub release creation"
    else
        # Check if logged in
        if ! gh auth status &> /dev/null; then
            print_error "Not logged into GitHub CLI. Run: gh auth login"
            print_warning "Skipping GitHub release creation"
        else
            TAG="v${VERSION}"

            # Check if tag exists
            if git rev-parse "$TAG" &> /dev/null; then
                print_warning "Tag $TAG already exists"
            else
                echo "Creating tag $TAG..."
                git tag -a "$TAG" -m "Release $TAG"
                git push origin "$TAG"
                print_step "Created and pushed tag: $TAG"
            fi

            # Create release
            echo "Creating GitHub release..."
            gh release create "$TAG" \
                "$SCRIPT_DIR/dist/$ARCHIVE_NAME" \
                "$SCRIPT_DIR/dist/$ARCHIVE_NAME.sha256" \
                --title "Neam $TAG" \
                --notes "## Neam $TAG

### Downloads
- **macOS ($ARCH)**: \`$ARCHIVE_NAME\`

### Installation
\`\`\`bash
tar -xzf $ARCHIVE_NAME
cd $DIST_NAME
./install.sh  # Or copy executables manually
\`\`\`

### Quick Start
\`\`\`bash
# Compile and run a Neam program
neamc hello.neam -o hello.neamb
neam hello.neamb
\`\`\`

### Checksums
\`\`\`
$(cat "$SCRIPT_DIR/dist/$ARCHIVE_NAME.sha256")
\`\`\`
" \
                --draft

            print_step "Created draft release: $TAG"
            echo ""
            echo "Visit GitHub to review and publish the release"
        fi
    fi
fi

#-------------------------------------------------------------------------------
# Summary
#-------------------------------------------------------------------------------
print_header "Build Complete!"

echo "Summary:"
echo "  Version:      $VERSION"
echo "  Architecture: $ARCH"
echo "  Distribution: $SCRIPT_DIR/dist/$ARCHIVE_NAME"
echo "  Checksum:     $CHECKSUM"
echo ""
echo "Package contents:"
ls -la "$DIST_DIR"
echo ""
echo "Archive size: $(ls -lh "$SCRIPT_DIR/dist/$ARCHIVE_NAME" | awk '{print $5}')"
echo ""
echo "To distribute:"
echo "  1. Upload $SCRIPT_DIR/dist/$ARCHIVE_NAME to GitHub Releases"
echo "  2. Or share via Google Drive, Dropbox, etc."
echo ""
echo "Users can install with:"
echo "  tar -xzf $ARCHIVE_NAME"
echo "  cd $DIST_NAME"
echo "  ./install.sh"
echo ""
print_step "Done!"
