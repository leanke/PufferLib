#!/bin/bash
# build_env.sh <env_name> — Build a standalone environment plugin .so.
# No PufferLib CUDA required; only needs pufferenv.h from src/.
#
# Usage:
#   ./build_env.sh cartpole
#   ./build_env.sh breakout
set -e

if [ -z "$1" ]; then
    echo "Usage: ./build_env.sh ENV_NAME"
    exit 1
fi
ENV="$1"
SRC_DIR="ocean/$ENV"
BINDING_SRC="$SRC_DIR/binding.c"

if [ ! -f "$BINDING_SRC" ]; then
    echo "Error: $BINDING_SRC not found"
    exit 1
fi

# ---- Download raylib if needed -----------------------------------------------
OS=$(uname -s)
RAYLIB_NAME='raylib-5.5_linux_amd64'
if [ "$OS" = "Darwin" ]; then RAYLIB_NAME='raylib-5.5_macos'; fi
RAYLIB_URL="https://github.com/raysan5/raylib/releases/download/5.5"
RAYLIB_A="$RAYLIB_NAME/lib/libraylib.a"

download() {
    local NAME="$1" URL="$2"
    if [ ! -d "$NAME" ]; then
        echo "Downloading $NAME..."
        if [[ "$NAME" == *zip ]]; then
            curl -L "$URL" -o "$NAME.zip" && unzip -q "$NAME.zip" -d "$NAME"
        else
            curl -L "$URL" -o "$NAME.tar.gz" && tar -xzf "$NAME.tar.gz"
        fi
    fi
}
download "$RAYLIB_NAME" "$RAYLIB_URL/$RAYLIB_NAME.tar.gz"

CC="${CC:-$(command -v ccache >/dev/null 2>&1 && echo 'ccache clang' || echo 'clang')}"

# ---- Per-env extra flags (optional) -----------------------------------------
EXTRA_CFLAGS=""
EXTRA_LDFLAGS=""
ENV_FLAGS="$SRC_DIR/build_flags.env"
if [ -f "$ENV_FLAGS" ]; then
    source "$ENV_FLAGS"
fi

OUTPUT="ocean/$ENV/${ENV}_env.so"

echo "Building $ENV plugin..."
${CC} -O2 -fPIC -shared \
    -I. -Isrc -I$SRC_DIR -Ivendor \
    -I$RAYLIB_NAME/include \
    -DPLATFORM_DESKTOP \
    -fopenmp \
    -fno-semantic-interposition \
    $EXTRA_CFLAGS \
    "$BINDING_SRC" \
    "$RAYLIB_A" \
    $EXTRA_LDFLAGS \
    -lm -lpthread \
    -o "$OUTPUT"

echo "Built: $OUTPUT"
