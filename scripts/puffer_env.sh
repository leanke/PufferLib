# Sourced by run.sh and menu.sh: CUDA device/arch and the NCCL library path ./puffer needs.
export CUDA_VISIBLE_DEVICES=0
export NVCC_ARCH=sm_86

NCCL_LIB=$(python -c 'import nvidia.nccl, os; print(os.path.join(nvidia.nccl.__path__[0], "lib"))')
export LD_LIBRARY_PATH="$NCCL_LIB:${LD_LIBRARY_PATH:-}"
