if [ "$MODE" = "web" ]; then
    echo "Error: pokered does not support --web (gambatte C++ core, OpenSSL stream client, ROM); build natively or with --cpu." >&2
    exit 1
fi
if [ "$USE_GPU_ENV" = "1" ]; then
    echo "Error: pokered does not support --cu (stepping runs on the CPU; pokered.cu is the policy encoder, always compiled in)." >&2
    exit 1
fi

PK_DIR=ocean/pokered
JOBS="$(nproc)"

GAMBATTE_DIR="${GAMBATTE_DIR:-$(pwd)/vendor/gambatte-libretro}"
[ -d "$GAMBATTE_DIR/src" ] || \
    git clone --depth 1 --branch sloppy https://github.com/leanke/gambatte-libretro.git "$GAMBATTE_DIR/src"
GB_STAMP="$GAMBATTE_DIR/install/source.rev"
GB_REV="$(git -C "$GAMBATTE_DIR/src" rev-parse HEAD 2>/dev/null)$(git -C "$GAMBATTE_DIR/src" status --porcelain 2>/dev/null | sha1sum | cut -c1-12)"
if [ ! -f "$GAMBATTE_DIR/install/lib/libgambatte.a" ] || [ "$(cat "$GB_STAMP" 2>/dev/null)" != "$GB_REV" ]; then
    echo "Building libgambatte (static) ..."
    GB_SRC_ROOT="$GAMBATTE_DIR/src"
    GB_CORE="$GB_SRC_ROOT/libgambatte/src"
    GB_OBJ_DIR="$GAMBATTE_DIR/build"
    mkdir -p "$GB_OBJ_DIR" "$GAMBATTE_DIR/install/include" "$GAMBATTE_DIR/install/lib"
    GB_INC=(-I"$GB_SRC_ROOT/libgambatte/include" -I"$GB_CORE"
            -I"$GB_SRC_ROOT/libgambatte/libretro"
            -I"$GB_SRC_ROOT/libgambatte/libretro-common/include"
            -I"$GB_SRC_ROOT/common")
    GB_FILES=(
        bootloader.cpp cpu.cpp gambatte.cpp initstate.cpp interrupter.cpp
        interruptrequester.cpp gambatte-memory.cpp sound.cpp statesaver.cpp
        tima.cpp video.cpp video_libretro.cpp
        mem/cartridge.cpp mem/cartridge_libretro.cpp mem/huc3.cpp
        mem/memptrs.cpp mem/rtc.cpp
        sound/channel1.cpp sound/channel2.cpp sound/channel3.cpp
        sound/channel4.cpp sound/duty_unit.cpp sound/envelope_unit.cpp
        sound/length_counter.cpp
        video/ly_counter.cpp video/lyc_irq.cpp video/next_m0_time.cpp
        video/ppu.cpp video/sprite_mapper.cpp
    )
    GB_OBJS=() GB_PIDS=()
    for f in "${GB_FILES[@]}"; do
        obj="$GB_OBJ_DIR/$(basename "$f" .cpp).o"
        ${CXX:-clang++} -std=c++17 -O2 -D__LIBRETRO__ -DHAVE_CSTDINT \
            "${GB_INC[@]}" -c "$GB_CORE/$f" -o "$obj" &
        GB_OBJS+=("$obj") GB_PIDS+=($!)
    done
    obj="$GB_OBJ_DIR/gambatte_log.o"
    ${CC:-clang} -O2 "${GB_INC[@]}" -c "$GB_SRC_ROOT/libgambatte/libretro/gambatte_log.c" -o "$obj" &
    GB_OBJS+=("$obj") GB_PIDS+=($!)
    for pid in "${GB_PIDS[@]}"; do wait "$pid"; done
    rm -f "$GAMBATTE_DIR/install/lib/libgambatte.a"
    ar rcs "$GAMBATTE_DIR/install/lib/libgambatte.a" "${GB_OBJS[@]}"
    cp "$GB_SRC_ROOT/libgambatte/include/"*.h "$GAMBATTE_DIR/install/include/"
    echo "$GB_REV" > "$GB_STAMP"
fi
INCLUDES+=(-I"$GAMBATTE_DIR/install/include")
LINK_ARCHIVES+=("$GAMBATTE_DIR/install/lib/libgambatte.a")
EXTRA_SRC="$PK_DIR/backend/gambatte/gambatte_c.cpp $PK_DIR/backend/gambatte/emulator.cpp $PK_DIR/backend/backend.cpp $PK_DIR/backend/pksnapshot.cpp vendor/cJSON.c"

