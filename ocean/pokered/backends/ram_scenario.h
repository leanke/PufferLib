#ifndef POKERED_RAM_SCENARIO_H
#define POKERED_RAM_SCENARIO_H

#include <stdbool.h>
#include <stdint.h>

#include "../includes/ram_map.h"
#include "../pokered_backend.h"
#include "step_events.h"

#define PK_VIRIDIAN_CITY_MAP 0x01

typedef struct PkRam {
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t val);
} PkRam;

static inline void pk_ram_set_missable_hidden(const PkRam *ram, uint8_t missable_index, bool hidden) {
    uint16_t addr = PKRED_ADDR_MISSABLE_OBJECT_FLAGS + (missable_index >> 3);
    uint8_t bit = missable_index & 7;
    uint8_t byte = ram->read(ram->ctx, addr);
    ram->write(ram->ctx, addr, hidden ? (byte | (1 << bit)) : (byte & ~(1 << bit)));
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

static inline void pk_ram_apply_scenario(const PkRam *ram, const PkBackendConfig *cfg, const PkSnapshot *last) {
    if (cfg->disable_wild_until_badge) {
        uint8_t flags = ram->read(ram->ctx, PKRED_ADDR_WD72E);
        if (last->badges == 0)
            ram->write(ram->ctx, PKRED_ADDR_WD72E, flags | (1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
        else
            ram->write(ram->ctx, PKRED_ADDR_WD72E, flags & ~(1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
    }

    {
        uint8_t flags = ram->read(ram->ctx, PKRED_ADDR_ROUTE22_RIVAL_EVENTS);
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

#endif
