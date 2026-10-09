#ifndef POKERED_PKSNAPSHOT_H
#define POKERED_PKSNAPSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../data/ram_map.h"
#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PK_STATE_WRAM_BASE 0xC000
#define PK_STATE_WRAM_SIZE 8192
#define PK_STATE_HRAM_BASE 0xFF80
#define PK_STATE_HRAM_SIZE 127
#define PK_STATE_EXT ".pkstate"

typedef struct PkState {
    uint8_t work_ram[PK_STATE_WRAM_SIZE];
    uint8_t high_ram[PK_STATE_HRAM_SIZE];
    uint32_t shade_rgb[4];
    uint64_t source_hash;
} PkState;

static inline uint8_t pk_state_byte(const PkState *s, uint16_t addr) {
    if (addr >= PK_STATE_WRAM_BASE && addr < PK_STATE_WRAM_BASE + PK_STATE_WRAM_SIZE)
        return s->work_ram[addr - PK_STATE_WRAM_BASE];
    if (addr >= PK_STATE_HRAM_BASE && addr < PK_STATE_HRAM_BASE + PK_STATE_HRAM_SIZE)
        return s->high_ram[addr - PK_STATE_HRAM_BASE];
    return 0;
}

void pk_state_pick_shades(const uint32_t *video, int pitch, int width, int height, uint32_t out[4]);

uint64_t pk_state_file_hash(const char *path);

bool pk_state_write(const char *path, const PkState *s);
bool pk_state_read(const char *path, PkState *out);

bool pk_state_from_gambatte(const char *rom_path, const char *state_path, PkState *out);

bool pk_state_resolve(const char *state_path, const char *rom_path, bool write_cache, PkState *out, char *err,
                      size_t err_size);

#ifdef __cplusplus
}
#endif

typedef struct PkMemReader {
    void *ctx;
    uint8_t (*u8)(void *ctx, uint16_t addr);
    uint16_t (*u16)(void *ctx, uint16_t addr);
    const uint8_t *(*span)(void *ctx, uint16_t addr, unsigned len);
} PkMemReader;

#define PK_POKEDEX_BYTES ((PKRED_POKEDEX_NUM_POKEMON + 7) / 8)

static inline __attribute__((always_inline)) int pk_snap_popcount(const PkMemReader *m, uint16_t addr, int n) {
    const uint8_t *p = m->span(m->ctx, addr, (unsigned)n);
    int count = 0;
    for (int i = 0; i < n; i++)
        count += __builtin_popcount(p ? p[i] : m->u8(m->ctx, (uint16_t)(addr + i)));
    return count;
}

static inline __attribute__((always_inline)) void pk_snap_battler(const PkMemReader *m, PkMon *out, uint16_t species,
                                                                  uint16_t hp, uint16_t max_hp, uint16_t level) {
    out->species = m->u8(m->ctx, species);
    out->hp = m->u16(m->ctx, hp);
    out->max_hp = m->u16(m->ctx, max_hp);
    out->level = m->u8(m->ctx, level);
}

