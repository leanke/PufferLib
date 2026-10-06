#ifndef POKERED_PKSTATE_H
#define POKERED_PKSTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PK_STATE_WRAM_BASE 0xC000
#define PK_STATE_WRAM_SIZE 8192
#define PK_STATE_HRAM_BASE 0xFF80
#define PK_STATE_HRAM_SIZE 127
#define PK_STATE_EXT ".pkstate"

typedef struct PkState {
    uint8_t work_ram[PK_STATE_WRAM_SIZE];
    uint8_t high_ram[PK_STATE_HRAM_SIZE];
    uint32_t shade_rgb[4];
    uint64_t source_hash;
} PkState;

static inline uint8_t pk_state_byte(const PkState *s, uint16_t addr) {
    if (addr >= PK_STATE_WRAM_BASE && addr < PK_STATE_WRAM_BASE + PK_STATE_WRAM_SIZE)
        return s->work_ram[addr - PK_STATE_WRAM_BASE];
    if (addr >= PK_STATE_HRAM_BASE && addr < PK_STATE_HRAM_BASE + PK_STATE_HRAM_SIZE)
        return s->high_ram[addr - PK_STATE_HRAM_BASE];
    return 0;
}

void pk_state_pick_shades(const uint32_t *video, int pitch, int width, int height, uint32_t out[4]);

uint64_t pk_state_file_hash(const char *path);

bool pk_state_write(const char *path, const PkState *s);
bool pk_state_read(const char *path, PkState *out);

bool pk_state_from_gambatte(const char *rom_path, const char *state_path, PkState *out);

bool pk_state_resolve(const char *state_path, const char *rom_path, bool write_cache, PkState *out, char *err,
                      size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
