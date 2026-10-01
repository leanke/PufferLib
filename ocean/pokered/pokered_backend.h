#ifndef POKERED_BACKEND_H
#define POKERED_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PK_FRAME_W 160
#define PK_FRAME_H 144
#define PK_MAX_EVENTS 512

typedef struct {
    uint8_t species, level, status, type1, type2;
    uint16_t hp, max_hp;
    uint8_t moves[4];
    uint8_t pp[4];
} PkMon;

typedef struct {
    uint8_t x, y, map_n;
    uint8_t facing;
    uint8_t badges;
    uint8_t party_count;
    PkMon party[6];
    uint8_t pokedex_owned_count, pokedex_seen_count;
    float hp_fraction;
    uint8_t fainted_count;
    uint32_t money;
    uint8_t last_blackout_map;
    uint8_t num_bag_items;
    uint8_t bag_qty[256];

    int8_t in_battle;
    uint8_t battle_type;
    uint8_t selected_move;
    PkMon battle_mon;
    PkMon enemy_mon;

    uint8_t events[PK_MAX_EVENTS];
} PkSnapshot;

typedef struct {

    char rom_path[256];
    char state_path[256];
    int frameskip, press_frames;
    bool headless, audio_enabled, frame_render_skip_enabled;
    int vec_num_threads;
    float milestone_sample_prob;
    bool milestones_enabled, event_milestones_enabled, town_milestones_enabled;

    char assets_dir[256];
    int fixed_starter_species;
    bool screen_obs_enabled;

    bool disable_wild_until_badge;
    bool route22_rival_beaten, route22_rival_2nd_beaten;

    bool nickname_prompt_enabled;
    bool verbose;
    unsigned env_id;
} PkBackendConfig;

typedef struct PkBackend {
    const char *name;
    void *(*create)(const PkBackendConfig *cfg);
    void (*destroy)(void *impl);

    void (*acquire)(void *impl);
    void (*release)(void *impl);

    void (*reset)(void *impl, bool full_reset, unsigned *rng, bool *from_milestone);

    void (*warmup)(void *impl);

    void (*step)(void *impl, int action, const PkSnapshot *last);

    void (*snapshot)(void *impl, PkSnapshot *out);

    void (*screen)(void *impl, float *out);

    void (*blackout)(void *impl);
    void (*milestone_map)(void *impl, int map_n, int prev_map_n);
    void (*milestone_event)(void *impl, int event_idx);
    int (*milestone_pool_size)(void);
    bool (*frame_rgba)(void *impl, uint8_t *rgba );
    bool (*quicksave)(void *impl, const char *path);
} PkBackend;

const PkBackend *pk_backend_emulator(void);
const PkBackend *pk_backend_redcore(void);

int pk_event_count(void);
const char *pk_event_name(int idx);

#ifdef __cplusplus
}
#endif

#endif
