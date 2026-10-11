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

#define PKMS_BADGES 8
#define PKMS_MAX_PER_SLOT 64

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
#define PKMS_SLOTS (PKMS_BADGES + PKMS_KEY_ITEMS)

typedef struct {
    uint8_t *blob;
    uint16_t events;
    uint8_t badges;
} PkPoolState;

typedef struct {
    PkPoolState states[PKMS_MAX_PER_SLOT];
    int filled;
    uint32_t seen;
} PkPoolSlot;

typedef struct PkMilestonePool {
    pthread_mutex_t lock;
    PkPoolSlot slots[PKMS_SLOTS];
    size_t state_size;
    int per_slot;
} PkMilestonePool;

typedef float (*PkPoolWeightFn)(void *ctx, int slot, const PkPoolState *state);

static PkMilestonePool g_pk_milestones = {PTHREAD_MUTEX_INITIALIZER};

static void pk_pool_init(PkMilestonePool *p, size_t state_size, int per_slot) {
    pthread_mutex_lock(&p->lock);
    if (p->state_size == 0) {
        p->state_size = state_size;
        p->per_slot = per_slot < 1 ? 1 : per_slot > PKMS_MAX_PER_SLOT ? PKMS_MAX_PER_SLOT : per_slot;
    }
    pthread_mutex_unlock(&p->lock);
}

static bool pk_pool_wants(PkMilestonePool *p, int slot, unsigned *rng) {
    pthread_mutex_lock(&p->lock);
    PkPoolSlot *s = &p->slots[slot];
    s->seen++;
    bool keep = s->filled < p->per_slot || (uint32_t)(rand_r(rng) % s->seen) < (uint32_t)p->per_slot;
    pthread_mutex_unlock(&p->lock);
    return keep;
}

static void pk_pool_commit(PkMilestonePool *p, int slot, const uint8_t *blob, uint16_t events, uint8_t badges,
                           unsigned *rng) {
    pthread_mutex_lock(&p->lock);
    PkPoolSlot *s = &p->slots[slot];
    int idx = s->filled < p->per_slot ? s->filled++ : (int)(rand_r(rng) % p->per_slot);
    PkPoolState *st = &s->states[idx];
    if (!st->blob)
        st->blob = (uint8_t *)malloc(p->state_size);
    memcpy(st->blob, blob, p->state_size);
    st->events = events;
    st->badges = badges;
    pthread_mutex_unlock(&p->lock);
}

static int pk_pool_slots_filled(PkMilestonePool *p) {
    pthread_mutex_lock(&p->lock);
    int n = 0;
    for (int i = 0; i < PKMS_SLOTS; i++)
        n += p->slots[i].filled > 0;
    pthread_mutex_unlock(&p->lock);
    return n;
}

static bool pk_pool_sample(PkMilestonePool *p, uint8_t *out, unsigned *rng, PkPoolWeightFn weight, void *ctx,
                           int *slot_out) {
    pthread_mutex_lock(&p->lock);
    const PkPoolState *pick = NULL;
    int pick_slot = -1;
    if (!weight) {
        int filled[PKMS_SLOTS], n = 0;
        for (int i = 0; i < PKMS_SLOTS; i++)
            if (p->slots[i].filled > 0)
                filled[n++] = i;
        if (n > 0) {
            pick_slot = filled[rand_r(rng) % n];
            PkPoolSlot *s = &p->slots[pick_slot];
            pick = &s->states[rand_r(rng) % s->filled];
        }
    } else {
        double total = 0.0;
        for (int i = 0; i < PKMS_SLOTS; i++)
            for (int k = 0; k < p->slots[i].filled; k++) {
                float w = weight(ctx, i, &p->slots[i].states[k]);
                total += w > 0.0f ? w : 0.0f;
            }
        if (total > 0.0) {
            double r = (double)rand_r(rng) / ((double)RAND_MAX + 1.0) * total;
            for (int i = 0; i < PKMS_SLOTS && (!pick || r >= 0.0); i++)
                for (int k = 0; k < p->slots[i].filled && (!pick || r >= 0.0); k++) {
                    float w = weight(ctx, i, &p->slots[i].states[k]);
                    if (w <= 0.0f)
                        continue;
                    pick = &p->slots[i].states[k];
                    pick_slot = i;
                    r -= w;
                }
        }
    }
    if (pick) {
        memcpy(out, pick->blob, p->state_size);
        if (slot_out)
            *slot_out = pick_slot;
    }
    pthread_mutex_unlock(&p->lock);
    return pick != NULL;
}

