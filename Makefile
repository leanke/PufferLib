SHELL := /bin/bash
PYTHON := python3
NUMPY_INCLUDE := $(shell $(PYTHON) -c "import numpy; print(numpy.get_include())")
OCEAN_DIR := pufferlib/ocean/mgba

DEBUG ?= 0
ifeq ($(DEBUG),1)
	OPT_FLAGS := -O0 -g -fsanitize=address,undefined,bounds,pointer-overflow,leak -fno-omit-frame-pointer
	LINK_OPT_FLAGS := -g -fsanitize=address,undefined,bounds,pointer-overflow,leak
else
	OPT_FLAGS := -O2 -flto
	LINK_OPT_FLAGS := -O2
endif

CFLAGS := -DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION -DPLATFORM_DESKTOP -I$(NUMPY_INCLUDE) -Wno-alloc-size-larger-than -Wno-implicit-function-declaration -fmax-errors=3 $(OPT_FLAGS) -DENABLE_VFS
LDFLAGS := -fwrapv -Bsymbolic-functions $(LINK_OPT_FLAGS) -lmgba

.PHONY: all clean help mgba

all: mgba

mgba: $(OCEAN_DIR)/binding.so


$(OCEAN_DIR)/binding.so: $(OCEAN_DIR)/binding.c 
	@echo "Compiling mGBA..."
	@cd $(OCEAN_DIR) && \
	$(PYTHON) -c "from distutils.core import setup, Extension; import numpy; \
	setup(ext_modules=[Extension('binding', ['binding.c', ], \
	    include_dirs=[numpy.get_include()], \
	    extra_compile_args='$(CFLAGS)'.split(), \
	    extra_link_args='$(LDFLAGS)'.split())])" build_ext --inplace

clean:
	@echo "Cleaning..."
	@find $(OCEAN_DIR) -name "*.so" -delete
	@find $(OCEAN_DIR) -name "build" -type d -exec rm -rf {} + 2>/dev/null || true

help:
	@echo "mGBA Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make                 - Build mgba binding"
	@echo "  make clean           - Clean environment"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1              - Build with debug symbols and sanitizers"

