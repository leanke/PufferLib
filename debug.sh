#!/bin/bash
# Debug script for mGBA environment
# Usage: ./debug.sh [test|train]
#
# For GDB debugging, remember to:
#   (gdb) catch signal SIGSEGV
#   (gdb) run

set -e

MODE="${1:-test}"
PYTHON=$(pyenv which python3)

echo "Building with DEBUG=1..."
DEBUG=1 make clean mgba

echo ""
echo "Running in debug mode with AddressSanitizer..."
echo "Mode: $MODE"
echo ""

if [ "$MODE" = "train" ]; then
    # Training mode - run the full training loop
    LD_PRELOAD=/usr/lib/gcc/x86_64-linux-gnu/13/libasan.so gdb \
        --args "$PYTHON" -m pufferlib.pufferl train puffer_mgba
elif [ "$MODE" = "test" ]; then
    # Test mode - just run the test script
    LD_PRELOAD=/usr/lib/gcc/x86_64-linux-gnu/13/libasan.so gdb \
        --args "$PYTHON" test_mgba.py 50
else
    # Custom command
    LD_PRELOAD=/usr/lib/gcc/x86_64-linux-gnu/13/libasan.so gdb \
        --args "$PYTHON" "$@"
fi
