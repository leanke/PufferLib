#ifndef POKERED_MILESTONES_H
#define POKERED_MILESTONES_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"

#define MILESTONE_CAPACITY 600
#if __STDC_VERSION__ >= 201112L
#  include <assert.h>
   static_assert(sizeof(EVENT_LIST) / sizeof(Event) <= MILESTONE_CAPACITY,
                 "MILESTONE_CAPACITY must cover every EVENT_LIST entry");
#endif


typedef struct {
    uint8_t map_id;
    const char *name;
    bool is_town;
} MapMilestone;

typedef struct {
    uint8_t map_id;
    const char *name;
} TownMap;

static const TownMap TOWN_MAPS[] = {
   // {0x00, "Pallet Town"},
    {0x01, "Viridian City"},
    {0x02, "Pewter City"},
    {0x03, "Cerulean City"},
    {0x04, "Lavender Town"},
    {0x05, "Vermilion City"},
    {0x06, "Celadon City"},
    {0x07, "Fuchsia City"},
    {0x08, "Cinnabar Island"},
    {0x09, "Indigo Plateau"},
    {0x0A, "Saffron City"},
};
#define TOWN_MAP_COUNT (sizeof(TOWN_MAPS) / sizeof(TownMap))

static const MapMilestone MAP_MILESTONES[] = {
   // {0x33, "Reached Viridian Forest", false},
   // {0x00, "Reached Pallet Town", true},
    {0x01, "Reached Viridian City", true},
    {0x02, "Reached Pewter City", true},
    {0x03, "Reached Cerulean City", true},
    {0x04, "Reached Lavender Town", true},
    {0x05, "Reached Vermilion City", true},
    {0x06, "Reached Celadon City", true},
    {0x07, "Reached Fuchsia City", true},
    {0x08, "Reached Cinnabar Island", true},
    {0x09, "Reached Indigo Plateau", true},
    {0x0A, "Reached Saffron City", true},
};
#define MAP_MILESTONE_COUNT (sizeof(MAP_MILESTONES) / sizeof(MapMilestone))
#define MAP_MILESTONE_SLOT_BASE 500  // must be > EVENT_COUNT (~496); see static_assert below
#if __STDC_VERSION__ >= 201112L
   static_assert(sizeof(EVENT_LIST) / sizeof(Event) <= MAP_MILESTONE_SLOT_BASE,
                 "MAP_MILESTONE_SLOT_BASE must sit above every EVENT_LIST index");
   static_assert(MAP_MILESTONE_SLOT_BASE + MAP_MILESTONE_COUNT <= MILESTONE_CAPACITY,
                 "MILESTONE_CAPACITY must also cover MAP_MILESTONES");
#endif

typedef struct {
    uint8_t *state_buf;   // g_gb_pool.state_size bytes; NULL until filled
    bool filled;
} MilestoneSlot;

typedef struct {
    MilestoneSlot slots[MILESTONE_CAPACITY];
    int filled_indices[MILESTONE_CAPACITY];
    int filled_count;
    size_t state_size;
    pthread_mutex_t lock;
} MilestonePool;

static MilestonePool g_milestone_pool = {
    .filled_count = 0,
    .state_size = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

// Called once state_size is known (after gb_pool_init). Idempotent.
static inline void milestone_pool_set_state_size(size_t state_size) {
    g_milestone_pool.state_size = state_size;
}

static inline void milestone_pool_try_capture(int event_idx, const uint8_t *state_buf) {
    if (event_idx < 0 || event_idx >= MILESTONE_CAPACITY || g_milestone_pool.state_size == 0)
        return;

    MilestoneSlot *slot = &g_milestone_pool.slots[event_idx];
    if (slot->filled)  // fast unlocked check; a stale false just costs one redundant capture race
        return;

    pthread_mutex_lock(&g_milestone_pool.lock);
    if (!slot->filled) {
        slot->state_buf = (uint8_t *)malloc(g_milestone_pool.state_size);
        memcpy(slot->state_buf, state_buf, g_milestone_pool.state_size);
        slot->filled = true;
        g_milestone_pool.filled_indices[g_milestone_pool.filled_count++] = event_idx;
    }
    pthread_mutex_unlock(&g_milestone_pool.lock);
}

static inline bool milestone_pool_sample(uint8_t *out, unsigned int *rng_state) {
    pthread_mutex_lock(&g_milestone_pool.lock);
    int count = g_milestone_pool.filled_count;
    int event_idx = count > 0 ? g_milestone_pool.filled_indices[rand_r(rng_state) % count] : -1;
    MilestoneSlot *slot = event_idx >= 0 ? &g_milestone_pool.slots[event_idx] : NULL;
    if (slot) memcpy(out, slot->state_buf, g_milestone_pool.state_size);
    pthread_mutex_unlock(&g_milestone_pool.lock);
    return slot != NULL;
}

static inline int milestone_pool_size(void) {
    return g_milestone_pool.filled_count;
}

static inline void milestone_pool_free(void) {
    pthread_mutex_lock(&g_milestone_pool.lock);
    for (int i = 0; i < MILESTONE_CAPACITY; i++) {
        free(g_milestone_pool.slots[i].state_buf);
        g_milestone_pool.slots[i].state_buf = NULL;
        g_milestone_pool.slots[i].filled = false;
    }
    g_milestone_pool.filled_count = 0;
    pthread_mutex_unlock(&g_milestone_pool.lock);
}

#endif /* POKERED_MILESTONES_H */
