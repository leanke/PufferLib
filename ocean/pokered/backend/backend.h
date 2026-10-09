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
#define PK_EVENT_FLAG_BYTES 320
#define PK_TILE_MAP_W 20
#define PK_TILE_MAP_H 18
#define PK_TILE_MAP_CELLS (PK_TILE_MAP_W * PK_TILE_MAP_H)
#define PK_SPRITES 16

typedef struct {
    uint8_t species, level;
    uint16_t hp, max_hp;
    uint8_t moves[4];
    uint8_t pp[4];
} PkMon;

typedef struct {
    uint8_t x, y, map_n;
    uint8_t facing;
    uint8_t badges;
    uint8_t party_count;
    uint8_t box_count;
    PkMon party[6];
    uint8_t pokedex_owned_count, pokedex_seen_count;
    float hp_fraction;

    uint8_t battle_result;

    int8_t in_battle;
    PkMon battle_mon;
    PkMon enemy_mon;

    uint8_t bag_count;
    struct { uint8_t item, count; } bag[20];

    uint8_t surfing, strength_active, used_fly, dark_cave, cut_used;
    uint32_t map_block_hash;

    uint8_t event_flags[PK_EVENT_FLAG_BYTES];

    uint8_t tile_map[PK_TILE_MAP_CELLS];
    struct { uint8_t picture, image, y, x; } sprites[PK_SPRITES];

    uint32_t step_events;
} PkSnapshot;

typedef enum {
    PK_EV_BLACKOUT = 1u << 0,
    PK_EV_BATTLE_WON = 1u << 1,
    PK_EV_BATTLE_FLED = 1u << 2,
} PkStepEvent;

enum {
    PK_HM_USE_CUT = 1u << 0,
    PK_HM_USE_SURF = 1u << 1,
    PK_HM_USE_STRENGTH = 1u << 2,
    PK_HM_USE_FLASH = 1u << 3,
    PK_HM_USE_FLY = 1u << 4,
};

typedef struct {
    unsigned env_id;
    bool verbose, headless, screen_obs_enabled;
    int frameskip, press_frames;
    char rom_path[256];
    char state_path[256];
    bool pkstate_cache_enabled;
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

typedef struct PkRam {
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t val);
} PkRam;

typedef struct PkBackend {
    const char *name;

    void *(*create)(const PkBackendConfig *cfg, const PkOptions *opts);
    void (*destroy)(void *impl);

    void (*acquire)(void *impl);
    void (*release)(void *impl);

    void (*reset)(void *impl, bool full_reset);

    void (*warmup)(void *impl);

    void (*step)(void *impl, unsigned buttons);

    void (*snapshot)(void *impl, PkSnapshot *out);

    uint8_t (*peek)(void *impl, uint16_t addr);
    void (*poke)(void *impl, uint16_t addr, uint8_t val);

    void (*screen)(void *impl, float *out);

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

#define SCREEN_WIDTH 160
#define SCALED_WIDTH 80
#define SCALED_HEIGHT 72
#define SCALED_PIXELS (SCALED_WIDTH * SCALED_HEIGHT)

#define TILE_OBS_OFFSET 0
#define SPRITE_OBS_OFFSET 360
#define TILE_SPRITE_NPC 1
#define TILE_SPRITE_PLAYER 2

#define PARTY_SIZE 6
#define MON_FIELDS 3
#define PARTY_OBS (PARTY_SIZE * MON_FIELDS)

#define BATTLE_TYPE_NONE 0
#define BATTLE_TYPE_WILD 1
#define BATTLE_TYPE_TRAINER 2
#define BATTLE_TYPE_COUNT 3
#define BATTLE_OBS (1 + 2 * MON_FIELDS)
#define BATTLE_PLAYER_MON_OFFSET 1
#define BATTLE_ENEMY_MON_OFFSET (1 + MON_FIELDS)

#define BAG_SLOTS 20
#define BAG_FIELDS 2
#define BAG_OBS (BAG_SLOTS * BAG_FIELDS)

#define BLOCK_PIXELS 16
#define BLOCKS_WIDE (SCREEN_WIDTH / BLOCK_PIXELS)
#define BLOCKS_TALL (144 / BLOCK_PIXELS)
#define PLAYER_BLOCK_COL 4
#define PLAYER_BLOCK_ROW 4

#define VISITED_MASK_OBS (BLOCKS_WIDE * BLOCKS_TALL)

#ifdef POKERED_OBS_U8
#define PK_OBS_HP_SCALE 255.0f
#else
#define PK_OBS_HP_SCALE 1.0f
#endif

#define TOTAL_OBSERVATIONS (SCALED_PIXELS + VISITED_MASK_OBS + BATTLE_OBS + PARTY_OBS + BAG_OBS)
#define VISITED_MASK_OFFSET SCALED_PIXELS
#define BATTLE_OBS_OFFSET (VISITED_MASK_OFFSET + VISITED_MASK_OBS)
#define PARTY_OBS_OFFSET (BATTLE_OBS_OFFSET + BATTLE_OBS)
#define BAG_OBS_OFFSET (PARTY_OBS_OFFSET + PARTY_OBS)

typedef enum {
    PKRED_ACTION_A = 0,
    PKRED_ACTION_B,
    PKRED_ACTION_RIGHT,
    PKRED_ACTION_LEFT,
    PKRED_ACTION_UP,
    PKRED_ACTION_DOWN,
    PKRED_ACTION_START,
    PKRED_ACTION_SELECT,
    PKRED_ACTION_COUNT
} PokeredAction;

#define PK_BTN_A      0x01u
#define PK_BTN_B      0x02u
#define PK_BTN_SELECT 0x04u
#define PK_BTN_START  0x08u
#define PK_BTN_RIGHT  0x10u
#define PK_BTN_LEFT   0x20u
#define PK_BTN_UP     0x40u
#define PK_BTN_DOWN   0x80u

static inline unsigned pk_action_buttons(int action) {
    switch (action) {
    case PKRED_ACTION_A: return PK_BTN_A;
    case PKRED_ACTION_B: return PK_BTN_B;
    case PKRED_ACTION_RIGHT: return PK_BTN_RIGHT;
    case PKRED_ACTION_LEFT: return PK_BTN_LEFT;
    case PKRED_ACTION_UP: return PK_BTN_UP;
    case PKRED_ACTION_DOWN: return PK_BTN_DOWN;
    case PKRED_ACTION_START: return PK_BTN_START;
    case PKRED_ACTION_SELECT: return PK_BTN_SELECT;
    default: return 0;
    }
}

#endif
