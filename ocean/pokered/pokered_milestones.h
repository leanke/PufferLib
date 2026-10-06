#ifndef POKERED_MILESTONES_H
#define POKERED_MILESTONES_H

#include <pthread.h>

#define PKMS_BADGES 8
#define PKMS_KEY_ITEMS 15
#define PKMS_SLOTS (PKMS_BADGES + PKMS_KEY_ITEMS)
#define PKMS_MAX_PER_SLOT 64

#define PKRED_ITEM_ITEMFINDER 0x47u
#define PKRED_ITEM_OLD_ROD 0x4Cu
#define PKRED_ITEM_GOOD_ROD 0x4Du
#define PKRED_ITEM_SUPER_ROD 0x4Eu

static const uint8_t PKMS_ITEM_IDS[PKMS_KEY_ITEMS] = {
    PKRED_ITEM_OAKS_PARCEL, PKRED_ITEM_TOWN_MAP, PKRED_ITEM_BIKE_VOUCHER, PKRED_ITEM_BICYCLE,
    PKRED_ITEM_S_S_TICKET, PKRED_ITEM_SECRET_KEY, PKRED_ITEM_GOLD_TEETH, PKRED_ITEM_CARD_KEY,
    PKRED_ITEM_COIN_CASE, PKRED_ITEM_ITEMFINDER, PKRED_ITEM_SILPH_SCOPE, PKRED_ITEM_POKE_FLUTE,
    PKRED_ITEM_LIFT_KEY, PKRED_ITEM_OLD_ROD, PKRED_ITEM_SUPER_ROD,
};

typedef struct {
    uint8_t *blobs[PKMS_MAX_PER_SLOT];
    int filled;
    uint32_t seen;
} PkMsSlot;

// Process-wide so every env/thread shares one pool; the first env to init fixes its geometry.
static struct {
    pthread_mutex_t lock;
    PkMsSlot slots[PKMS_SLOTS];
    size_t state_size;
    int per_slot;
} g_pkms = {PTHREAD_MUTEX_INITIALIZER};

static void pkms_init(size_t state_size, int per_slot) {
    pthread_mutex_lock(&g_pkms.lock);
    if (g_pkms.state_size == 0) {
        g_pkms.state_size = state_size;
        g_pkms.per_slot = per_slot < 1 ? 1 : per_slot > PKMS_MAX_PER_SLOT ? PKMS_MAX_PER_SLOT : per_slot;
    }
    pthread_mutex_unlock(&g_pkms.lock);
}

// Reservoir sampling: each of the n captures of a slot ends up kept with equal probability.
static bool pkms_wants(int slot, unsigned *rng) {
    pthread_mutex_lock(&g_pkms.lock);
    PkMsSlot *s = &g_pkms.slots[slot];
    s->seen++;
    bool keep = s->filled < g_pkms.per_slot ||
                (uint32_t)(rand_r(rng) % s->seen) < (uint32_t)g_pkms.per_slot;
    pthread_mutex_unlock(&g_pkms.lock);
    return keep;
}

static void pkms_commit(int slot, const uint8_t *blob, unsigned *rng) {
    pthread_mutex_lock(&g_pkms.lock);
    PkMsSlot *s = &g_pkms.slots[slot];
    int idx = s->filled < g_pkms.per_slot ? s->filled++ : (int)(rand_r(rng) % g_pkms.per_slot);
    if (!s->blobs[idx])
        s->blobs[idx] = (uint8_t *)malloc(g_pkms.state_size);
    memcpy(s->blobs[idx], blob, g_pkms.state_size);
    pthread_mutex_unlock(&g_pkms.lock);
}

static int pkms_slots_filled(void) {
    pthread_mutex_lock(&g_pkms.lock);
    int n = 0;
    for (int i = 0; i < PKMS_SLOTS; i++)
        n += g_pkms.slots[i].filled > 0;
    pthread_mutex_unlock(&g_pkms.lock);
    return n;
}

// Uniform over filled slots, then over the states in the slot, so rare late milestones are not swamped.
static bool pkms_sample(uint8_t *out, unsigned *rng) {
    pthread_mutex_lock(&g_pkms.lock);
    int filled[PKMS_SLOTS], n = 0;
    for (int i = 0; i < PKMS_SLOTS; i++)
        if (g_pkms.slots[i].filled > 0)
            filled[n++] = i;
    bool ok = n > 0;
    if (ok) {
        PkMsSlot *s = &g_pkms.slots[filled[rand_r(rng) % n]];
        memcpy(out, s->blobs[rand_r(rng) % s->filled], g_pkms.state_size);
    }
    pthread_mutex_unlock(&g_pkms.lock);
    return ok;
}

static bool pkms_active(const Env *env) {
    return env->milestones_enabled && (env->be->caps & PK_CAP_STATE_SNAPSHOT);
}

static bool pkms_bag_has(const PkSnapshot *s, uint8_t item) {
    for (int i = 0; i < s->bag_count; i++)
        if (s->bag[i].item == item)
            return true;
    return false;
}

static void pkms_capture(Env *env, int slot, const char *what) {
    if (!pkms_wants(slot, &env->rng))
        return;
    if (!env->ms_buf)
        env->ms_buf = (uint8_t *)malloc(g_pkms.state_size);
    if (!env->be->state_save(env->impl, env->ms_buf))
        return;
    pkms_commit(slot, env->ms_buf, &env->rng);
    if (env->verbose)
        printf("Milestone saved: %s\n", what);
}

static void pkms_check(Env *env) {
    const PkSnapshot *cur = &env->cur, *prev = &env->prev;
    uint8_t gained = cur->badges & (uint8_t)~prev->badges;
    for (int b = 0; b < PKMS_BADGES; b++)
        if (gained & (1u << b))
            pkms_capture(env, b, "badge");
    for (int i = 0; i < PKMS_KEY_ITEMS; i++)
        if (pkms_bag_has(cur, PKMS_ITEM_IDS[i]) && !pkms_bag_has(prev, PKMS_ITEM_IDS[i]))
            pkms_capture(env, PKMS_BADGES + i, "key item");
}

static void pkms_try_reset(Env *env) {
    if (!pkms_active(env))
        return;
    env->reset_from_milestone = false;
    if (env->milestone_reset_prob <= 0.0f ||
        (float)rand_r(&env->rng) / (float)RAND_MAX >= env->milestone_reset_prob)
        return;
    if (!env->ms_buf)
        env->ms_buf = (uint8_t *)malloc(g_pkms.state_size);
    if (pkms_sample(env->ms_buf, &env->rng) && env->be->state_load(env->impl, env->ms_buf))
        env->reset_from_milestone = true;
}

#endif
