#!/bin/bash

# Build standalone mGBA environment with raylib rendering
# Usage: ./build_standalone.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUFFERLIB_ROOT="$SCRIPT_DIR/../../.."

# Paths
RAYLIB_DIR="$PUFFERLIB_ROOT/raylib-5.5_linux_amd64"
MGBA_INCLUDE="/usr/include"  # Adjust if mGBA is installed elsewhere

# Output
OUTPUT="$SCRIPT_DIR/mgba_standalone"

echo "Building standalone mGBA environment..."
echo "Raylib: $RAYLIB_DIR"

# Check if raylib exists
if [ ! -d "$RAYLIB_DIR" ]; then
    echo "Error: raylib not found at $RAYLIB_DIR"
    exit 1
fi

# Compile
gcc -DSTANDALONE_BUILD -DENABLE_VFS \
    -I"$RAYLIB_DIR/include" \
    -I"$MGBA_INCLUDE" \
    -L"$RAYLIB_DIR/lib" \
    -o "$OUTPUT" \
    "$SCRIPT_DIR/mgba.c" \
    -lmgba -lraylib -lm -lpthread -ldl -lrt \
    -Wl,-rpath,"$RAYLIB_DIR/lib" \
    -O2

if [ $? -eq 0 ]; then
    echo "Build successful: $OUTPUT"
    echo ""
    echo "Usage: $OUTPUT <rom_path>"
    echo ""
    echo "Controls:"
    echo "  Arrow keys: D-pad"
    echo "  Z/X: A button"
    echo "  A/S: B button"
    echo "  Enter: Start"
    echo "  Backspace: Select"
    echo "  F1: Save state"
    echo "  F2: Load state"
    echo "  R: Reset"
    echo "  ESC: Quit"
else
    echo "Build failed!"
    exit 1
fi
