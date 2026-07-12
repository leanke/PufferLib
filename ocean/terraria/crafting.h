#pragma once

#include <stdint.h>
#include "inventory.h"

// Station codes for recipes
#define STATION_ANY       0
#define STATION_WORKBENCH 1
#define STATION_FURNACE   2
#define STATION_MERCHANT  3

#define NUM_RECIPES 43

typedef struct {
    uint8_t station;
    uint8_t n_in;
    uint8_t in_item[4];
    uint8_t in_count[4];
    uint8_t out_item;
    uint8_t out_count;
    uint8_t requires_hardmode; // trailing: existing initializers default to 0
} Recipe;

// Progression: Hand → Wood → Stone → Iron → Gold
static const Recipe RECIPES[NUM_RECIPES] = {
    // 0:  Workbench        (10 wood, anywhere)
    {STATION_ANY, 1, {ITEM_WOOD, 0, 0, 0}, {10, 0, 0, 0}, ITEM_WORKBENCH_ITEM, 1},
    // 1:  Wood Pickaxe     (8 wood, workbench)
    {STATION_WORKBENCH, 1, {ITEM_WOOD, 0, 0, 0}, {8, 0, 0, 0}, ITEM_PICK_WOOD, 1},
    // 2:  Wood Axe         (6 wood, workbench)
    {STATION_WORKBENCH, 1, {ITEM_WOOD,  0, 0, 0}, {6, 0, 0, 0}, ITEM_AXE_WOOD, 1},
    // 3:  Wood Sword       (7 wood, workbench)
    {STATION_WORKBENCH, 1, {ITEM_WOOD,  0, 0, 0}, {7, 0, 0, 0}, ITEM_SWORD_WOOD, 1},
    // 4:  Torch            (3 coal + 3 wood, anywhere)
    {STATION_ANY, 2, {ITEM_COAL,  ITEM_WOOD,      0,           0          }, {3,  3,  0,  0}, ITEM_TORCH,          8},
    // 5:  Platform         (2 wood = 4 platforms, anywhere)
    {STATION_ANY,       1, {ITEM_WOOD,  0,              0,           0          }, {2,  0,  0,  0}, ITEM_PLATFORM,       4},
    // 6:  Stone Pickaxe    (12 stone, workbench)
    {STATION_WORKBENCH, 1, {ITEM_STONE, 0,              0,           0          }, {12, 0,  0,  0}, ITEM_PICK_STONE,     1},
    // 7:  Stone Axe        (10 stone, workbench)
    {STATION_WORKBENCH, 1, {ITEM_STONE, 0,              0,           0          }, {10, 0,  0,  0}, ITEM_AXE_STONE,      1},
    // 8:  Stone Sword      (11 stone, workbench)
    {STATION_WORKBENCH, 1, {ITEM_STONE, 0,              0,           0          }, {11, 0,  0,  0}, ITEM_SWORD_STONE,    1},
    // 9:  Furnace          (20 stone, workbench)
    {STATION_WORKBENCH, 1, {ITEM_STONE, 0,              0,           0          }, {20, 0,  0,  0}, ITEM_FURNACE_ITEM,   1},
    // 10: Iron Bar         (4 iron ore, furnace)
    {STATION_FURNACE,   1, {ITEM_IRON_ORE, 0,           0,           0          }, {4,  0,  0,  0}, ITEM_IRON_BAR,       1},
    // 11: Gold Bar         (4 gold ore, furnace)
    {STATION_FURNACE,   1, {ITEM_GOLD_ORE, 0,           0,           0          }, {4,  0,  0,  0}, ITEM_GOLD_BAR,       1},
    // 12: Iron Pickaxe     (12 iron bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_IRON_BAR, 0,           0,           0          }, {12, 0,  0,  0}, ITEM_PICK_IRON,      1},
    // 13: Iron Axe         (10 iron bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_IRON_BAR, 0,           0,           0          }, {10, 0,  0,  0}, ITEM_AXE_IRON,       1},
    // 14: Iron Sword       (8 iron bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_IRON_BAR, 0,           0,           0          }, {8,  0,  0,  0}, ITEM_SWORD_IRON,     1},
    // 15: Iron Helmet      (10 iron bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_IRON_BAR, 0,           0,           0          }, {10, 0,  0,  0}, ITEM_HELM_IRON,      1},
    // 16: Gold Pickaxe     (12 gold bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_GOLD_BAR, 0,           0,           0          }, {12, 0,  0,  0}, ITEM_PICK_GOLD,      1},
    // 17: Gold Sword       (8 gold bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_GOLD_BAR, 0,           0,           0          }, {8,  0,  0,  0}, ITEM_SWORD_GOLD,     1},
    // 18: Door             (6 wood, workbench)
    {STATION_WORKBENCH, 1, {ITEM_WOOD,  0,              0,           0          }, {6,  0,  0,  0}, ITEM_DOOR_ITEM,      1},
    // 19: Bed              (15 wood + 5 coal, workbench)
    {STATION_WORKBENCH, 2, {ITEM_WOOD,  ITEM_COAL,      0,           0          }, {15, 5,  0,  0}, ITEM_BED_ITEM,       1},
    // 20: Gold Helmet      (10 gold bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_GOLD_BAR, 0,           0,           0          }, {10, 0,  0,  0}, ITEM_HELM_GOLD,      1},
    // 21: Iron Chestplate  (14 iron bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_IRON_BAR, 0,           0,           0          }, {14, 0,  0,  0}, ITEM_CHEST_IRON,     1},
    // 22: Gold Chestplate  (14 gold bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_GOLD_BAR, 0,           0,           0          }, {14, 0,  0,  0}, ITEM_CHEST_GOLD,     1},
    // 23: Iron Greaves     (12 iron bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_IRON_BAR, 0,           0,           0          }, {12, 0,  0,  0}, ITEM_LEGS_IRON,      1},
    // 24: Gold Greaves     (12 gold bar, workbench)
    {STATION_WORKBENCH, 1, {ITEM_GOLD_BAR, 0,           0,           0          }, {12, 0,  0,  0}, ITEM_LEGS_GOLD,      1},
    // 25: Boss Summon      (15 gold bar, furnace) — reachable only after a real
    //     gold-tier grind, mirrors the real game's material-gated boss summons.
    {STATION_FURNACE,   1, {ITEM_GOLD_BAR, 0,              0,           0          }, {15, 0,  0,  0}, ITEM_BOSS_SUMMON,    1},
    // 26-31: Cobalt tier — Hardmode-gated, crafted from Gold Bar + Boss Soul
    // (a boss-kill-only drop) instead of a newly mined ore, so unlocking this
    // tier never requires mutating the already-generated world.
    {STATION_WORKBENCH, 2, {ITEM_GOLD_BAR, ITEM_BOSS_SOUL, 0,         0          }, {12, 2,  0,  0}, ITEM_PICK_COBALT,    1, 1},
    {STATION_WORKBENCH, 2, {ITEM_GOLD_BAR, ITEM_BOSS_SOUL, 0,         0          }, {10, 2,  0,  0}, ITEM_AXE_COBALT,     1, 1},
    {STATION_WORKBENCH, 2, {ITEM_GOLD_BAR, ITEM_BOSS_SOUL, 0,         0          }, {10, 2,  0,  0}, ITEM_SWORD_COBALT,   1, 1},
    {STATION_WORKBENCH, 2, {ITEM_GOLD_BAR, ITEM_BOSS_SOUL, 0,         0          }, {12, 2,  0,  0}, ITEM_HELM_COBALT,    1, 1},
    {STATION_WORKBENCH, 2, {ITEM_GOLD_BAR, ITEM_BOSS_SOUL, 0,         0          }, {16, 3,  0,  0}, ITEM_CHEST_COBALT,   1, 1},
    {STATION_WORKBENCH, 2, {ITEM_GOLD_BAR, ITEM_BOSS_SOUL, 0,         0          }, {14, 3,  0,  0}, ITEM_LEGS_COBALT,    1, 1},
    // 32: Silver Bar        (4 silver ore, furnace)
    {STATION_FURNACE,   1, {ITEM_SILVER_ORE, 0,           0,           0          }, {4,  0,  0,  0}, ITEM_SILVER_BAR,     1},
    // 33-38: Silver tier (pre-Hardmode, between Iron and Gold)
    {STATION_WORKBENCH, 1, {ITEM_SILVER_BAR, 0,           0,           0          }, {12, 0,  0,  0}, ITEM_PICK_SILVER,    1},
    {STATION_WORKBENCH, 1, {ITEM_SILVER_BAR, 0,           0,           0          }, {10, 0,  0,  0}, ITEM_AXE_SILVER,     1},
    {STATION_WORKBENCH, 1, {ITEM_SILVER_BAR, 0,           0,           0          }, {9,  0,  0,  0}, ITEM_SWORD_SILVER,   1},
    {STATION_WORKBENCH, 1, {ITEM_SILVER_BAR, 0,           0,           0          }, {11, 0,  0,  0}, ITEM_HELM_SILVER,    1},
    {STATION_WORKBENCH, 1, {ITEM_SILVER_BAR, 0,           0,           0          }, {15, 0,  0,  0}, ITEM_CHEST_SILVER,   1},
    {STATION_WORKBENCH, 1, {ITEM_SILVER_BAR, 0,           0,           0          }, {13, 0,  0,  0}, ITEM_LEGS_SILVER,    1},
    // 39-40: Merchant shop — buying goes through the same craft_act/do_craft
    // path as ordinary crafting, gated by near_merchant() instead of a tile.
    {STATION_MERCHANT, 1, {ITEM_COIN, 0,                  0,           0          }, {5,  0,  0,  0}, ITEM_TORCH,          8},
    {STATION_MERCHANT, 1, {ITEM_COIN, 0,                  0,           0          }, {10, 0,  0,  0}, ITEM_HEALTH_POTION,  1},
    // 41-42: Accessories — a single equip slot, flat stat boosts, no new action.
    {STATION_WORKBENCH, 2, {ITEM_SILVER_BAR, ITEM_WOOD,   0,           0          }, {10, 5,  0,  0}, ITEM_ACC_BOOTS,      1},
    {STATION_WORKBENCH, 1, {ITEM_GOLD_BAR, 0,             0,           0          }, {8,  0,  0,  0}, ITEM_ACC_RING,       1},
};