pk_native_stale() {
    local out=$1 deps=$2 stamp=$3 flags=$4 dep
    [ -f "$out" ] && [ -f "$deps" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$flags" ] || return 0
    [ "$NATIVE_BUILD/libnative_core.a" -nt "$out" ] && return 0
    for dep in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$deps"); do
        [ "$dep" -nt "$out" ] && return 0
    done
    return 1
}

if [ "${POKERED_NO_NATIVE:-0}" != "1" ]; then
    NATIVE_DIR="vendor/pokered-native"
    NATIVE_REMOTE="git@github.com:leanke/pokered-native.git"
    if [ ! -d "$NATIVE_DIR/.git" ]; then
        echo "Cloning pokered-native from $NATIVE_REMOTE ..."
        git clone "$NATIVE_REMOTE" "$NATIVE_DIR"
    fi
    NATIVE_BUILD="$(pwd)/$NATIVE_DIR/build_native"
    NATIVE_CMAKE_MARCH=$([ "${NATIVE_MARCH:-1}" = "1" ] && echo ON || echo OFF)
    NATIVE_CACHE="$NATIVE_BUILD/CMakeCache.txt"
    if [ -f "$NATIVE_CACHE" ] &&
       ! grep -qx "CMAKE_HOME_DIRECTORY:INTERNAL=$(pwd)/$PK_DIR/backend/native" "$NATIVE_CACHE"; then
        rm -rf "$NATIVE_CACHE" "$NATIVE_BUILD/CMakeFiles"
    fi
    if ! grep -qx "NATIVE_MARCH:BOOL=$NATIVE_CMAKE_MARCH" "$NATIVE_CACHE" 2>/dev/null; then
        cmake -S "$PK_DIR/backend/native" -B "$NATIVE_BUILD" -DNATIVE_ROOT="$(pwd)/$NATIVE_DIR" \
            -DNATIVE_MARCH=$NATIVE_CMAKE_MARCH -DCMAKE_BUILD_TYPE=Release >/dev/null
    fi
    cmake --build "$NATIVE_BUILD" --target native_core -j"$JOBS"

    NATIVE_OBJ="$NATIVE_BUILD/pokered_native_backend.o"
    NATIVE_LTO_OBJ="$NATIVE_BUILD/pokered_native_lto.o"
    NATIVE_CC_FLAGS=(-std=gnu11 -O3 -ftls-model=initial-exec -flto=auto)
    [ "${NATIVE_MARCH:-1}" = "1" ] && NATIVE_CC_FLAGS+=(-march=native)
    NATIVE_STAMP="$NATIVE_BUILD/pokered_native_lto.flags"
    NATIVE_FLAGS_ID="${CC:-gcc} ${NATIVE_CC_FLAGS[*]} nolto-rel"
    if pk_native_stale "$NATIVE_LTO_OBJ" "$NATIVE_OBJ.d" "$NATIVE_STAMP" "$NATIVE_FLAGS_ID"; then
        echo "Building the native backend object ..."
        ${CC:-gcc} "${NATIVE_CC_FLAGS[@]}" -ffat-lto-objects -MMD -MF "$NATIVE_OBJ.d" \
            -I./src -I./vendor -I./$PK_DIR -I./$NATIVE_DIR/include -I./$NATIVE_DIR/data \
            -c "$PK_DIR/backend/native/native.c" -o "$NATIVE_OBJ"
        ${CC:-gcc} "${NATIVE_CC_FLAGS[@]}" -flto="$JOBS" -flinker-output=nolto-rel -r -nostdlib \
            "$NATIVE_OBJ" -Wl,--whole-archive "$NATIVE_BUILD/libnative_core.a" -Wl,--no-whole-archive \
            -o "$NATIVE_LTO_OBJ"
        echo "$NATIVE_FLAGS_ID" > "$NATIVE_STAMP"
    fi
    LINK_ARCHIVES+=("$NATIVE_LTO_OBJ")
fi

EXTRA_CFLAGS+=(-D__LIBRETRO__ -DHAVE_CSTDINT)
[ "${POKERED_OBS_U8:-0}" = "1" ] && EXTRA_CFLAGS+=(-DPOKERED_OBS_U8)
EXTRA_LDFLAGS+=(-lssl -lcrypto)
if [ "$PLATFORM" = "Linux" ]; then
    EXTRA_LDFLAGS+=(-lstdc++)
else
    EXTRA_LDFLAGS+=(-lc++)
fi
