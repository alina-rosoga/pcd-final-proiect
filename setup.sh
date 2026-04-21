#!/usr/bin/env bash
# =============================================================================
# setup.sh - PCD T31 Project Setup Script
# =============================================================================
# Automates:
#   1. Installing system dependencies (libgsoap-dev, libconfig-dev)
#   2. Building LibrosaC from source (if not already installed)
#   3. Running soapcpp2 to generate gSOAP bindings
#   4. Building all project binaries
#   5. Running clang-tidy
# =============================================================================

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIBROSA_INSTALL_DIR="${HOME}/.local"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[setup]${NC} $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
error() { echo -e "${RED}[error]${NC} $*" >&2; exit 1; }

# ---- Step 1: System packages -----------------------------------------------
info "Checking system packages..."

if command -v apt-get &>/dev/null; then
    MISSING=()
    dpkg -l libgsoap-dev  &>/dev/null || MISSING+=(libgsoap-dev)
    dpkg -l libconfig-dev &>/dev/null || MISSING+=(libconfig-dev)
    dpkg -l clang-tidy    &>/dev/null || MISSING+=(clang-tidy)
    dpkg -l cmake         &>/dev/null || MISSING+=(cmake)
    dpkg -l git           &>/dev/null || MISSING+=(git)

    if [ ${#MISSING[@]} -gt 0 ]; then
        info "Installing: ${MISSING[*]}"
        sudo apt-get update -qq
        sudo apt-get install -y -q "${MISSING[@]}"
    else
        info "All system packages present."
    fi
else
    warn "apt-get not found. Install manually: libgsoap-dev libconfig-dev clang-tidy"
fi

# ---- Step 2: LibrosaC -------------------------------------------------------
info "Checking LibrosaC..."

if pkg-config --exists rosacpp 2>/dev/null || \
   [ -f "${LIBROSA_INSTALL_DIR}/lib/librosacpp.so" ] || \
   [ -f "/usr/local/lib/librosacpp.so" ]; then
    info "LibrosaC already installed."
else
    warn "LibrosaC not found. Attempting to build from source..."
    TMPDIR_LIBROSA="$(mktemp -d)"
    cd "${TMPDIR_LIBROSA}"

    git clone --depth=1 https://github.com/librosa/librosa-cpp . 2>/dev/null || {
        warn "Could not clone LibrosaC. Install manually:"
        warn "  https://github.com/librosa/librosa-cpp"
        warn "Continuing build (linking will fail without LibrosaC)."
        cd "${PROJECT_DIR}"
        rm -rf "${TMPDIR_LIBROSA}"
    }

    if [ -f CMakeLists.txt ]; then
        mkdir -p build && cd build
        cmake .. \
            -DBUILD_C_BINDINGS=ON \
            -DCMAKE_INSTALL_PREFIX="${LIBROSA_INSTALL_DIR}" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=ON \
            2>/dev/null
        make -j"$(nproc)"
        make install
        info "LibrosaC installed to ${LIBROSA_INSTALL_DIR}"

        # Add to pkg-config / ldconfig search paths
        export PKG_CONFIG_PATH="${LIBROSA_INSTALL_DIR}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
        export LD_LIBRARY_PATH="${LIBROSA_INSTALL_DIR}/lib:${LD_LIBRARY_PATH:-}"

        # Persist in ~/.bashrc
        if ! grep -q "LIBROSA_INSTALL_DIR" "${HOME}/.bashrc"; then
            cat >> "${HOME}/.bashrc" << EOF

# LibrosaC (PCD T31)
export PKG_CONFIG_PATH="${LIBROSA_INSTALL_DIR}/lib/pkgconfig:\${PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="${LIBROSA_INSTALL_DIR}/lib:\${LD_LIBRARY_PATH}"
EOF
            info "Added LibrosaC paths to ~/.bashrc"
        fi

        cd "${PROJECT_DIR}"
        rm -rf "${TMPDIR_LIBROSA}"
    fi
fi

# ---- Step 3: Generate gSOAP bindings ----------------------------------------
cd "${PROJECT_DIR}"
info "Generating gSOAP bindings from src/proto.h..."

if command -v soapcpp2 &>/dev/null; then
    soapcpp2 -c -S -Isrc src/proto.h -d src/ 2>/dev/null && \
        info "gSOAP bindings generated." || \
        warn "soapcpp2 reported warnings (may be OK)."
else
    error "soapcpp2 not found. Install: sudo apt install libgsoap-dev"
fi

# ---- Step 4: Create output directory ----------------------------------------
mkdir -p output
info "Output directory: ${PROJECT_DIR}/output"

# ---- Step 5: Build ----------------------------------------------------------
info "Building project..."
make -j"$(nproc)" && info "Build successful." || error "Build failed."

# ---- Step 6: clang-tidy -----------------------------------------------------
info "Running clang-tidy..."
make tidy && info "clang-tidy: no issues." || warn "clang-tidy reported issues."

# ---- Done -------------------------------------------------------------------
echo ""
info "=== Setup complete ==="
echo ""
echo "  Start server:  ./serverds -c config/server.cfg -v"
echo "  SOAP client:   ./inetclient -s http://localhost:8080/soap -f audio.wav"
echo "  M1 demo:       ./demo_milestone1 -c config/server.cfg -f audio.wav"
echo "  Raw TCP:       ./inetds2   (server) | ./inetsample2 -f audio.wav"
echo ""
