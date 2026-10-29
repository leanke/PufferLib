#ifndef POKERED_UTILS_H
#define POKERED_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include "ram_map.h"
#include "events.h"
#include "../backend/backend.h"

#define MAX_MAPS 256
#define MAX_X 128
#define MAX_Y 128
#define VISITED_COORDS_SIZE (MAX_MAPS * MAX_X * MAX_Y)
#define VISITED_BYTES (VISITED_COORDS_SIZE / 8)

static inline uint32_t coord_index(uint8_t map, uint8_t x, uint8_t y) {
    uint32_t cx = x < MAX_X ? x : MAX_X - 1;
    uint32_t cy = y < MAX_Y ? y : MAX_Y - 1;
    return (uint32_t)map * (MAX_X * MAX_Y) + cx * MAX_Y + cy;
}

static inline bool vbit_get(const uint8_t *bits, uint32_t idx) {
    return (bits[idx >> 3] >> (idx & 7)) & 1;
}

static inline bool vbit_test_set(uint8_t *bits, uint32_t idx) {
    uint8_t mask = (uint8_t)(1u << (idx & 7));
    if (bits[idx >> 3] & mask)
        return false;
    bits[idx >> 3] |= mask;
    return true;
}

#define PKRED_ITEM_ITEMFINDER 0x47u
#define PKRED_ITEM_OLD_ROD 0x4Cu
#define PKRED_ITEM_GOOD_ROD 0x4Du
#define PKRED_ITEM_SUPER_ROD 0x4Eu

static const uint8_t PKMS_ITEM_IDS[] = {
    // PKRED_ITEM_BIKE_VOUCHER, 
    PKRED_ITEM_BICYCLE,
    // PKRED_ITEM_S_S_TICKET, 
    PKRED_ITEM_SECRET_KEY, 
    PKRED_ITEM_GOLD_TEETH, 
    PKRED_ITEM_CARD_KEY,
    PKRED_ITEM_COIN_CASE, 
    PKRED_ITEM_SILPH_SCOPE, 
    PKRED_ITEM_POKE_FLUTE,
    PKRED_ITEM_LIFT_KEY,
};
#define PKMS_KEY_ITEMS ((int)(sizeof(PKMS_ITEM_IDS) / sizeof(PKMS_ITEM_IDS[0])))

static inline bool pk_ghost_battle(const PkSnapshot *s) {
    if (s->in_battle != 1 || s->map_n < PKRED_MAP_POKEMON_TOWER_1F || s->map_n > PKRED_MAP_POKEMON_TOWER_7F)
        return false;
    for (int i = 0; i < s->bag_count; i++)
        if (s->bag[i].item == PKRED_ITEM_SILPH_SCOPE)
            return false;
    return true;
}

typedef struct PkEventTracker {
    bool rebase, pending;
    bool wiped;
    bool escape_latched;
    uint32_t last;
    bool battle_active;
    uint16_t enemy_hp;
    uint16_t mons_at_battle_start;
    bool ghost_battle;
    uint8_t cut_fired;
    bool cut_swapped;
    uint16_t cut_wait;
} PkEventTracker;

static inline void pk_events_rebase(PkEventTracker *t) {
    t->rebase = true;
    t->pending = false;
    t->wiped = false;
    t->escape_latched = false;
    t->ghost_battle = false;
    t->last = 0;
    t->cut_fired = 0;
    t->cut_swapped = false;
    t->cut_wait = 0;
}

static inline void pk_events_stepped(PkEventTracker *t) { t->pending = true; }

static inline void pk_events_apply(PkEventTracker *t, PkSnapshot *s) {
    bool active = s->in_battle == 1 || s->in_battle == 2;
    if (t->pending && !t->rebase) {
        uint32_t ev = 0;

        bool won = t->battle_active && !active && t->enemy_hp == 0 && s->hp_fraction > 0.0f;
        if (won) ev |= PK_EV_BATTLE_WON;

        if (active && s->battle_result == PKRED_BATTLE_RESULT_RAN)
            t->escape_latched = true;
        bool fled = false;
        if (t->battle_active && !active) {
            bool caught = s->party_count + s->box_count > t->mons_at_battle_start;
            fled = t->escape_latched && !caught && !won;
            t->escape_latched = false;
        } else if (!active) {
            t->escape_latched = false;
        }
        if (fled) ev |= PK_EV_BATTLE_FLED;
        bool ghost = active && !t->battle_active ? pk_ghost_battle(s) : t->ghost_battle;
        if (ghost && (active || t->battle_active)) ev |= PK_EV_GHOST_BATTLE;

        if (s->party_count == 0 || s->hp_fraction > 0.0f) {
            t->wiped = false;
        } else {
            if (!t->wiped) ev |= PK_EV_BLACKOUT;
            t->wiped = true;
        }
        t->last = ev;
    }
    if (t->pending || t->rebase) {
        if (active && (!t->battle_active || t->rebase)) {
            t->mons_at_battle_start = (uint16_t)(s->party_count + s->box_count);
            t->ghost_battle = pk_ghost_battle(s);
        }
        t->battle_active = active;
        t->enemy_hp = s->enemy_mon.hp;
    }
    t->pending = t->rebase = false;
    s->step_events = t->last;
    s->cut_used = t->cut_fired;
}

