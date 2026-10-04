// "native" pokered backend: vendor/pokered-native, a C port of Pokemon Red's disassembly that
// keeps the game's WRAM layout. It is read like the emulator's RAM (same addresses, same
// events), so observations, rewards and the policy are shared with the other backends.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pokered/api.h"
#include "pokered/snapshot.h"

#include "../includes/ram_map.h"
#include "../includes/events.h"
#include "../pokered_layout.h"
#include "../pokered_backend.h"
#include "native_start.h"

_Static_assert(PK_FRAME_W == POKERED_SCREEN_WIDTH && PK_FRAME_H == POKERED_SCREEN_HEIGHT,
              "pokered_layout.h screen geometry out of sync with pokered-native");
_Static_assert(sizeof(NATIVE_START_WRAM) == WRAM_SIZE && sizeof(NATIVE_START_HRAM) == HRAM_SIZE,
              "native_start.h out of sync with pokered-native's WRAM/HRAM");
_Static_assert(sizeof(EVENT_LIST) / sizeof(EVENT_LIST[0]) <= PK_MAX_EVENTS, "PkSnapshot.events too small");

#define VIRIDIAN_CITY_MAP 0x01
#define POKEDEX_BYTES ((PKRED_POKEDEX_NUM_POKEMON + 7) / 8)

typedef struct NativeBackend {
    void *handle;       // PokeRedState
    GameContext ctx;
    PkBackendConfig cfg;
    void *start;        // pokered_state_save of the start state; every full reset reloads it
    size_t start_size;
    uint32_t gray[4];   // per-shade r*77+g*150+b*29 of the emulator's DMG colors
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
}

static void fill_battler(PkMon *m, const BattleMon *b) {
    memset(m, 0, sizeof(*m));
    m->species = b->species;
    m->hp = b->hp;
    m->max_hp = b->max_hp;
    m->level = b->level;
}

static void set_missable_object_hidden(uint8_t missable_index, bool hidden) {
    uint16_t addr = PKRED_ADDR_MISSABLE_OBJECT_FLAGS + (missable_index >> 3);
    uint8_t bit = missable_index & 7;
    uint8_t byte = mem_read(addr);
    mem_write(addr, hidden ? (byte | (1 << bit)) : (byte & ~(1 << bit)));
}

static void *nat_create(const PkBackendConfig *cfg) {
    NativeBackend *be = (NativeBackend *)calloc(1, sizeof(NativeBackend));
    be->cfg = *cfg;
    be->handle = pokered_create();
    if (!be->handle) {
        fprintf(stderr, "pokered: native backend out of memory\n");
        exit(1);
    }
    pokered_make_current(be->handle);
    game_init(&be->ctx);
    game_load_state(&be->ctx, NATIVE_START_WRAM, NATIVE_START_HRAM);

    be->start_size = pokered_state_size();
    be->start = malloc(be->start_size);
    if (pokered_state_save(be->handle, &be->ctx, be->start, be->start_size) != be->start_size) {
        fprintf(stderr, "pokered: native backend could not snapshot its start state\n");
        exit(1);
    }
    for (int i = 0; i < 4; i++) {
        uint32_t c = NATIVE_START_SHADE_RGB[i];
        be->gray[i] = ((c >> 16) & 0xFF) * 77 + ((c >> 8) & 0xFF) * 150 + (c & 0xFF) * 29;
    }
    return be;
}

static void nat_destroy(void *impl) {
    NativeBackend *be = (NativeBackend *)impl;
    pokered_destroy(be->handle);
    free(be->start);
    free(be);
}

static void nat_acquire(void *impl) { pokered_make_current(((NativeBackend *)impl)->handle); }
static void nat_release(void *impl) { (void)impl; }
static void nat_warmup(void *impl) { (void)impl; }

static void nat_reset(void *impl, bool full_reset, unsigned *rng, bool *from_milestone) {
    NativeBackend *be = (NativeBackend *)impl;
    (void)rng;
    *from_milestone = false;
    if (!full_reset)
        return;
    if (!pokered_state_load(be->handle, &be->ctx, be->start, be->start_size)) {
        fprintf(stderr, "pokered: native backend could not restore its start state\n");
        exit(1);
    }
}

