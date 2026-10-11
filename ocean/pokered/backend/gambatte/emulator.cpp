#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "gambatte_wrapper.h"
#include "../../data/ram_map.h"
#include "../backend.h"
#include "../pksnapshot.h"

static_assert(SCREEN_WIDTH == GB_SCREEN_WIDTH && BLOCKS_TALL == (GB_SCREEN_HEIGHT / BLOCK_PIXELS),
              "backend.h screen geometry out of sync with gambatte_wrapper.h");
static_assert(PK_FRAME_W == GB_SCREEN_WIDTH && PK_FRAME_H == GB_SCREEN_HEIGHT, "frame size mismatch");

namespace {

struct EmuBackend {
    Emulator emu;
    PkBackendConfig cfg;
    int warmup_frames;
};

struct EmuMem {
    const uint8_t *bank0, *bank1;
};

inline uint8_t mem_u8(void *ctx, uint16_t addr) {
    const EmuMem *m = (const EmuMem *)ctx;
    if (addr >= 0xC000 && addr < 0xD000) return m->bank0[addr - 0xC000];
    if (addr >= 0xD000 && addr < 0xE000) return m->bank1[addr - 0xD000];
    return 0;
}
inline uint16_t mem_u16(void *ctx, uint16_t addr) { return PKRED_BE16(mem_u8(ctx, addr), mem_u8(ctx, addr + 1)); }
inline const uint8_t *mem_span(void *ctx, uint16_t addr, unsigned len) {
    const EmuMem *m = (const EmuMem *)ctx;
    if (addr >= 0xC000 && addr + len <= 0xD000) return m->bank0 + (addr - 0xC000);
    if (addr >= 0xD000 && addr + len <= 0xE000) return m->bank1 + (addr - 0xD000);
    return NULL;
}

uint8_t emu_peek(void *impl, uint16_t addr) { return read_mem(&((EmuBackend *)impl)->emu, addr); }
void emu_poke(void *impl, uint16_t addr, uint8_t val) { write_mem(&((EmuBackend *)impl)->emu, addr, val); }

void *emu_create(const PkBackendConfig *cfg, const PkOptions *opts) {
    size_t state_len = strlen(cfg->state_path), ext_len = strlen(PK_STATE_EXT);
    if (state_len >= ext_len && strcmp(cfg->state_path + state_len - ext_len, PK_STATE_EXT) == 0) {
        fprintf(stderr,
                "pokered: the emulator backend resumes a Gambatte save state; state_path %s is a .pkstate "
                "(RAM only, for native). Point state_path at the Gambatte state it was converted from.\n",
                cfg->state_path);
        exit(1);
    }
    EmuBackend *be = (EmuBackend *)calloc(1, sizeof(EmuBackend));
    be->cfg = *cfg;
    be->warmup_frames = pk_opt_int(opts, "warmup_frames", 3);
    if (be->warmup_frames < 0) {
        fprintf(stderr, "pokered: emulator warmup_frames must be >= 0\n");
        exit(1);
    }
    int vec_threads = pk_opt_int(opts, "vec_num_threads", 1);
    Emulator *emu = &be->emu;
    emu->frame_skip = cfg->frameskip;
    emu->press_frames = cfg->press_frames;
    emu->render_enabled = !cfg->headless;
    if (cfg->state_path[0])
        strncpy(emu->state_path, cfg->state_path, sizeof(emu->state_path) - 1);
    strncpy(emu->rom_path, cfg->rom_path, sizeof(emu->rom_path) - 1);

    gb_set_audio_enabled(pk_opt_bool(opts, "audio_enabled", false));
    gb_set_frame_render_skip_enabled(pk_opt_bool(opts, "frame_render_skip_enabled", false));
    gb_pool_init(vec_threads > 0 ? vec_threads : 1, cfg->rom_path);
    emu->pool_state_buf = (uint8_t *)malloc(g_gb_pool.state_size);
    emu->pool_initial_state_buf = (uint8_t *)malloc(g_gb_pool.state_size);
    emu->video_buffer = (color_t *)calloc(GB_VIDEO_PITCH * GB_SCREEN_HEIGHT, sizeof(color_t));
    emu->gb = NULL;
    gb_pool_load_initial_state(emu, emu->state_path);
    return be;
}

void emu_destroy(void *impl) {
    EmuBackend *be = (EmuBackend *)impl;
    Emulator *emu = &be->emu;
    if (emu->gb) {
        gambatte_destroy(emu->gb);
        emu->gb = NULL;
    }
    if (emu->uses_shared_rom) {
        release_shared_rom();
        emu->uses_shared_rom = false;
    }
    free(emu->video_buffer);
    free(emu->pool_state_buf);
    free(emu->pool_initial_state_buf);
    free(be);
}

void emu_acquire(void *impl) { gb_pool_acquire_for(&((EmuBackend *)impl)->emu); }
void emu_release(void *impl) { gb_pool_release_for(&((EmuBackend *)impl)->emu); }

void emu_reset(void *impl, bool full_reset) {
    EmuBackend *be = (EmuBackend *)impl;
    if (!full_reset)
        return;
    if (be->cfg.verbose)
        printf("-- Resetting from initial state --\n");
    gambatte_load_state_raw(be->emu.gb, be->emu.pool_initial_state_buf);
}

void emu_warmup(void *impl) {
    EmuBackend *be = (EmuBackend *)impl;
    Emulator *emu = &be->emu;
    for (int i = 0; i < be->warmup_frames; i++)
        gambatte_run_frame(emu->gb, emu->video_buffer);
}

void emu_step(void *impl, unsigned buttons) {
    Emulator *emu = &((EmuBackend *)impl)->emu;
    int skip = emu->frame_skip > 0 ? emu->frame_skip : 24;
    int press = emu->press_frames > 0 ? emu->press_frames : 8;
    STEP_ACTION_FRAMES(emu->gb, (uint32_t)buttons, emu->video_buffer, press, skip);
}

void emu_snapshot(void *impl, PkSnapshot *s) {
    Emulator *emu = &((EmuBackend *)impl)->emu;
    EmuMem mem = {emu->gb ? gambatte_rambank0_ptr(emu->gb) : NULL, gb_rambank1_base(emu)};
    if (!mem.bank0 || !mem.bank1) {
        memset(s, 0, sizeof(*s));
        return;
    }
    const PkMemReader reader = {&mem, mem_u8, mem_u16, mem_span};
    pk_snapshot_extract(&reader, s);
}

void emu_screen(void *impl, float *obs) {
    const color_t *vbuf = ((EmuBackend *)impl)->emu.video_buffer;
    for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
        for (int sx = 0; sx < SCALED_WIDTH; sx++) {
            int src_y = sy * 2;
            int src_x = sx * 2;
            uint32_t gray_sum = 0;
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    color_t pixel = vbuf[(src_y + dy) * GB_VIDEO_PITCH + (src_x + dx)];
                    uint32_t r = (pixel >> 16) & 0xFF;
                    uint32_t g = (pixel >> 8) & 0xFF;
                    uint32_t bl = pixel & 0xFF;
                    gray_sum += r * 77 + g * 150 + bl * 29;
                }
            }
            obs[sy * SCALED_WIDTH + sx] = (float)(gray_sum >> 10);
        }
    }
}