static inline __attribute__((always_inline)) void pk_snapshot_extract(const PkMemReader *m, PkSnapshot *s) {
    void *c = m->ctx;
    memset(s, 0, sizeof(*s));

    s->x = m->u8(c, PKRED_ADDR_X_COORD);
    s->y = m->u8(c, PKRED_ADDR_Y_COORD);
    s->map_n = m->u8(c, PKRED_ADDR_CUR_MAP);
    s->facing = m->u8(c, PKRED_ADDR_PLAYER_SPRITE_FACING_DIRECTION) / 4;
    s->badges = m->u8(c, PKRED_ADDR_OBTAINED_BADGES);

    uint8_t party = m->u8(c, PKRED_ADDR_PARTY_COUNT);
    s->party_count = party > 6 ? 6 : party;
    for (int i = 0; i < s->party_count; i++) {
        uint16_t base = (uint16_t)PKRED_ADDR_PARTY_MON(i);
        PkMon *mon = &s->party[i];
        mon->species = m->u8(c, (uint16_t)(base + offsetof(PkredPartyMon, species)));
        mon->level = m->u8(c, (uint16_t)(base + offsetof(PkredPartyMon, level)));
        mon->hp = m->u16(c, (uint16_t)(base + offsetof(PkredPartyMon, hp_hi)));
        mon->max_hp = m->u16(c, (uint16_t)(base + offsetof(PkredPartyMon, max_hp_hi)));
        for (int k = 0; k < 4; k++) {
            mon->moves[k] = m->u8(c, (uint16_t)(base + offsetof(PkredPartyMon, moves) + k));
            mon->pp[k] = m->u8(c, (uint16_t)(base + offsetof(PkredPartyMon, pp) + k));
        }
    }
    s->box_count = m->u8(c, PKRED_ADDR_BOX_COUNT);
    bool knows_cut = false;
    for (int i = 0; i < s->party_count; i++)
        for (int k = 0; k < 4; k++)
            knows_cut |= s->party[i].moves[k] == PKRED_MOVE_CUT;
    s->pokedex_owned_count = (uint8_t)pk_snap_popcount(m, PKRED_ADDR_POKEDEX_OWNED, PK_POKEDEX_BYTES);
    s->pokedex_seen_count = (uint8_t)pk_snap_popcount(m, PKRED_ADDR_POKEDEX_SEEN, PK_POKEDEX_BYTES);

    s->hp_fraction = 1.0f;
    if (s->party_count > 0) {
        uint32_t hp = 0, max_hp = 0;
        for (int i = 0; i < s->party_count; i++) {
            hp += s->party[i].hp;
            max_hp += s->party[i].max_hp;
        }
        s->hp_fraction = max_hp > 0 ? (float)hp / (float)max_hp : 1.0f;
    }

    s->in_battle = (int8_t)m->u8(c, PKRED_ADDR_IS_IN_BATTLE);
    if (s->in_battle == 1 || s->in_battle == 2) {
        pk_snap_battler(m, &s->battle_mon, PKRED_ADDR_BATTLE_MON_SPECIES, PKRED_ADDR_BATTLE_MON_HP,
                        PKRED_ADDR_BATTLE_MON_MAX_HP, PKRED_ADDR_BATTLE_MON_LEVEL);
        s->battle_result = m->u8(c, PKRED_ADDR_BATTLE_RESULT);
        pk_snap_battler(m, &s->enemy_mon, PKRED_ADDR_ENEMY_MON_SPECIES, PKRED_ADDR_ENEMY_MON_HP,
                        PKRED_ADDR_ENEMY_MON_MAX_HP, PKRED_ADDR_ENEMY_MON_LEVEL);
    }

    uint8_t bag = m->u8(c, PKRED_ADDR_NUM_BAG_ITEMS);
    s->bag_count = bag > PKRED_BAG_ITEM_CAPACITY ? PKRED_BAG_ITEM_CAPACITY : bag;
    for (int i = 0; i < s->bag_count; i++) {
        s->bag[i].item = m->u8(c, (uint16_t)(PKRED_ADDR_BAG_ITEMS + 2 * i));
        s->bag[i].count = m->u8(c, (uint16_t)(PKRED_ADDR_BAG_ITEMS + 2 * i + 1));
    }

    s->surfing = m->u8(c, PKRED_ADDR_WALK_BIKE_SURF) == PKRED_SURF_STATE;
    s->strength_active = (m->u8(c, PKRED_ADDR_STATUS_FLAGS1) >> PKRED_STATUS_FLAGS1_STRENGTH_BIT) & 1;
    s->used_fly = (m->u8(c, PKRED_ADDR_STATUS_FLAGS7) >> PKRED_STATUS_FLAGS7_USED_FLY_BIT) & 1;
    s->dark_cave = m->u8(c, PKRED_ADDR_MAP_PAL_OFFSET) == PKRED_PAL_DARK_CAVE;
    if (knows_cut) {
        const uint8_t *blocks = m->span(c, PKRED_ADDR_OVERWORLD_MAP, PKRED_OVERWORLD_MAP_SIZE);
        if (blocks) {
            s->map_block_hash = pkred_hash_bytes(blocks, PKRED_OVERWORLD_MAP_SIZE);
        } else {
            uint8_t buf[PKRED_OVERWORLD_MAP_SIZE];
            for (int i = 0; i < PKRED_OVERWORLD_MAP_SIZE; i++)
                buf[i] = m->u8(c, (uint16_t)(PKRED_ADDR_OVERWORLD_MAP + i));
            s->map_block_hash = pkred_hash_bytes(buf, PKRED_OVERWORLD_MAP_SIZE);
        }
    }

    const uint8_t *tiles = m->span(c, PKRED_ADDR_TILE_MAP, PK_TILE_MAP_CELLS);
    if (tiles)
        memcpy(s->tile_map, tiles, PK_TILE_MAP_CELLS);
    else
        for (int i = 0; i < PK_TILE_MAP_CELLS; i++)
            s->tile_map[i] = m->u8(c, (uint16_t)(PKRED_ADDR_TILE_MAP + i));
    for (int i = 0; i < PK_SPRITES; i++) {
        uint16_t base = (uint16_t)(PKRED_ADDR_SPRITE_STATE_DATA1 + i * PKRED_SPRITE_STATE_STRIDE);
        s->sprites[i].picture = m->u8(c, (uint16_t)(base + PKRED_SPRITE_PICTURE_ID));
        s->sprites[i].image = m->u8(c, (uint16_t)(base + PKRED_SPRITE_IMAGE_INDEX));
        s->sprites[i].y = m->u8(c, (uint16_t)(base + PKRED_SPRITE_Y_PIXELS));
        s->sprites[i].x = m->u8(c, (uint16_t)(base + PKRED_SPRITE_X_PIXELS));
    }

    const uint8_t *flags = m->span(c, PKRED_ADDR_EVENT_FLAGS, PK_EVENT_FLAG_BYTES);
    if (flags)
        memcpy(s->event_flags, flags, PK_EVENT_FLAG_BYTES);
    else
        for (int i = 0; i < PK_EVENT_FLAG_BYTES; i++)
            s->event_flags[i] = m->u8(c, (uint16_t)(PKRED_ADDR_EVENT_FLAGS + i));
}

#endif
