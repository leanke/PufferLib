#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pokered/api.h"
#include "pokered/rl.h"
#include "pokered/snapshot.h"

#include "../../data/ram_map.h"
#include "../backend.h"
#include "../pksnapshot.h"

_Static_assert(PK_FRAME_W == POKERED_SCREEN_WIDTH && PK_FRAME_H == POKERED_SCREEN_HEIGHT,
              "backend.h screen geometry out of sync with pokered-native");
_Static_assert(PK_STATE_WRAM_SIZE == WRAM_SIZE && PK_STATE_HRAM_SIZE == HRAM_SIZE,
              "pksnapshot.h out of sync with pokered-native's WRAM/HRAM");

typedef struct NativeBackend {
    PokeredEnv *env;
    void *handle;
    GameContext *ctx;
    PkBackendConfig cfg;
    void *start;
    size_t start_size;
    uint32_t gray[4];
    float half_lut[256];
    bool screen_half;
    bool decision_mode, auto_text;
    unsigned decision_max_frames;
    uint32_t shade_rgb[4];
} NativeBackend;

static uint8_t mem_u8(void *ctx, uint16_t addr) { (void)ctx; return mem_read(addr); }
static uint16_t mem_u16(void *ctx, uint16_t addr) { (void)ctx; return mem_read16_be(addr); }
static const uint8_t *mem_span(void *ctx, uint16_t addr, unsigned len) {
    (void)ctx;
    return wram_span_ptr(addr, len);
}

static uint8_t nat_peek(void *impl, uint16_t addr) { (void)impl; return mem_read(addr); }
static void nat_poke(void *impl, uint16_t addr, uint8_t val) { (void)impl; mem_write(addr, val); }

static void *nat_create(const PkBackendConfig *cfg, const PkOptions *opts) {
    NativeBackend *be = (NativeBackend *)calloc(1, sizeof(NativeBackend));
    be->cfg = *cfg;
    PkState start_state;
    char err[512];
    if (!pk_state_resolve(cfg->state_path, cfg->rom_path, cfg->pkstate_cache_enabled, &start_state, err, sizeof(err))) {
        fprintf(stderr, "pokered: native backend: %s\n", err);
        exit(1);
    }
    PokeredEnvConfig ecfg;
    pokered_env_config_default(&ecfg);
    ecfg.screen_format = POKERED_SCREEN_SHADES;
    ecfg.text_scroll = false;
    ecfg.cut_hooks = false;
    ecfg.fast_mode = (uint8_t)pk_opt_int(opts, "fast_mode", POKERED_FAST_OFF);
    be->decision_mode = pk_opt_bool(opts, "decision_step_enabled", false);
    be->auto_text = pk_opt_bool(opts, "auto_text_enabled", false);
    be->decision_max_frames = (unsigned)pk_opt_int(opts, "decision_max_frames", 3600);
    be->screen_half = pk_opt_bool(opts, "screen_half_enabled", false);
    if (ecfg.fast_mode > POKERED_FAST_GAME) {
        fprintf(stderr, "pokered: native backend: fast_mode (0-3) out of range\n");
        exit(1);
    }
    if ((ecfg.fast_mode & POKERED_FAST_NO_VIDEO) && cfg->screen_obs_enabled)
        fprintf(stderr, "pokered: native backend: fast_mode skips video, so the screen observation is all zeros; "
                        "set env.screen_obs_enabled=False\n");
    be->env = pokered_env_create(&ecfg);
    if (!be->env) {
        fprintf(stderr, "pokered: native backend out of memory\n");
        exit(1);
    }
    be->handle = pokered_env_game(be->env);
    be->ctx = pokered_env_context(be->env);
    pokered_make_current(be->handle);
    game_init(be->ctx);
    game_load_state(be->ctx, start_state.work_ram, start_state.high_ram);

    be->start_size = pokered_state_size();
    be->start = malloc(be->start_size);
    if (pokered_state_save(be->handle, be->ctx, be->start, be->start_size) != be->start_size) {
        fprintf(stderr, "pokered: native backend could not snapshot its start state\n");
        exit(1);
    }
    for (int i = 0; i < 4; i++) {
        uint32_t c = start_state.shade_rgb[i];
        be->shade_rgb[i] = c;
        be->gray[i] = ((c >> 16) & 0xFF) * 77 + ((c >> 8) & 0xFF) * 150 + (c & 0xFF) * 29;
    }
    for (int b = 0; b < 256; b++)
        be->half_lut[b] = (float)((be->gray[b >> 6 & 3] + be->gray[b >> 4 & 3] + be->gray[b >> 2 & 3] +
                                   be->gray[b & 3]) >> 10);
    return be;
}

static void nat_destroy(void *impl) {
    NativeBackend *be = (NativeBackend *)impl;
    pokered_env_destroy(be->env);
    free(be->start);
    free(be);
}

static void nat_acquire(void *impl) { pokered_make_current(((NativeBackend *)impl)->handle); }
static void nat_release(void *impl) { (void)impl; }
static void nat_warmup(void *impl) { (void)impl; }