#define PK_CUT_STALE_STEPS 64

static inline void pk_ram_clear_cut(const PkRam *ram, PkEventTracker *t) {
    ram->write(ram->ctx, PKRED_ADDR_CUT_TILE, 0);
    t->cut_swapped = false;
    t->cut_wait = 0;
}

/* wCutTile is written only by a successful Cut and never cleared by the game, so it is zeroed
   once the animation is over (tree block swapped, sprites no longer animating); the step that
   observes that is the one cut_used fires. Zeroing earlier would break AnimCut, which reads it. */
static inline void pk_ram_track_cut(const PkRam *ram, PkEventTracker *t, const PkSnapshot *last) {
    t->cut_fired = 0;
    uint8_t tile = ram->read(ram->ctx, PKRED_ADDR_CUT_TILE);
    if (!tile) {
        t->cut_swapped = false;
        t->cut_wait = 0;
        return;
    }
    if (!t->cut_swapped) {
        uint8_t blocks[PKRED_OVERWORLD_MAP_SIZE];
        for (int i = 0; i < PKRED_OVERWORLD_MAP_SIZE; i++)
            blocks[i] = ram->read(ram->ctx, PKRED_ADDR_OVERWORLD_MAP + i);
        t->cut_swapped = pkred_hash_bytes(blocks, PKRED_OVERWORLD_MAP_SIZE) != last->map_block_hash;
    }
    if (t->cut_swapped && ram->read(ram->ctx, PKRED_ADDR_UPDATE_SPRITES) != PKRED_SPRITES_ANIMATING) {
        t->cut_fired = tile;
        pk_ram_clear_cut(ram, t);
    } else if (++t->cut_wait > PK_CUT_STALE_STEPS) {
        pk_ram_clear_cut(ram, t);
    }
}

typedef struct PkScenario {
    uint8_t text_speed, battle_animation, battle_style;
    bool rng_randomize_on_reset;
} PkScenario;

static inline uint8_t pk_options_stored(uint8_t options, uint8_t text_speed, uint8_t animation, uint8_t style) {
    uint8_t delay = options & PKRED_OPTIONS_TEXT_DELAY_MASK;
    if (delay != 1 && delay != 3 && delay != 5)
        delay = 3;
    if (text_speed)
        delay = text_speed == 1 ? 1 : text_speed == 2 ? 3 : 5;
    bool animation_off = (options >> PKRED_OPTIONS_BATTLE_ANIMATION_BIT) & 1;
    bool style_set = (options >> PKRED_OPTIONS_BATTLE_SHIFT_BIT) & 1;
    if (animation)
        animation_off = animation == 2;
    if (style)
        style_set = style == 2;
    return (uint8_t)(delay | (animation_off << PKRED_OPTIONS_BATTLE_ANIMATION_BIT) |
                     (style_set << PKRED_OPTIONS_BATTLE_SHIFT_BIT));
}

static inline void pk_ram_prepare_start(const PkRam *ram, const PkScenario *cfg, unsigned *rng) {
    if (cfg->text_speed || cfg->battle_animation || cfg->battle_style)
        ram->write(ram->ctx, PKRED_ADDR_OPTIONS,
                   pk_options_stored(ram->read(ram->ctx, PKRED_ADDR_OPTIONS), cfg->text_speed,
                                     cfg->battle_animation, cfg->battle_style));
    if (cfg->rng_randomize_on_reset) {
        ram->write(ram->ctx, PKRED_ADDR_H_RANDOM_ADD, (uint8_t)rand_r(rng));
        ram->write(ram->ctx, PKRED_ADDR_H_RANDOM_SUB, (uint8_t)rand_r(rng));
    }
}

static inline void pk_ram_apply_scenario(const PkRam *ram, const PkScenario *cfg, const PkSnapshot *last) {
    (void)ram;
    (void)cfg;
    (void)last;
}

typedef struct PkEpisode {
    int32_t max_length;
    int32_t base_length;
    bool full_reset;
    bool milestone_extend_episode;
    bool milestone_key_items;
    int extensions;
    int blackouts;
} PkEpisode;

static bool pk_bag_has(const PkSnapshot *s, uint8_t item) {
    for (int i = 0; i < s->bag_count; i++)
        if (s->bag[i].item == item)
            return true;
    return false;
}

static int pk_milestones_held(const PkEpisode *ep, const PkSnapshot *cur) {
    int held = __builtin_popcount(cur->badges);
    if (ep->milestone_key_items)
        for (int i = 0; i < PKMS_KEY_ITEMS; i++)
            held += pk_bag_has(cur, PKMS_ITEM_IDS[i]);
    return held;
}

static void pk_episode_update_length(PkEpisode *ep, const PkSnapshot *cur) {
    if (!ep->milestone_extend_episode || ep->base_length <= 0)
        return;
    int held = pk_milestones_held(ep, cur);
    if (held <= ep->extensions)
        return;
    ep->extensions = held;
    int64_t len = (int64_t)ep->base_length * (1 + held);
    ep->max_length = len > INT32_MAX ? INT32_MAX : (int32_t)len;
}

static void pk_episode_begin(PkEpisode *ep, const PkSnapshot *cur) {
    ep->max_length = ep->base_length;
    ep->extensions = 0;
    pk_episode_update_length(ep, cur);
}

#endif
