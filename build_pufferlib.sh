#!/bin/bash
# build_pufferlib.sh — Build the PufferLib core once (no env-specific flags).
# Run once; environments compile separately with build_env.sh.
#
# Usage:
#   ./build_pufferlib.sh           # BF16 precision
#   ./build_pufferlib.sh --float   # FP32 precision
#   ./build_pufferlib.sh --cpu     # CPU fallback (no CUDA, for --slowly torch mode)
set -e

MODE=gpu
PRECISION=""

for arg in "$@"; do
    case $arg in
        --float) PRECISION="-DPRECISION_FLOAT" ;;
        --cpu)   MODE=cpu; PRECISION="-DPRECISION_FLOAT" ;;
        *) echo "Unknown argument: $arg" && exit 1 ;;
    esac
done

# ---- Common setup (mirrors build.sh) ----------------------------------------

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

CUDA_HOME=${CUDA_HOME:-${CUDA_PATH:-$(dirname "$(dirname "$(which nvcc)")")}}

PYTHON_INCLUDE=$(python -c "import sysconfig; print(sysconfig.get_path('include'))")
PYBIND_INCLUDE=$(python -c "import pybind11; print(pybind11.get_include())")
NUMPY_INCLUDE=$(python -c "import numpy; print(numpy.get_include())")
EXT_SUFFIX=$(python -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")
OUTPUT="pufferlib/_C${EXT_SUFFIX}"

ARCH=${NVCC_ARCH:-native}
NVCC="ccache $CUDA_HOME/bin/nvcc"
mkdir -p build

CUDNN_IFLAG=${CUDNN_HOME:+-I$CUDNN_HOME/include}
NCCL_IFLAG=${NCCL_HOME:+-I$NCCL_HOME/include}
CUDNN_LFLAG=${CUDNN_HOME:+-L$CUDNN_HOME/lib64}
NCCL_LFLAG=${NCCL_HOME:+-L$NCCL_HOME/lib}

if [ "$MODE" = "gpu" ]; then
    echo "Building PufferLib CUDA core ($ARCH)..."
    $NVCC -c -arch=$ARCH -Xcompiler -fPIC \
        -Xcompiler=-D_GLIBCXX_USE_CXX11_ABI=1 \
        -Xcompiler=-DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION \
        -Xcompiler=-DPLATFORM_DESKTOP \
        -std=c++17 \
        -I. -Isrc \
        -I$PYTHON_INCLUDE -I$PYBIND_INCLUDE -I$NUMPY_INCLUDE \
        -I$CUDA_HOME/include $CUDNN_IFLAG $NCCL_IFLAG -I$RAYLIB_NAME/include \
        -Xcompiler=-fopenmp \
        $PRECISION -O3 \
        src/bindings.cu -o build/bindings_core.o

    ${CXX:-g++} -shared -fPIC -fopenmp \
        build/bindings_core.o "$RAYLIB_A" \
        -L$CUDA_HOME/lib64 $CUDNN_LFLAG $NCCL_LFLAG \
        -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand -lcudnn \
        -ldl -lm -lpthread \
        -o "$OUTPUT"

    echo "Built: $OUTPUT"

elif [ "$MODE" = "cpu" ]; then
    echo "Building PufferLib CPU core..."
    ${CXX:-g++} -c -fPIC -fopenmp \
        -D_GLIBCXX_USE_CXX11_ABI=1 \
        -DPLATFORM_DESKTOP \
        -std=c++17 \
        -I. -Isrc \
        -I$PYTHON_INCLUDE -I$PYBIND_INCLUDE \
        $PRECISION -O3 \
        src/bindings_cpu.cpp -o build/bindings_cpu_core.o

    ${CXX:-g++} -shared -fPIC -fopenmp \
        build/bindings_cpu_core.o "$RAYLIB_A" \
        -lm -lpthread -ldl \
        -o "$OUTPUT"

    echo "Built: $OUTPUT"
fi