static void nat_reset(void *impl, bool full_reset) {
    NativeBackend *be = (NativeBackend *)impl;
    if (!full_reset)
        return;
    if (!pokered_state_load(be->handle, be->ctx, be->start, be->start_size)) {
        fprintf(stderr, "pokered: native backend could not restore its start state\n");
        exit(1);
    }
}

static void nat_step(void *impl, unsigned buttons) {
    NativeBackend *be = (NativeBackend *)impl;
    const PkBackendConfig *cfg = &be->cfg;

    int skip = cfg->frameskip > 0 ? cfg->frameskip : 24;
    int press = cfg->press_frames > 0 ? cfg->press_frames : 8;
    if (press > skip)
        press = skip;
    uint8_t keys = (buttons & PK_BTN_A ? BTN_A : 0) | (buttons & PK_BTN_B ? BTN_B : 0) |
                   (buttons & PK_BTN_SELECT ? BTN_SELECT : 0) | (buttons & PK_BTN_START ? BTN_START : 0) |
                   (buttons & PK_BTN_RIGHT ? BTN_RIGHT : 0) | (buttons & PK_BTN_LEFT ? BTN_LEFT : 0) |
                   (buttons & PK_BTN_UP ? BTN_UP : 0) | (buttons & PK_BTN_DOWN ? BTN_DOWN : 0);
    if (be->decision_mode) {
        pokered_env_run_decision(be->env, keys, (unsigned)press, be->decision_max_frames, be->auto_text, NULL);
    } else {
        for (int f = 0; f < skip; f++)
            game_frame(be->ctx, f < press ? keys : 0);
    }
}

static void nat_snapshot(void *impl, PkSnapshot *s) {
    (void)impl;
    const PkMemReader reader = {NULL, mem_u8, mem_u16, mem_span};
    pk_snapshot_extract(&reader, s);
}

static const uint8_t *nat_shades(NativeBackend *be, uint8_t *buf) {
    pokered_get_screen(be->handle, buf, POKERED_SCREEN_SHADES);
    return buf;
}

static void nat_screen(void *impl, float *obs) {
    NativeBackend *be = (NativeBackend *)impl;
    if (be->screen_half) {
        uint8_t half[SCALED_PIXELS];
        pokered_get_screen(be->handle, half, POKERED_SCREEN_HALF);
        for (int i = 0; i < SCALED_PIXELS; i++)
            obs[i] = be->half_lut[half[i]];
        return;
    }
    uint8_t shades[PK_FRAME_W * PK_FRAME_H];
    nat_shades(be, shades);
    for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
        const uint8_t *r0 = &shades[(sy * 2) * PK_FRAME_W];
        const uint8_t *r1 = r0 + PK_FRAME_W;
        for (int sx = 0; sx < SCALED_WIDTH; sx++)
            obs[sy * SCALED_WIDTH + sx] =
                (float)((be->gray[r0[sx * 2] & 3] + be->gray[r0[sx * 2 + 1] & 3] + be->gray[r1[sx * 2] & 3] +
                         be->gray[r1[sx * 2 + 1] & 3]) >> 10);
    }
}

static bool nat_frame_rgba(void *impl, uint8_t *rgba) {
    NativeBackend *be = (NativeBackend *)impl;
    uint8_t shades[PK_FRAME_W * PK_FRAME_H];
    nat_shades(be, shades);
    for (int i = 0; i < PK_FRAME_W * PK_FRAME_H; i++) {
        uint32_t c = be->shade_rgb[shades[i] & 3];
        rgba[i * 4 + 0] = (c >> 16) & 0xFF;
        rgba[i * 4 + 1] = (c >> 8) & 0xFF;
        rgba[i * 4 + 2] = c & 0xFF;
        rgba[i * 4 + 3] = 255;
    }
    return true;
}

static bool nat_export_state(void *impl, PkState *out) {
    NativeBackend *be = (NativeBackend *)impl;
    pokered_make_current(be->handle);
    game_save_state(be->ctx, out->work_ram, out->high_ram);
    memcpy(out->shade_rgb, be->shade_rgb, sizeof(out->shade_rgb));
    return true;
}

static size_t nat_state_size(void) { return pokered_state_size(); }

static bool nat_state_save(void *impl, void *buf) {
    NativeBackend *be = (NativeBackend *)impl;
    size_t size = pokered_state_size();
    return pokered_state_save(be->handle, be->ctx, buf, size) == size;
}

static bool nat_state_load(void *impl, const void *buf) {
    NativeBackend *be = (NativeBackend *)impl;
    return pokered_state_load(be->handle, be->ctx, buf, pokered_state_size());
}

static const PkBackend NATIVE_BACKEND = {
    "native",
    nat_create, nat_destroy, nat_acquire, nat_release, nat_reset, nat_warmup, nat_step, nat_snapshot,
    nat_peek, nat_poke, nat_screen,
    nat_frame_rgba,
    NULL, nat_export_state,
    nat_state_size, nat_state_save, nat_state_load,
};

PK_REGISTER_BACKEND(NATIVE_BACKEND)