typedef struct PkEventTracker {
    bool rebase, pending;
    bool wiped;
    bool escape_latched;
    uint32_t last;
    bool battle_active;
    uint16_t enemy_hp;
    uint16_t mons_at_battle_start;
    bool cut_fired, cut_swapped;
    uint16_t cut_wait;
} PkEventTracker;

static inline void pk_events_rebase(PkEventTracker *t) {
    t->rebase = true;
    t->pending = false;
    t->wiped = false;
    t->escape_latched = false;
    t->last = 0;
    t->cut_fired = t->cut_swapped = false;
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

        if (s->party_count == 0 || s->hp_fraction > 0.0f) {
            t->wiped = false;
        } else {
            if (!t->wiped) ev |= PK_EV_BLACKOUT;
            t->wiped = true;
        }
        t->last = ev;
    }
    if (t->pending || t->rebase) {
        if (active && (!t->battle_active || t->rebase))
            t->mons_at_battle_start = (uint16_t)(s->party_count + s->box_count);
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
    t->cut_fired = false;
    if (!ram->read(ram->ctx, PKRED_ADDR_CUT_TILE)) {
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
        t->cut_fired = true;
        pk_ram_clear_cut(ram, t);
    } else if (++t->cut_wait > PK_CUT_STALE_STEPS) {
        pk_ram_clear_cut(ram, t);
    }
}

#define PK_VIRIDIAN_CITY_MAP 0x01

