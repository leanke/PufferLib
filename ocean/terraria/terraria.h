// Terraria-inspired RL environment for PufferLib.
// 2D tile-world with mining, crafting, combat. C99, no dynamic alloc during step.
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "inventory.h"
#include "entities.h"
#include "crafting.h"

#define WORLD_W 256
#define WORLD_H 128

typedef enum {
    TILE_AIR = 0,
    TILE_DIRT,
    TILE_GRASS,
    TILE_STONE,
    TILE_WOOD,
    TILE_LEAVES,
    TILE_SAND,
    TILE_COAL,
    TILE_IRON,
    TILE_GOLD,
    TILE_PLATFORM,
    TILE_CHEST,
    TILE_WORKBENCH,
    TILE_FURNACE,
    TILE_TORCH,
    TILE_DOOR_CLOSED,
    TILE_DOOR_OPEN,
    TILE_BED,
    TILE_COUNT   // = 18
} TileType;

// Byte layout: bits 0-4 = type (32 values), bit 5 = background wall, bits 6-7 reserved
typedef uint8_t Tile;
#define TILE_TYPE(t)         ((TileType)((t) & 0x1F))
#define TILE_HAS_WALL(t)     (((t) >> 5) & 1)
#define TILE_SET_TYPE(t, ty) ((t) = ((t) & 0xE0u) | ((uint8_t)(ty) & 0x1Fu))
#define TILE_SET_WALL(t, w)  ((t) = ((t) & 0xDFu) | (((uint8_t)(w) & 1u) << 5))

typedef struct {
    uint8_t hardness;   // ticks to break at base tool level
    uint8_t tool_min;   // min pick tier: 0=hand,1=wood,2=stone,3=iron,4=gold
    uint8_t drop_item;  // ItemType dropped on break
    uint8_t is_solid;
    uint8_t is_platform;
} TileProps;

static const TileProps TILE_PROPS[TILE_COUNT] = {
    //                  hard  tier  drop                     solid  plat
    /* AIR         */ { 0,    0,    ITEM_NONE,               0,     0 },
    /* DIRT        */ { 4,    0,    ITEM_DIRT,               1,     0 },
    /* GRASS       */ { 4,    0,    ITEM_DIRT,               1,     0 },
    /* STONE       */ { 12,   1,    ITEM_STONE,              1,     0 },
    /* WOOD        */ { 6,    0,    ITEM_WOOD,               1,     0 },
    /* LEAVES      */ { 1,    0,    ITEM_NONE,               0,     0 },
    /* SAND        */ { 3,    0,    ITEM_SAND,               1,     0 },
    /* COAL        */ { 14,   1,    ITEM_COAL,               1,     0 },
    /* IRON        */ { 20,   2,    ITEM_IRON_ORE,           1,     0 },
    /* GOLD        */ { 28,   3,    ITEM_GOLD_ORE,           1,     0 },
    /* PLATFORM    */ { 2,    0,    ITEM_PLATFORM,           0,     1 },
    /* CHEST       */ { 4,    0,    ITEM_CHEST_ITEM,         1,     0 },
    /* WORKBENCH   */ { 4,    0,    ITEM_WORKBENCH_ITEM,     1,     0 },
    /* FURNACE     */ { 4,    0,    ITEM_FURNACE_ITEM,       1,     0 },
    /* TORCH       */ { 1,    0,    ITEM_TORCH,              0,     0 },
    /* DOOR_CLOSED */ { 4,    0,    ITEM_DOOR_ITEM,          1,     0 },
    /* DOOR_OPEN   */ { 4,    0,    ITEM_DOOR_ITEM,          0,     0 },
    /* BED         */ { 4,    0,    ITEM_BED_ITEM,           1,     0 },
};

