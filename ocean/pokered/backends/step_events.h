#ifndef POKERED_STEP_EVENTS_H
#define POKERED_STEP_EVENTS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../pokered_backend.h"

typedef struct PkEventTracker {
    bool rebase, pending;
    bool wiped;
    bool escape_latched;
    uint32_t last;
    bool battle_active;
    uint16_t enemy_hp;
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

        if (active && s->escaped)
            t->escape_latched = true;
        bool fled = false;
        if (t->battle_active && !active) {
            fled = t->escape_latched;
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
        t->battle_active = active;
        t->enemy_hp = s->enemy_mon.hp;
    }
    t->pending = t->rebase = false;
    s->step_events = t->last;
    s->cut_used = t->cut_fired;
}

#endif
