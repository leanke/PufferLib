#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pokered/api.h"
#include "pokered/rl.h"
#include "pokered/snapshot.h"

#include "../includes/ram_map.h"
#include "../includes/events.h"
#include "../pokered_layout.h"
#include "../pokered_backend.h"
#include "../pkstate.h"
#include "ram_scenario.h"
#include "step_events.h"

_Static_assert(PK_FRAME_W == POKERED_SCREEN_WIDTH && PK_FRAME_H == POKERED_SCREEN_HEIGHT,
              "pokered_layout.h screen geometry out of sync with pokered-native");
_Static_assert(PK_STATE_WRAM_SIZE == WRAM_SIZE && PK_STATE_HRAM_SIZE == HRAM_SIZE,
              "pkstate.h out of sync with pokered-native's WRAM/HRAM");
_Static_assert(sizeof(EVENT_LIST) / sizeof(EVENT_LIST[0]) <= PK_MAX_EVENTS, "PkSnapshot.events too small");

#define POKEDEX_BYTES ((PKRED_POKEDEX_NUM_POKEMON + 7) / 8)

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
    PkEventTracker events;
} NativeBackend;

static int popcount_bytes(const uint8_t *b, int size) {
    int count = 0;
    for (int i = 0; i < size; i++)
        count += __builtin_popcount(b[i]);
    return count;
}

static void fill_mon(PkMon *m, const PartyMon *p) {
    m->species = p->species;
    m->level = p->level;
    m->hp = p->current_hp;
    m->max_hp = p->max_hp;
    memcpy(m->moves, p->moves, sizeof(m->moves));
    memcpy(m->pp, p->pp, sizeof(m->pp));
}

static void fill_battler(PkMon *m, const BattleMon *b) {
    memset(m, 0, sizeof(*m));
    m->species = b->species;
    m->hp = b->hp;
    m->max_hp = b->max_hp;
    m->level = b->level;
}