typedef struct PkScenario {
    bool disable_wild_until_badge;
    bool route22_rival_beaten, route22_rival_2nd_beaten;
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

static inline void pk_ram_set_missable_hidden(const PkRam *ram, uint8_t missable_index, bool hidden) {
    uint16_t addr = PKRED_ADDR_MISSABLE_OBJECT_FLAGS + (missable_index >> 3);
    uint8_t bit = missable_index & 7;
    uint8_t byte = ram->read(ram->ctx, addr);
    ram->write(ram->ctx, addr, hidden ? (byte | (1 << bit)) : (byte & ~(1 << bit)));
}

static inline void pk_ram_apply_scenario(const PkRam *ram, const PkScenario *cfg, const PkSnapshot *last) {
    if (cfg->disable_wild_until_badge) {
        uint8_t flags = ram->read(ram->ctx, PKRED_ADDR_WD72E);
        if (last->badges == 0)
            ram->write(ram->ctx, PKRED_ADDR_WD72E, flags | (1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
        else
            ram->write(ram->ctx, PKRED_ADDR_WD72E, flags & ~(1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
    }

    if (cfg->route22_rival_beaten || cfg->route22_rival_2nd_beaten) {
        uint8_t flags = ram->read(ram->ctx, PKRED_ADDR_ROUTE22_RIVAL_EVENTS);
        bool trigger_1st_was_set = flags & (1 << PKRED_ROUTE22_RIVAL_TRIGGER_1ST_BIT);
        bool trigger_2nd_was_set = flags & (1 << PKRED_ROUTE22_RIVAL_TRIGGER_2ND_BIT);
        if (cfg->route22_rival_beaten) {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_TRIGGER_1ST_BIT);
            flags |= (1 << PKRED_ROUTE22_RIVAL_BEAT_1ST_BIT);
            if (trigger_1st_was_set)
                flags &= ~(1 << PKRED_ROUTE22_RIVAL_WANTS_BATTLE_BIT);
        }
        if (cfg->route22_rival_2nd_beaten) {
            flags &= ~(1 << PKRED_ROUTE22_RIVAL_TRIGGER_2ND_BIT);
            flags |= (1 << PKRED_ROUTE22_RIVAL_BEAT_2ND_BIT);
            if (trigger_2nd_was_set)
                flags &= ~(1 << PKRED_ROUTE22_RIVAL_WANTS_BATTLE_BIT);
        }
        ram->write(ram->ctx, PKRED_ADDR_ROUTE22_RIVAL_EVENTS, flags);
        if (cfg->route22_rival_beaten)
            pk_ram_set_missable_hidden(ram, PKRED_MISSABLE_HS_ROUTE_22_RIVAL_1, true);
        if (cfg->route22_rival_2nd_beaten)
            pk_ram_set_missable_hidden(ram, PKRED_MISSABLE_HS_ROUTE_22_RIVAL_2, true);
    }

    if (last->map_n == PK_VIRIDIAN_CITY_MAP && ram->read(ram->ctx, PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT) == 1) {
        ram->write(ram->ctx, PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT, 0);
        ram->write(ram->ctx, PKRED_ADDR_BATTLE_TYPE, 0);
    }
}

typedef struct PkEpisode {
    int32_t max_length;
    bool full_reset;
    bool milestones_enabled;
    float milestone_reset_prob;
    int milestone_states_per_slot;
    float milestone_progress_bias;
    PkMilestonePool *pool;
    uint8_t *buf;
    bool from_milestone;
    int start_slot;
    int blackouts;
} PkEpisode;

static void pk_episode_init(PkEpisode *ep, const PkBackend *be) {
    ep->start_slot = -1;
    if (!ep->milestones_enabled)
        return;
    ep->pool = &g_pk_milestones;
    pk_pool_init(ep->pool, be->state_size(), ep->milestone_states_per_slot);
}

static bool pk_bag_has(const PkSnapshot *s, uint8_t item) {
    for (int i = 0; i < s->bag_count; i++)
        if (s->bag[i].item == item)
            return true;
    return false;
}

static void pk_episode_capture_slot(PkEpisode *ep, int slot, const char *what, const PkSnapshot *cur,
                                    const PkBackend *be, void *impl, unsigned *rng, bool verbose) {
    if (!pk_pool_wants(ep->pool, slot, rng))
        return;
    if (!ep->buf)
        ep->buf = (uint8_t *)malloc(ep->pool->state_size);
    if (!be->state_save(impl, ep->buf))
        return;
    pk_pool_commit(ep->pool, slot, ep->buf, (uint16_t)pk_completed_events(cur->event_flags),
                   (uint8_t)__builtin_popcount(cur->badges), rng);
    if (verbose)
        printf("Milestone saved: %s\n", what);
}

static void pk_episode_capture(PkEpisode *ep, const PkSnapshot *cur, const PkSnapshot *prev, const PkBackend *be,
                               void *impl, unsigned *rng, bool verbose) {
    if (!ep->milestones_enabled)
        return;
    uint8_t gained = cur->badges & (uint8_t)~prev->badges;
    for (int b = 0; b < PKMS_BADGES; b++)
        if (gained & (1u << b))
            pk_episode_capture_slot(ep, b, "badge", cur, be, impl, rng, verbose);
    for (int i = 0; i < PKMS_KEY_ITEMS; i++)
        if (pk_bag_has(cur, PKMS_ITEM_IDS[i]) && !pk_bag_has(prev, PKMS_ITEM_IDS[i]))
            pk_episode_capture_slot(ep, PKMS_BADGES + i, "key item", cur, be, impl, rng, verbose);
}

static float pk_progress_weight(void *ctx, int slot, const PkPoolState *state) {
    (void)slot;
    return expf(*(const float *)ctx * (float)state->events / 100.0f);
}

static const uint8_t *pk_episode_pick_milestone(PkEpisode *ep, unsigned *rng) {
    ep->start_slot = -1;
    if (!ep->milestones_enabled)
        return NULL;
    ep->from_milestone = false;
    if (ep->milestone_reset_prob <= 0.0f || (float)rand_r(rng) / (float)RAND_MAX >= ep->milestone_reset_prob)
        return NULL;
    if (!ep->buf)
        ep->buf = (uint8_t *)malloc(ep->pool->state_size);
    float bias = ep->milestone_progress_bias;
    if (!pk_pool_sample(ep->pool, ep->buf, rng, bias != 0.0f ? pk_progress_weight : NULL, &bias, &ep->start_slot))
        return NULL;
    return ep->buf;
}

#endif