// Item → tile when placed (0 = not placeable)
static const uint8_t ITEM_TO_TILE[ITEM_COUNT] = {
    0,              // NONE
    TILE_DIRT,      // DIRT
    TILE_STONE,     // STONE
    TILE_WOOD,      // WOOD
    0,              // COAL
    0, 0, 0, 0,     // ores, bars
    TILE_SAND,      // SAND
    TILE_TORCH,     // TORCH
    TILE_PLATFORM,  // PLATFORM
    TILE_CHEST,     // CHEST_ITEM
    TILE_WORKBENCH, // WORKBENCH_ITEM
    TILE_FURNACE,   // FURNACE_ITEM
    TILE_DOOR_CLOSED,// DOOR_ITEM
    TILE_BED,       // BED_ITEM
    // tools / armor: 0 (not placeable) — remaining entries zero-initialized
};

#define DAY_LENGTH    1800   // ticks per day half
#define MAX_TICKS     50000  // episode truncation
#define SPAWN_CD_DAY  90
#define SPAWN_CD_NIGHT 30

// ─── Log (must be all floats, n last) ────────────────────────────────────────

typedef struct {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float tiles_mined;
    float items_crafted;
    float enemies_killed;
    float depth_reached;
    float n;   // MUST BE LAST
} Log;

typedef struct {
    // PufferLib required (order matters)
    Log          log;
    float*       observations;
    float*       actions;
    float*       rewards;
    float*       terminals;
    int          num_agents;
    unsigned int rng;

    uint8_t      tiles[WORLD_H][WORLD_W];

    Player       player;
    EnemySoA     enemies;
    Inventory    inventory;

    uint16_t     tick;
    uint16_t     day_tick;
    uint8_t      is_night;
    uint8_t      spawn_cd;
    uint8_t      first_craft_flags[NUM_RECIPES];

    // Episode metrics (reset each episode)
    float        ep_return;
    float        ep_length;
    float        ep_tiles_mined;
    float        ep_items_crafted;
    float        ep_enemies_killed;
    float        ep_max_depth;  // normalized 0-1
} Terraria;

// ─── Physics constants (defined here so worldgen.h and physics.h share them) ─

#define PLAYER_HW   0.38f
#define PLAYER_HH   0.88f
#define WALK_SPEED  0.18f
#define JUMP_SPEED (-2.2f)
#define GRAVITY     0.35f
#define MAX_FALL    8.0f

// ─── Helpers (defined before subsystem headers that call them) ────────────────

static inline int pick_tier(const Player* p) {
    switch (p->equip_pick) {
        case ITEM_PICK_WOOD:  return 1;
        case ITEM_PICK_STONE: return 2;
        case ITEM_PICK_IRON:  return 3;
        case ITEM_PICK_GOLD:  return 4;
        default:              return 0;
    }
}

static inline int weapon_damage(const Player* p) {
    switch (p->equip_weapon) {
        case ITEM_SWORD_WOOD:  return 8;
        case ITEM_SWORD_STONE: return 15;
        case ITEM_SWORD_IRON:  return 22;
        case ITEM_SWORD_GOLD:  return 35;
        default:               return 5;
    }
}

static inline int armor_defense(const Player* p) {
    int def = 0;
    if (p->equip_helmet == ITEM_HELM_IRON  || p->equip_helmet == ITEM_HELM_GOLD)  def += 3;
    if (p->equip_helmet == ITEM_HELM_GOLD)  def += 2;
    if (p->equip_chest  == ITEM_CHEST_IRON || p->equip_chest  == ITEM_CHEST_GOLD) def += 4;
    if (p->equip_chest  == ITEM_CHEST_GOLD) def += 3;
    if (p->equip_legs   == ITEM_LEGS_IRON  || p->equip_legs   == ITEM_LEGS_GOLD)  def += 2;
    if (p->equip_legs   == ITEM_LEGS_GOLD)  def += 2;
    return def;
}

