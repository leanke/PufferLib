#pragma once

#include <stdint.h>
#include "inventory.h"

typedef struct {
    float    px, py;       // center position (float tile coords)
    float    pvx, pvy;     // velocity
    int16_t  php, pmax_hp;
    int8_t   facing;       // -1 = left, 1 = right
    uint8_t  on_ground;
    uint8_t  attack_cd;    // melee cooldown ticks
    uint8_t  iframes;      // invincibility frames after taking damage

    // Mining state
    int16_t  mine_tx, mine_ty; // current mining target (-1 = none)
    uint8_t  mine_progress;    // accumulated mining damage

    // Equipment slots (ItemType IDs, 0 = none)
    uint8_t  equip_pick;
    uint8_t  equip_axe;
    uint8_t  equip_weapon;
    uint8_t  equip_helmet;
    uint8_t  equip_chest;
    uint8_t  equip_legs;

    uint8_t  selected_slot;
} Player;

typedef enum {
    ENEMY_SLIME = 0,
    ENEMY_ZOMBIE,
    ENEMY_BAT,
    ENEMY_SKELETON,
    ENEMY_COUNT
} EnemyType;

typedef enum {
    AI_WANDER = 0,
    AI_CHASE,
    AI_ATTACK
} AIState;

typedef struct {
    int16_t  hp;
    uint8_t  dmg;          // contact damage per hit
    uint8_t  speed_x16;    // horizontal speed × 16 (fixed-point)
    uint8_t  max_hp_lo;    // low byte of max_hp
    uint8_t  max_hp_hi;    // high byte of max_hp
    uint8_t  is_flying;    // 1 = ignores gravity (bat)
    uint8_t  patrol_range; // wander radius in tiles
    uint8_t  aggro_range;  // chase trigger distance in tiles
    uint8_t  attack_range; // melee range in tiles
    uint8_t  attack_cd;    // attack cooldown ticks
    uint8_t  xp;           // experience on kill (used for reward)
} EnemyProps;

static const EnemyProps ENEMY_PROPS[ENEMY_COUNT] = {
    // hp  dmg spd  mhilo mhihi fly  pat agr  atk  acd  xp
    { 25,  5,  25,  25,  0,   0,   8,  12,  2,   45,  1  }, // SLIME
    { 45, 10,  38,  45,  0,   0,   6,  16,  2,   40,  2  }, // ZOMBIE
    { 20,  8,  64,  20,  0,   1,  12,  20,  2,   30,  2  }, // BAT
    { 60, 15,  32,  60,  0,   0,   5,  14,  2,   50,  4  }, // SKELETON
};

#define MAX_ENEMIES 32

typedef struct {
    // Hot arrays (iterated every step)
    float   ex[MAX_ENEMIES];
    float   ey[MAX_ENEMIES];
    float   evx[MAX_ENEMIES];
    float   evy[MAX_ENEMIES];
    int16_t ehp[MAX_ENEMIES];
    uint8_t etype[MAX_ENEMIES];
    uint8_t estate[MAX_ENEMIES];  // AIState
    uint8_t eactive[MAX_ENEMIES]; // 1 = slot in use

    // Cold arrays (touched on hit/spawn)
    uint8_t estun[MAX_ENEMIES];
    uint8_t eattack_cd[MAX_ENEMIES];
    uint8_t ewander_cd[MAX_ENEMIES]; // ticks until direction change in wander
    int8_t  ewander_dir[MAX_ENEMIES]; // -1 or 1 for wander direction
} EnemySoA;

static inline int enemies_alloc(EnemySoA* e) {
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (!e->eactive[i]) return i;
    return -1;
}

static inline int enemies_count(const EnemySoA* e) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        n += e->eactive[i];
    return n;
}
