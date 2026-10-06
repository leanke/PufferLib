#ifndef POKERED_BACKEND_H
#define POKERED_BACKEND_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PK_FRAME_W 160
#define PK_FRAME_H 144
#define PK_MAX_EVENTS 512

typedef struct {
    uint8_t species, level;
    uint16_t hp, max_hp;
    uint8_t moves[4];
} PkMon;

typedef struct {
    uint8_t x, y, map_n;
    uint8_t facing;
    uint8_t badges;
    uint8_t party_count;
    PkMon party[6];
    uint8_t pokedex_owned_count, pokedex_seen_count;
    float hp_fraction;

    uint8_t escaped;

    int8_t in_battle;
    PkMon battle_mon;
    PkMon enemy_mon;

    uint8_t bag_count;
    struct { uint8_t item, count; } bag[20];

    uint8_t events[PK_MAX_EVENTS];

    uint32_t step_events;
} PkSnapshot;

typedef enum {
    PK_EV_BLACKOUT = 1u << 0,
    PK_EV_BATTLE_WON = 1u << 1,
    PK_EV_BATTLE_FLED = 1u << 2,
} PkStepEvent;

typedef enum {
    PK_CAP_FRAME_RGBA = 1u << 1,
    PK_CAP_QUICKSAVE = 1u << 2,
    PK_CAP_BLACKOUT = 1u << 3,
    PK_CAP_EXPORT_STATE = 1u << 4,
    PK_CAP_STATE_SNAPSHOT = 1u << 5,
} PkCapability;

typedef struct {
    unsigned env_id;
    bool verbose, headless, screen_obs_enabled;
    int frameskip, press_frames;
    char rom_path[256];
    char state_path[256];
    bool pkstate_cache_enabled;

    bool disable_wild_until_badge;
    bool route22_rival_beaten, route22_rival_2nd_beaten;
} PkBackendConfig;

typedef struct PkOptions {
    void *ctx;
    bool (*lookup)(void *ctx, const char *key, double *num, const char **str);
} PkOptions;

static inline double pk_opt_num(const PkOptions *o, const char *key, double def) {
    double num = 0;
    return o && o->lookup && o->lookup(o->ctx, key, &num, NULL) ? num : def;
}
static inline bool pk_opt_bool(const PkOptions *o, const char *key, bool def) { return pk_opt_num(o, key, def ? 1.0 : 0.0) != 0.0; }
static inline int pk_opt_int(const PkOptions *o, const char *key, int def) { return (int)pk_opt_num(o, key, def); }
static inline float pk_opt_float(const PkOptions *o, const char *key, float def) { return (float)pk_opt_num(o, key, def); }
static inline const char *pk_opt_str(const PkOptions *o, const char *key, const char *def) {
    const char *str = NULL;
    double num = 0;
    return o && o->lookup && o->lookup(o->ctx, key, &num, &str) && str && str[0] ? str : def;
}

typedef struct { const char *key; const char *str; double num; } PkKV;
typedef struct { const PkKV *kv; int n; } PkKVList;
static inline bool pk_kv_lookup(void *ctx, const char *key, double *num, const char **str) {
    const PkKVList *l = (const PkKVList *)ctx;
    for (int i = 0; i < l->n; i++) {
        const char *k = l->kv[i].key;
        int j = 0;
        while (k[j] && key[j] && k[j] == key[j]) j++;
        if (k[j] || key[j]) continue;
        if (num) *num = l->kv[i].num;
        if (str) *str = l->kv[i].str;
        return true;
    }
    return false;
}

struct PkState;

typedef struct PkBackend {
    const char *name;
    uint32_t caps;
    uint32_t buttons;

    void *(*create)(const PkBackendConfig *cfg, const PkOptions *opts);
    void (*destroy)(void *impl);

    void (*acquire)(void *impl);
    void (*release)(void *impl);

    void (*reset)(void *impl, bool full_reset, unsigned *rng);

    void (*warmup)(void *impl);

    void (*step)(void *impl, int action, const PkSnapshot *last);

    void (*snapshot)(void *impl, PkSnapshot *out);

    void (*screen)(void *impl, float *out);

    void (*blackout)(void *impl);
    bool (*frame_rgba)(void *impl, uint8_t *rgba);
    bool (*quicksave)(void *impl, const char *path);
    bool (*export_state)(void *impl, struct PkState *out);
    size_t (*state_size)(void);
    bool (*state_save)(void *impl, void *buf);
    bool (*state_load)(void *impl, const void *buf);
} PkBackend;

void pk_backend_register(const PkBackend *be);
const PkBackend *pk_backend_find(const char *name);
int pk_backend_count(void);
const PkBackend *pk_backend_at(int index);

#define PK_REGISTER_BACKEND(be) \
    __attribute__((constructor)) static void pk_register_backend_##be(void) { pk_backend_register(&(be)); }

int pk_event_count(void);
const char *pk_event_name(int idx);
int pk_event_address(int idx);
int pk_event_bit(int idx);

#ifdef __cplusplus
}
#endif

#endif
