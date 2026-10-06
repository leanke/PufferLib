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

    // Cumulative whole-party blackouts the backend resolved inside a single step
    // (redcore heals and warps the player immediately, so the 0-HP party is never
    // visible afterwards). Stays 0 on the emulator, where the blackout plays out
    // over many frames and shows up as hp_fraction == 0.
    uint16_t blackouts;

    // Cumulative battles won, for backends that resolve a battle's last turn and its
    // end inside one step (redcore), where the enemy's 0 HP is never observable.
    // Stays 0 on the emulator, which uses the 0-HP-enemy check instead.
    uint16_t battles_won;

    // Cumulative battles the player ran from, for backends that resolve the escape and
    // the battle's end inside one step (redcore). Stays 0 on the emulator, which sets
    // `escaped` while the battle is still on screen instead.
    uint16_t battles_fled;
    uint8_t escaped;         // wEscapedFromBattle during an active battle, else 0

    // Backends must leave everything below zeroed when it does not apply (no battle,
    // empty party slots, empty bag slots): the emulator's RAM keeps stale values there.
    int8_t in_battle;        // 1 = wild, 2 = trainer, otherwise no active battle
    PkMon battle_mon;
    PkMon enemy_mon;

    uint8_t bag_count;
    struct { uint8_t item, count; } bag[20];

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
    bool npc_text_enabled;
    bool real_battle_ui_enabled;
    bool battle_text_enabled;
    bool npc_movement_enabled;
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
const PkBackend *pk_backend_native(void);

int pk_event_count(void);
const char *pk_event_name(int idx);

#ifdef __cplusplus
}
#endif

#endif