static uint8_t ram_read(void *ctx, uint16_t addr) { (void)ctx; return mem_read(addr); }
static void ram_write(void *ctx, uint16_t addr, uint8_t val) { (void)ctx; mem_write(addr, val); }

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
    ecfg.text_speed = (uint8_t)pk_opt_int(opts, "text_speed", OPTION_TEXT_SPEED_KEEP);
    ecfg.battle_animation = (uint8_t)pk_opt_int(opts, "battle_animation", OPTION_BATTLE_ANIMATION_KEEP);
    ecfg.battle_style = (uint8_t)pk_opt_int(opts, "battle_style", OPTION_BATTLE_STYLE_KEEP);
    be->decision_mode = pk_opt_bool(opts, "decision_step_enabled", false);
    be->auto_text = pk_opt_bool(opts, "auto_text_enabled", false);
    be->decision_max_frames = (unsigned)pk_opt_int(opts, "decision_max_frames", 3600);
    be->screen_half = pk_opt_bool(opts, "screen_half_enabled", false);
    if (ecfg.fast_mode > POKERED_FAST_GAME || ecfg.text_speed > OPTION_TEXT_SPEED_SLOW ||
        ecfg.battle_animation > OPTION_BATTLE_ANIMATION_OFF || ecfg.battle_style > OPTION_BATTLE_STYLE_SET) {
        fprintf(stderr, "pokered: native backend: fast_mode (0-3), text_speed (0-3), battle_animation (0-2) or "
                        "battle_style (0-2) out of range\n");
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
    pokered_env_apply_options(be->env);

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

static void nat_reset(void *impl, bool full_reset, unsigned *rng) {
    NativeBackend *be = (NativeBackend *)impl;
    (void)rng;
    pk_events_rebase(&be->events);
    if (!full_reset)
        return;
    if (!pokered_state_load(be->handle, be->ctx, be->start, be->start_size)) {
        fprintf(stderr, "pokered: native backend could not restore its start state\n");
        exit(1);
    }
    PkRam ram = {NULL, ram_read, ram_write};
    pk_ram_clear_cut(&ram, &be->events);
}

static void nat_step(void *impl, int action, const PkSnapshot *last) {
    NativeBackend *be = (NativeBackend *)impl;
    const PkBackendConfig *cfg = &be->cfg;

    PkRam ram = {NULL, ram_read, ram_write};
    pk_ram_apply_scenario(&ram, cfg, last);

    int skip = cfg->frameskip > 0 ? cfg->frameskip : 24;
    int press = cfg->press_frames > 0 ? cfg->press_frames : 8;
    if (press > skip)
        press = skip;
    unsigned buttons = pk_action_buttons(action);
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
    pk_ram_track_cut(&ram, &be->events, last);
    pk_events_stepped(&be->events);
}

static void nat_snapshot(void *impl, PkSnapshot *s) {
    NativeBackend *be = (NativeBackend *)impl;
    memset(s, 0, sizeof(*s));

    s->x = g_wram.x_coord;
    s->y = g_wram.y_coord;
    s->map_n = g_wram.cur_map;
    s->facing = mem_read(PKRED_ADDR_PLAYER_SPRITE_FACING_DIRECTION) / 4;
    s->badges = mem_read(PKRED_ADDR_OBTAINED_BADGES);
    s->party_count = g_wram.party_count > 6 ? 6 : g_wram.party_count;
    for (int i = 0; i < s->party_count; i++)
        fill_mon(&s->party[i], &g_wram.party_mons[i]);
    s->pokedex_owned_count = (uint8_t)popcount_bytes(g_wram.pokedex_owned, POKEDEX_BYTES);
    s->pokedex_seen_count = (uint8_t)popcount_bytes(g_wram.pokedex_seen, POKEDEX_BYTES);

    s->hp_fraction = 1.0f;
    if (s->party_count > 0) {
        uint32_t total_hp = 0, total_maxhp = 0;
        for (int i = 0; i < s->party_count; i++) {
            total_hp += s->party[i].hp;
            total_maxhp += s->party[i].max_hp;
        }
        s->hp_fraction = (total_maxhp > 0) ? (float)total_hp / (float)total_maxhp : 1.0f;
    }

    s->in_battle = (int8_t)g_wram.is_in_battle;
    if (s->in_battle == 1 || s->in_battle == 2) {
        fill_battler(&s->battle_mon, &g_wram.battle_mon);
        s->escaped = g_wram.escaped_from_battle || g_wram.battle_result == 2;
        fill_battler(&s->enemy_mon, &g_wram.enemy_mon);
    }

    s->bag_count = g_wram.num_bag_items > PKRED_BAG_ITEM_CAPACITY ? PKRED_BAG_ITEM_CAPACITY : g_wram.num_bag_items;
    for (int i = 0; i < s->bag_count; i++) {
        s->bag[i].item = g_wram.bag_items[2 * i];
        s->bag[i].count = g_wram.bag_items[2 * i + 1];
    }

    s->surfing = g_wram.walk_bike_surf_state == PKRED_SURF_STATE;
    s->strength_active = g_wram.status_flags1.strength_active;
    s->used_fly = g_wram.status_flags7.used_fly;
    s->dark_cave = g_wram.map_pal_offset == PKRED_PAL_DARK_CAVE;
    s->map_block_hash = pkred_hash_bytes(g_wram.overworld_map, PKRED_OVERWORLD_MAP_SIZE);

    for (size_t i = 0; i < EVENT_COUNT; ++i)
        s->events[i] = (mem_read(EVENT_LIST[i].address) >> EVENT_LIST[i].bit) & 1;

    pk_events_apply(&be->events, s);
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
    for (int i = 0; i < PK_STATE_WRAM_SIZE; i++)
        out->work_ram[i] = mem_read((uint16_t)(PK_STATE_WRAM_BASE + i));
    for (int i = 0; i < PK_STATE_HRAM_SIZE; i++)
        out->high_ram[i] = mem_read((uint16_t)(PK_STATE_HRAM_BASE + i));
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
    if (!pokered_state_load(be->handle, be->ctx, buf, pokered_state_size()))
        return false;
    pk_events_rebase(&be->events);
    PkRam ram = {NULL, ram_read, ram_write};
    pk_ram_clear_cut(&ram, &be->events);
    return true;
}

static const PkBackend NATIVE_BACKEND = {
    "native",
    nat_create, nat_destroy, nat_acquire, nat_release, nat_reset, nat_warmup, nat_step, nat_snapshot, nat_screen,
    nat_frame_rgba,
    NULL, nat_export_state,
    nat_state_size, nat_state_save, nat_state_load,
};

PK_REGISTER_BACKEND(NATIVE_BACKEND)