// Equip an item if it belongs to an equipment slot
static inline void auto_equip(Terraria* env, uint8_t item_id) {
    Player* p = &env->player;
    switch (item_id) {
        case ITEM_PICK_WOOD: case ITEM_PICK_STONE: case ITEM_PICK_IRON: case ITEM_PICK_GOLD:
            p->equip_pick = item_id; break;
        case ITEM_AXE_WOOD: case ITEM_AXE_STONE: case ITEM_AXE_IRON:
            p->equip_axe = item_id; break;
        case ITEM_SWORD_WOOD: case ITEM_SWORD_STONE: case ITEM_SWORD_IRON: case ITEM_SWORD_GOLD:
            p->equip_weapon = item_id; break;
        case ITEM_HELM_IRON:  case ITEM_HELM_GOLD:
            p->equip_helmet = item_id; break;
        case ITEM_CHEST_IRON: case ITEM_CHEST_GOLD:
            p->equip_chest = item_id; break;
        case ITEM_LEGS_IRON:  case ITEM_LEGS_GOLD:
            p->equip_legs = item_id; break;
        default: break;
    }
}

// Check if a crafting station tile is within 4 tiles of the player
static inline int has_station(const Terraria* env, TileType station_tile) {
    int ptx = (int)env->player.px;
    int pty = (int)env->player.py;
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int tx = ptx + dx, ty = pty + dy;
            if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) continue;
            if (TILE_TYPE(env->tiles[ty][tx]) == station_tile) return 1;
        }
    }
    return 0;
}

// ─── Subsystem headers (depend on Terraria struct + helpers being defined) ────

#include "worldgen.h"
#include "physics.h"
#include "combat.h"
#include "observation.h"
#include "render.h"

static float do_craft(Terraria* env, int recipe_idx) {
    if (recipe_idx < 0 || recipe_idx >= NUM_RECIPES) return 0.0f;
    const Recipe* r = &RECIPES[recipe_idx];

    if (r->station == STATION_WORKBENCH && !has_station(env, TILE_WORKBENCH)) return 0.0f;
    if (r->station == STATION_FURNACE   && !has_station(env, TILE_FURNACE))   return 0.0f;

    for (int i = 0; i < r->n_in; i++) {
        if (r->in_item[i] == ITEM_NONE) continue;
        if (inv_count(&env->inventory, r->in_item[i]) < r->in_count[i]) return 0.0f;
    }

    for (int i = 0; i < r->n_in; i++) {
        if (r->in_item[i] == ITEM_NONE) continue;
        inv_remove(&env->inventory, r->in_item[i], r->in_count[i]);
    }

    inv_add(&env->inventory, r->out_item, r->out_count);
    auto_equip(env, r->out_item);
    env->ep_items_crafted += 1.0f;

    float reward = 1.0f; // base craft reward
    if (!env->first_craft_flags[recipe_idx]) {
        env->first_craft_flags[recipe_idx] = 1;
        reward = 5.0f; // bonus for first time
    }
    return reward;
}

static void c_reset(Terraria* env);

static void finalize_episode(Terraria* env) {
    env->log.episode_return  += env->ep_return;
    env->log.episode_length  += env->ep_length;
    env->log.tiles_mined     += env->ep_tiles_mined;
    env->log.items_crafted   += env->ep_items_crafted;
    env->log.enemies_killed  += env->ep_enemies_killed;
    env->log.depth_reached   += env->ep_max_depth;
    // perf: score normalized [0,1] based on progression
    float score = env->ep_tiles_mined * 0.1f
                + env->ep_items_crafted * 2.0f
                + env->ep_enemies_killed * 1.0f
                + env->ep_max_depth * 10.0f;
    env->log.score   += score;
    env->log.perf    += fminf(score / 100.0f, 1.0f);
    env->log.n       += 1.0f;
}

static void c_init(Terraria* env) __attribute__((unused));
static void c_init(Terraria* env) {
    env->num_agents = 1;
    c_reset(env);
}

static void c_reset(Terraria* env) {
    memset(env->tiles,            0, sizeof(env->tiles));
    memset(&env->player,          0, sizeof(Player));
    memset(&env->enemies,         0, sizeof(EnemySoA));
    memset(&env->inventory,       0, sizeof(Inventory));
    memset(env->first_craft_flags, 0, sizeof(env->first_craft_flags));

    env->tick    = 0;
    env->day_tick = 0;
    env->is_night = 0;
    env->spawn_cd = SPAWN_CD_DAY;

    env->ep_return       = 0.0f;
    env->ep_length       = 0.0f;
    env->ep_tiles_mined  = 0.0f;
    env->ep_items_crafted = 0.0f;
    env->ep_enemies_killed = 0.0f;
    env->ep_max_depth    = 0.0f;

    generate_world(env);
    encode_observation(env);
}