size_t emu_state_size(void) { return g_gb_pool.state_size; }

bool emu_state_save(void *impl, void *buf) {
    gambatte_save_state_raw(((EmuBackend *)impl)->emu.gb, (uint8_t *)buf);
    return true;
}

bool emu_state_load(void *impl, const void *buf) {
    gambatte_load_state_raw(((EmuBackend *)impl)->emu.gb, (const uint8_t *)buf);
    return true;
}

bool emu_frame_rgba(void *impl, uint8_t *rgba) {
    const color_t *vb = ((EmuBackend *)impl)->emu.video_buffer;
    if (!vb)
        return false;
    for (int y = 0; y < GB_SCREEN_HEIGHT; y++) {
        const color_t *row = &vb[y * GB_VIDEO_PITCH];
        uint8_t *out = &rgba[y * GB_SCREEN_WIDTH * 4];
        for (int x = 0; x < GB_SCREEN_WIDTH; x++) {
            color_t px = row[x];
            out[x * 4 + 0] = (px >> 16) & 0xFF;
            out[x * 4 + 1] = (px >> 8) & 0xFF;
            out[x * 4 + 2] = px & 0xFF;
            out[x * 4 + 3] = 255;
        }
    }
    return true;
}

bool emu_quicksave(void *impl, const char *path) {
    Emulator *emu = &((EmuBackend *)impl)->emu;
    gb_pool_acquire_for(emu);
    bool ok = c_save_state_file(emu, path);
    gb_pool_release_for(emu);
    return ok;
}

bool emu_export_state(void *impl, PkState *out) {
    Emulator *emu = &((EmuBackend *)impl)->emu;
    gb_pool_acquire_for(emu);
    for (int i = 0; i < PK_STATE_WRAM_SIZE; i++)
        out->work_ram[i] = read_mem(emu, PK_STATE_WRAM_BASE + i);
    for (int i = 0; i < PK_STATE_HRAM_SIZE; i++)
        out->high_ram[i] = read_mem(emu, PK_STATE_HRAM_BASE + i);
    pk_state_pick_shades(emu->video_buffer, GB_VIDEO_PITCH, GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT, out->shade_rgb);
    gb_pool_release_for(emu);
    return true;
}

const PkBackend EMULATOR_BACKEND = {
    "emulator",
    emu_create, emu_destroy, emu_acquire, emu_release, emu_reset, emu_warmup, emu_step, emu_snapshot,
    emu_peek, emu_poke, emu_screen,
    emu_frame_rgba,
    emu_quicksave, emu_export_state,
    emu_state_size, emu_state_save, emu_state_load,
};

}

PK_REGISTER_BACKEND(EMULATOR_BACKEND)
