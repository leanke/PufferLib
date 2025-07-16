SHELL := /bin/bash

# python
PYTHON := python3
SYSTEM := $(shell uname -s)
NUMPY_INCLUDE := $(shell $(PYTHON) -c "import numpy; print(numpy.get_include())")

# lib/incl
RAYLIB ?= 1
LIBS ?=
INCLUDES ?=

# raylib
RAYLIB_VERSION := 5.5
ifeq ($(SYSTEM),Darwin)
    RAYLIB_NAME := raylib-$(RAYLIB_VERSION)_macos
    RAYLIB_EXT := .tar.gz
    PLATFORM_FLAGS := -framework Cocoa -framework OpenGL -framework IOKit
    COMPILE_FLAGS := -Wno-error=int-conversion -Wno-error=incompatible-function-pointer-types -Wno-error=implicit-function-declaration
else
    RAYLIB_NAME := raylib-$(RAYLIB_VERSION)_linux_amd64
    RAYLIB_EXT := .tar.gz
    PLATFORM_FLAGS := -Bsymbolic-functions
    COMPILE_FLAGS := -Wno-alloc-size-larger-than -Wno-implicit-function-declaration -fmax-errors=3
endif
RAYLIB_DIR := $(RAYLIB_NAME)
RAYLIB_LIB := $(RAYLIB_DIR)/lib/libraylib.a
RAYLIB_INCLUDE := $(RAYLIB_DIR)/include
ROOT_DIR := $(shell pwd)
RAYLIB_LIB_ABS := $(ROOT_DIR)/$(RAYLIB_LIB)
RAYLIB_INCLUDE_ABS := $(ROOT_DIR)/$(RAYLIB_INCLUDE)

# lib/incl cond
ifeq ($(RAYLIB),1)
    REQUIRED_LIBS := $(RAYLIB_LIB_ABS)
    REQUIRED_INCLUDES := $(RAYLIB_INCLUDE_ABS)
    REQUIRED_TARGETS := raylib
else
    REQUIRED_LIBS :=
    REQUIRED_INCLUDES :=
    REQUIRED_TARGETS :=
endif
ifneq ($(LIBS),)
    REQUIRED_LIBS += $(LIBS)
endif
ifneq ($(INCLUDES),)
    REQUIRED_INCLUDES += $(INCLUDES)
endif

COMMON_FLAGS := -DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION -DPLATFORM_DESKTOP
INCLUDE_FLAGS := -I$(NUMPY_INCLUDE) $(addprefix -I,$(REQUIRED_INCLUDES))
LINK_FLAGS := -fwrapv $(PLATFORM_FLAGS) $(REQUIRED_LIBS)

# envs
OCEAN_DIR := pufferlib/ocean
ENV_DIRS := $(shell find $(OCEAN_DIR) -name "binding.c" -exec dirname {} \;)
ENV_NAMES := $(notdir $(ENV_DIRS))

# debugging
DEBUG ?= 0
ifeq ($(DEBUG),1)
    OPT_FLAGS := -O0 -g -fsanitize=address,undefined,bounds,pointer-overflow,leak -fno-omit-frame-pointer
    LINK_OPT_FLAGS := -g -fsanitize=address,undefined,bounds,pointer-overflow,leak
else
    OPT_FLAGS := -O2 -flto
    LINK_OPT_FLAGS := -O2
endif

# comp
CFLAGS := $(COMMON_FLAGS) $(INCLUDE_FLAGS) $(COMPILE_FLAGS) $(OPT_FLAGS)
LDFLAGS := $(LINK_FLAGS) $(LINK_OPT_FLAGS)

.PHONY: all raylib clean clean-raylib clean-envs clean-all list help $(ENV_NAMES)

all: $(REQUIRED_TARGETS) $(ENV_NAMES)

raylib: $(RAYLIB_LIB)

$(RAYLIB_LIB):
	@echo "Downloading raylib..."
	@if [ ! -d "$(RAYLIB_DIR)" ]; then \
		curl -L -o "$(RAYLIB_NAME)$(RAYLIB_EXT)" "https://github.com/raysan5/raylib/releases/download/$(RAYLIB_VERSION)/$(RAYLIB_NAME)$(RAYLIB_EXT)"; \
		tar -xzf "$(RAYLIB_NAME)$(RAYLIB_EXT)"; \
		rm "$(RAYLIB_NAME)$(RAYLIB_EXT)"; \
	fi
	@if [ ! -f "$(RAYLIB_INCLUDE)/rlights.h" ]; then \
		echo "Downloading rlights.h..."; \
		curl -L -o "$(RAYLIB_INCLUDE)/rlights.h" "https://raw.githubusercontent.com/raysan5/raylib/refs/heads/master/examples/shaders/rlights.h"; \
	fi

# envs and binding
$(ENV_NAMES): %: $(OCEAN_DIR)/%/binding.so
$(OCEAN_DIR)/%/binding.so: $(OCEAN_DIR)/%/binding.c $(REQUIRED_TARGETS)
	@echo "Compiling $*..."
	@cd $(OCEAN_DIR)/$* && \
	$(PYTHON) -c "from distutils.core import setup, Extension; import numpy; \
	setup(ext_modules=[Extension('binding', ['binding.c'], \
		include_dirs=[numpy.get_include()]$(if $(REQUIRED_INCLUDES), + ['$(REQUIRED_INCLUDES)'],), \
		extra_compile_args='$(CFLAGS)'.split(), \
		extra_link_args='$(LDFLAGS)'.split())])" build_ext --inplace

clean: clean-envs

clean-envs:
	@echo "Cleaning compiled environments..."
	@find $(OCEAN_DIR) -name "*.so" -delete
	@find $(OCEAN_DIR) -name "build" -type d -exec rm -rf {} + 2>/dev/null || true

clean-raylib:
	@echo "Cleaning raylib..."
	@rm -rf $(RAYLIB_DIR) raylib-*_webassembly*

clean-all: clean-raylib clean-envs

list:
	@echo "Available environments:"
	@for env in $(ENV_NAMES); do echo "  $$env"; done

help:
	@echo "PufferLib C Environment Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make                 - Build all environments"
	@echo "  make <env_name>      - Build specific environment (e.g., make pong)"
	@echo "  make list            - List available environments"
	@echo "  make clean           - Clean compiled environments"
	@echo "  make clean-raylib    - Clean raylib"
	@echo "  make clean-all       - Clean everything"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1              - Build with debug symbols and sanitizers"
	@echo "  RAYLIB=0             - Disable raylib (it's enabled by default)"
	@echo "  LIBS=\"-lncurses\"      - Add library linking flags"
	@echo "  INCLUDES=\"-I/path\"    - Add additional include directories"
	@echo ""
	@echo "Available environments:"
	@for env in $(ENV_NAMES); do echo "  $$env"; done