static void c_step(Terraria* env) {
    int move_act  = (int)env->actions[0];  // 0-5
    int tool_act  = (int)env->actions[1];  // 0-2
    int slot_act  = (int)env->actions[2];  // 0-31
    int craft_act = (int)env->actions[3];  // 0-20
    int focus_act = (int)env->actions[4];  // 0-9 (numpad layout)

    if (move_act  < 0 || move_act  > 5)  move_act  = 0;
    if (focus_act < 0 || focus_act > 9)  focus_act = 0;
    if (tool_act  < 0 || tool_act  > 2)  tool_act  = 0;
    if (slot_act  < 0 || slot_act  > 31) slot_act  = 0;
    if (craft_act < 0 || craft_act > 20) craft_act = 0;

    float reward = 0.0f;
    Player* p = &env->player;
    p->selected_slot = (uint8_t)slot_act;
    if (craft_act > 0) {
        reward += do_craft(env, craft_act - 1);
    }

    // Movement — set velocity intent
    switch (move_act) {
        case 1: p->pvx = -WALK_SPEED; p->facing = -1; break;
        case 2: p->pvx =  WALK_SPEED; p->facing =  1; break;
        case 3: // jump
            if (p->on_ground) { p->pvy = JUMP_SPEED; p->on_ground = 0; }
            break;
        case 4: // jump left
            p->pvx = -WALK_SPEED; p->facing = -1;
            if (p->on_ground) { p->pvy = JUMP_SPEED; p->on_ground = 0; }
            break;
        case 5: // jump right
            p->pvx =  WALK_SPEED; p->facing =  1;
            if (p->on_ground) { p->pvy = JUMP_SPEED; p->on_ground = 0; }
            break;
        default: // idle — let friction stop us
            break;
    }

    physics_step(env);
    if (tool_act == 1) {
        reward += do_mine_or_attack(env, focus_act);
    } else if (tool_act == 2) {
        do_place_block(env);
    } else {
        // No tool use: reset mining progress
        p->mine_tx = -1;
        p->mine_ty = -1;
        p->mine_progress = 0;
    }
    reward += update_enemies(env);
    if (p->attack_cd > 0) p->attack_cd--;
    if (p->iframes   > 0) p->iframes--;

    env->day_tick++;
    if (env->day_tick >= (uint16_t)(DAY_LENGTH * 2)) env->day_tick = 0;
    env->is_night = (env->day_tick >= (uint16_t)DAY_LENGTH) ? 1 : 0;

    if (env->spawn_cd > 0) {
        env->spawn_cd--;
    } else {
        try_spawn_enemy(env);
        env->spawn_cd = env->is_night ? (uint8_t)SPAWN_CD_NIGHT : (uint8_t)SPAWN_CD_DAY;
    }

    float surface_y  = (float)(WORLD_H / 4);
    float depth_norm = (p->py - surface_y) / (float)(WORLD_H - surface_y);
    if (depth_norm < 0.0f) depth_norm = 0.0f;
    if (depth_norm > env->ep_max_depth) env->ep_max_depth = depth_norm;
    env->ep_return += reward;
    env->ep_length += 1.0f;

    if (p->php <= 0) {
        finalize_episode(env);
        env->rewards[0]   = reward;
        env->terminals[0] = 1.0f;
        c_reset(env);
        return;
    }
    env->tick++;
    if (env->tick >= MAX_TICKS) {
        finalize_episode(env);
        env->rewards[0]   = reward;
        env->terminals[0] = 1.0f;
        c_reset(env);
        return;
    }

    env->rewards[0]   = reward;
    env->terminals[0] = 0.0f;
    encode_observation(env);
}

static void c_close(Terraria* env) {
    (void)env;
}
