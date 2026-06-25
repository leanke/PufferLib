#pragma once
#include "pufferenv.h"
#include "sim/sim.h"

// Observation layout: 64x64 tile map (4 channels HWC) + 64-byte scalar block
#define CRPG_TILE_H        VIEWPORT_H            // 64
#define CRPG_TILE_W        VIEWPORT_W            // 64
#define CRPG_TILE_CHANNELS 4                     // type, passable, resource, feature
#define CRPG_TILE_OBS      (CRPG_TILE_H * CRPG_TILE_W * CRPG_TILE_CHANNELS)  // 16384
#define CRPG_SCALAR_OBS    64
#define CRPG_OBS_SIZE      (CRPG_TILE_OBS + CRPG_SCALAR_OBS)                  // 16448

typedef struct {
    float episode_return;
    float episode_length;
    float deaths;   // 1.0 if sim.done (player death), 0.0 if max_steps reached
    float n;        // episode count — must be last
} CrpgLog;

typedef struct {
    uint8_t*  observations;  // CRPG_OBS_SIZE bytes, PUFFERENV_DTYPE_UINT8
    float*    actions;       // [1] discrete action index as float
    float*    rewards;       // [1]
    float*    terminals;     // [1]
    CrpgLog   log;
    int       num_agents;    // always 1 (V1: single player)
    SimState  sim;
    uint64_t  seed;
    int       tick;
    int       max_steps;
    float     episode_return;
} CrpgEnv;

static inline uint8_t clamp_u8(uint32_t v) {
    return (uint8_t)(v > 255 ? 255 : v);
}

static inline void encode_obs(CrpgEnv* env) {
    uint8_t* obs = env->observations;
    if (!obs) return;

    // ---- Tile map [CRPG_TILE_OBS bytes]: HWC [h][w][c] ----
    for (int vy = 0; vy < CRPG_TILE_H; vy++) {
        for (int vx = 0; vx < CRPG_TILE_W; vx++) {
            const Tile* t = &env->sim.world.tiles[vy][vx];
            int base = (vy * CRPG_TILE_W + vx) * CRPG_TILE_CHANNELS;
            obs[base + 0] = t->type;
            obs[base + 1] = t->passable ? 255 : 0;
            obs[base + 2] = t->resource;
            obs[base + 3] = t->feature;
        }
    }

    // ---- Scalar block [64 bytes]: player 0 state ----
    uint8_t* sf = obs + CRPG_TILE_OBS;
    const Player*      p  = &env->sim.group.players[0];
    const EntityStore* es = &env->sim.entities;

    // Skill levels [0..9]: 1-99
    for (int i = 0; i < SKILL_COUNT; i++)
        sf[i] = p->skills.level[i];

    // Skill XP [10..19]: normalized (xp_tenths / 100000, max ~2000 → clamp 255)
    for (int i = 0; i < SKILL_COUNT; i++)
        sf[10 + i] = clamp_u8(p->skills.xp[i] / 100000);

    // Inventory [20..32]
    sf[20] = clamp_u8(p->inv.runes[RUNE_AIR]);
    sf[21] = clamp_u8(p->inv.runes[RUNE_FIRE]);
    sf[22] = clamp_u8(p->inv.runes[RUNE_WATER]);
    sf[23] = clamp_u8(p->inv.runes[RUNE_EARTH]);
    sf[24] = clamp_u8(p->inv.runes[RUNE_SOUL]);
    sf[25] = clamp_u8(p->inv.cooked_meat);
    sf[26] = clamp_u8(p->inv.cooked_fish);
    sf[27] = clamp_u8(p->inv.gold);
    sf[28] = clamp_u8(p->inv.ore[ORE_COPPER]);
    sf[29] = clamp_u8(p->inv.ore[ORE_IRON]);
    sf[30] = clamp_u8(p->inv.ore[ORE_COAL]);
    sf[31] = clamp_u8(p->inv.logs[LOG_OAK]);
    sf[32] = clamp_u8(p->inv.logs[LOG_PINE]);

    // Equipment slots [33..43]: 1 if occupied, 0 if empty (11 slots)
    for (int i = 0; i < SLOT_COUNT; i++)
        sf[33 + i] = (p->equip.item_id[i] != 0) ? 1 : 0;

    // HP ratio [44]
    EntityID eid = p->eid;
    int32_t hp     = (eid > 0) ? es->hp[eid]     : 0;
    int32_t hp_max = (eid > 0) ? es->hp_max[eid] : 1;
    sf[44] = (hp_max > 0) ? (uint8_t)((int32_t)255 * hp / hp_max) : 0;

    // Prayer [45]: prayer_points / PRAYER_MAX (990)
    sf[45] = (uint8_t)((int32_t)255 * p->prayer_points / PRAYER_MAX);

    // Run energy [46]: run_energy / RUN_ENERGY_MAX (10000)
    sf[46] = (uint8_t)((int32_t)255 * p->run_energy / RUN_ENERGY_MAX);

    // Position relative to spawn, clamped [-64, 63] → [0, 127]
    int64_t rx = p->world_x - env->sim.world.spawn_wx;
    int64_t ry = p->world_y - env->sim.world.spawn_wy;
    rx = rx < -64 ? -64 : (rx > 63 ? 63 : rx);
    ry = ry < -64 ? -64 : (ry > 63 ? 63 : ry);
    sf[47] = (uint8_t)(rx + 64);
    sf[48] = (uint8_t)(ry + 64);

    // Padding
    for (int i = 49; i < CRPG_SCALAR_OBS; i++) sf[i] = 0;
}
