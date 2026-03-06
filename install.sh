#!/bin/bash
set -e

echo "═══════════════════════════════════════════════"
echo "  J.A.R.V.I.S. Plasmoid Installer v0.1.1"
echo "═══════════════════════════════════════════════"
echo

# Determine install prefix
PREFIX="${CMAKE_INSTALL_PREFIX:-/usr}"

echo "[1/4] Configuring with CMake..."
cmake -B build \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_C_FLAGS="-march=native -O3" \
    -DCMAKE_CXX_FLAGS="-march=native -O3" \
    -DCMAKE_BUILD_TYPE=Release

echo
echo "[2/4] Building..."
cmake --build build -j"$(nproc)"

echo
echo "[3/4] Installing (requires sudo)..."
sudo cmake --install build

echo
echo "[4/4] Updating icon cache..."
sudo gtk-update-icon-cache /usr/share/icons/hicolor/ 2>/dev/null || true
sudo update-icon-caches /usr/share/icons/hicolor/ 2>/dev/null || true

echo
echo "═══════════════════════════════════════════════"
echo "  Installation complete!"
echo "  Restart plasmashell to load the plasmoid:"
echo "    plasmashell --replace &"
echo "═══════════════════════════════════════════════"
