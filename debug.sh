#!/bin/bash
# dont forget catch signal SIGSEGV
DEBUG=1 python setup.py build_ext --inplace
LD_PRELOAD=/usr/lib/gcc/x86_64-linux-gnu/13/libasan.so gdb \
    --args $(pyenv which python3) -m pufferlib.pufferl train $1
