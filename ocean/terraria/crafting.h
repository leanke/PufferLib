#pragma once

#include <stdint.h>
#include "inventory.h"

// Station codes for recipes
#define STATION_ANY       0
#define STATION_WORKBENCH 1
#define STATION_FURNACE   2

#define NUM_RECIPES 20

typedef struct {
    uint8_t station;
    uint8_t n_in;
    uint8_t in_item[4];
    uint8_t in_count[4];
    uint8_t out_item;
    uint8_t out_count;
} Recipe;

// Progression: Hand → Wood → Stone → Iron → Gold
static const Recipe RECIPES[NUM_RECIPES] = {
    // 0:  Workbench        (10 wood, anywhere)
    {STATION_ANY,       1, {ITEM_WOOD,  0,              0,           0          }, {10, 0,  0,  0}, ITEM_WORKBENCH_ITEM, 1},
    // 1:  Wood Pickaxe     (8 wood, workbench)
    {STATION_WORKBENCH, 1, {ITEM_WOOD,  0,              0,           0          }, {8,  0,  0,  0}, ITEM_PICK_WOOD,      1},
    // 2:  Wood Axe         (6 wood, workbench)
    {STATION_WORKBENCH, 1, {ITEM_WOOD,  0,              0,           0          }, {6,  0,  0,  0}, ITEM_AXE_WOOD,       1},
    // 3:  Wood Sword       (7 wood, workbench)
    {STATION_WORKBENCH, 1, {ITEM_WOOD,  0,              0,           0          }, {7,  0,  0,  0}, ITEM_SWORD_WOOD,     1},
    // 4:  Torch            (3 coal + 3 wood, anywhere)
    {STATION_ANY,       2, {ITEM_COAL,  ITEM_WOOD,      0,           0          }, {3,  3,  0,  0}, ITEM_TORCH,          8},
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
};
