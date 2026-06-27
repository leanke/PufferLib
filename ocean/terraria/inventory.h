#pragma once

#include <stdint.h>

#define INV_SLOTS 32

typedef enum {
    ITEM_NONE = 0,
    // Raw materials
    ITEM_DIRT, ITEM_STONE, ITEM_WOOD, ITEM_COAL,
    ITEM_IRON_ORE, ITEM_GOLD_ORE, ITEM_IRON_BAR, ITEM_GOLD_BAR,
    ITEM_SAND,
    // Placeable items
    ITEM_TORCH, ITEM_PLATFORM,
    ITEM_CHEST_ITEM, ITEM_WORKBENCH_ITEM, ITEM_FURNACE_ITEM,
    ITEM_DOOR_ITEM, ITEM_BED_ITEM,
    // Tools: pickaxes
    ITEM_PICK_WOOD, ITEM_PICK_STONE, ITEM_PICK_IRON, ITEM_PICK_GOLD,
    // Tools: axes
    ITEM_AXE_WOOD, ITEM_AXE_STONE, ITEM_AXE_IRON,
    // Weapons
    ITEM_SWORD_WOOD, ITEM_SWORD_STONE, ITEM_SWORD_IRON, ITEM_SWORD_GOLD,
    // Armor
    ITEM_HELM_IRON,  ITEM_HELM_GOLD,
    ITEM_CHEST_IRON, ITEM_CHEST_GOLD,
    ITEM_LEGS_IRON,  ITEM_LEGS_GOLD,
    ITEM_COUNT
} ItemType;

typedef struct {
    uint8_t item_id;
    uint8_t count;
} InvSlot;

typedef struct {
    InvSlot slots[INV_SLOTS];
} Inventory;

static inline int inv_add(Inventory* inv, uint8_t item, uint8_t n) {
    for (int i = 0; i < INV_SLOTS && n > 0; i++) {
        if (inv->slots[i].item_id == item && inv->slots[i].count < 99) {
            int space = 99 - inv->slots[i].count;
            int add = (n < space) ? n : space;
            inv->slots[i].count += (uint8_t)add;
            n -= (uint8_t)add;
        }
    }
    for (int i = 0; i < INV_SLOTS && n > 0; i++) {
        if (inv->slots[i].item_id == ITEM_NONE) {
            inv->slots[i].item_id = item;
            inv->slots[i].count = n;
            n = 0;
        }
    }
    return (n == 0) ? 1 : 0;
}

static inline int inv_remove(Inventory* inv, uint8_t item, uint8_t n) {
    int total = 0;
    for (int i = 0; i < INV_SLOTS; i++)
        if (inv->slots[i].item_id == item) total += inv->slots[i].count;
    if (total < n) return 0;
    for (int i = 0; i < INV_SLOTS && n > 0; i++) {
        if (inv->slots[i].item_id == item) {
            int take = (inv->slots[i].count < n) ? inv->slots[i].count : n;
            inv->slots[i].count -= (uint8_t)take;
            n -= (uint8_t)take;
            if (inv->slots[i].count == 0) inv->slots[i].item_id = ITEM_NONE;
        }
    }
    return 1;
}

static inline int inv_count(const Inventory* inv, uint8_t item) {
    int total = 0;
    for (int i = 0; i < INV_SLOTS; i++)
        if (inv->slots[i].item_id == item) total += inv->slots[i].count;
    return total;
}