static void nat_step(void *impl, int action, const PkSnapshot *last) {
    NativeBackend *be = (NativeBackend *)impl;
    const PkBackendConfig *cfg = &be->cfg;

    if (cfg->disable_wild_until_badge) {
        uint8_t flags = mem_read(PKRED_ADDR_WD72E);
        if (last->badges == 0)
            mem_write(PKRED_ADDR_WD72E, flags | (1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
        else
            mem_write(PKRED_ADDR_WD72E, flags & ~(1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
    }

    {
        // Same Route 22 rival handling as the emulator backend (the event bytes are identical).
        uint8_t flags = mem_read(PKRED_ADDR_ROUTE22_RIVAL_EVENTS);
        bool trigger_1st_was_set = flags & (1 << PKRED_ROUTE22_RIVAL_TRIGGER_1ST_BIT);
        bool trigger_2nd_was_set = flags & (1 << PKRED_ROUTE22_RIVAL_TRIGGER_2ND_BIT);
        if (cfg->route22_rival_beaten) {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_TRIGGER_1ST_BIT);
            flags |= (1 << PKRED_ROUTE22_RIVAL_BEAT_1ST_BIT);
            if (trigger_1st_was_set)
                flags &= ~(1 << PKRED_ROUTE22_RIVAL_WANTS_BATTLE_BIT);
        } else {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_BEAT_1ST_BIT);
        }
        if (cfg->route22_rival_2nd_beaten) {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_TRIGGER_2ND_BIT);
            flags |= (1 << PKRED_ROUTE22_RIVAL_BEAT_2ND_BIT);
            if (trigger_2nd_was_set)
                flags &= ~(1 << PKRED_ROUTE22_RIVAL_WANTS_BATTLE_BIT);
        } else {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_BEAT_2ND_BIT);
        }
        mem_write(PKRED_ADDR_ROUTE22_RIVAL_EVENTS, flags);
        if (cfg->route22_rival_beaten)
            set_missable_object_hidden(PKRED_MISSABLE_HS_ROUTE_22_RIVAL_1, true);
        if (cfg->route22_rival_2nd_beaten)
            set_missable_object_hidden(PKRED_MISSABLE_HS_ROUTE_22_RIVAL_2, true);
    }

    if (last->map_n == VIRIDIAN_CITY_MAP && mem_read(PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT) == 1) {
        mem_write(PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT, 0);
        mem_write(PKRED_ADDR_BATTLE_TYPE, 0);
    }

    static const uint8_t TO_BTN[PKRED_ACTION_COUNT] = {BTN_A, BTN_B, BTN_RIGHT, BTN_LEFT, BTN_UP, BTN_DOWN};
    int skip = cfg->frameskip > 0 ? cfg->frameskip : 24;
    int press = cfg->press_frames > 0 ? cfg->press_frames : 8;
    if (press > skip)
        press = skip;
    uint8_t keys = (action < 0 || action >= PKRED_ACTION_COUNT) ? 0 : TO_BTN[action];
    for (int f = 0; f < skip; f++)
        game_frame(&be->ctx, f < press ? keys : 0);
}

static void nat_snapshot(void *impl, PkSnapshot *s) {
    (void)impl;
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
        // The native RUN command records the escape as wBattleResult == 2 (cleared when a
        // battle starts) without setting wEscapedFromBattle, which only Teleport/Roar/Poke Doll
        // set; the emulator's game sets both.
        s->escaped = g_wram.escaped_from_battle || g_wram.battle_result == 2;
        fill_battler(&s->enemy_mon, &g_wram.enemy_mon);
    }

    s->bag_count = g_wram.num_bag_items > PKRED_BAG_ITEM_CAPACITY ? PKRED_BAG_ITEM_CAPACITY : g_wram.num_bag_items;
    for (int i = 0; i < s->bag_count; i++) {
        s->bag[i].item = g_wram.bag_items[2 * i];
        s->bag[i].count = g_wram.bag_items[2 * i + 1];
    }

    for (size_t i = 0; i < EVENT_COUNT; ++i)
        s->events[i] = (mem_read(EVENT_LIST[i].address) >> EVENT_LIST[i].bit) & 1;
}

// Renders the LCD's DMG shades (0 = white .. 3 = black) with the emulator's colors.
static const uint8_t *nat_shades(NativeBackend *be, uint8_t *buf) {
    pokered_get_screen(be->handle, buf, POKERED_SCREEN_SHADES);
    return buf;
}

// The emulator backend's observation: each 2x2 block's luma sum >> 10 (a mean gray).
static void nat_screen(void *impl, float *obs) {
    NativeBackend *be = (NativeBackend *)impl;
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
        uint32_t c = NATIVE_START_SHADE_RGB[shades[i] & 3];
        rgba[i * 4 + 0] = (c >> 16) & 0xFF;
        rgba[i * 4 + 1] = (c >> 8) & 0xFF;
        rgba[i * 4 + 2] = c & 0xFF;
        rgba[i * 4 + 3] = 255;
    }
    return true;
}

static const PkBackend NATIVE_BACKEND = {
    "native", nat_create, nat_destroy, nat_acquire, nat_release, nat_reset, nat_warmup, nat_step,
    nat_snapshot, nat_screen, NULL, NULL, NULL, NULL, nat_frame_rgba, NULL,
};

const PkBackend *pk_backend_native(void) { return &NATIVE_BACKEND; }
