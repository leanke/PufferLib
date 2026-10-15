#ifndef POKERED_RAM_MAP_H
#define POKERED_RAM_MAP_H

#include <stdint.h>
#  include <assert.h>

#define PKRED_BE16(hi, lo) ((uint16_t)(((uint16_t)(hi) << 8) | (uint8_t)(lo)))

#if defined(__GNUC__) || defined(__clang__)
#  define PKRED_PACKED __attribute__((packed))
#else
#  define PKRED_PACKED
#endif
#ifdef _MSC_VER
#  pragma pack(push, 1)
#endif

typedef struct PKRED_PACKED {
    uint8_t species;
    uint8_t hp_hi, hp_lo;
    uint8_t box_level;
    uint8_t status;
    uint8_t type1, type2;
    uint8_t catch_rate;
    uint8_t moves[4];
    uint8_t ot_id_hi, ot_id_lo;
    uint8_t exp[3];
    uint8_t hp_exp_hi, hp_exp_lo;
    uint8_t attack_exp_hi, attack_exp_lo;
    uint8_t defense_exp_hi, defense_exp_lo;
    uint8_t speed_exp_hi, speed_exp_lo;
    uint8_t special_exp_hi, special_exp_lo;
    uint8_t dv_hi, dv_lo;
    uint8_t pp[4];
    uint8_t level;
    uint8_t max_hp_hi, max_hp_lo;
    uint8_t attack_hi, attack_lo;
    uint8_t defense_hi, defense_lo;
    uint8_t speed_hi, speed_lo;
    uint8_t special_hi, special_lo;
} PkredPartyMon;

#ifdef _MSC_VER
#  pragma pack(pop)
#endif

#if __STDC_VERSION__ >= 201112L
   static_assert(sizeof(PkredPartyMon) == 0x2C, "PkredPartyMon size mismatch");
#endif

#define PKRED_ADDR_PARTY_MON1 0xD16Bu
#define PKRED_PARTY_MON_STRIDE 0x2Cu
#define PKRED_ADDR_PARTY_MON(n) (PKRED_ADDR_PARTY_MON1 + (n) * PKRED_PARTY_MON_STRIDE)

#define PKRED_ADDR_CUR_MAP            0xD35Eu
#define PKRED_ADDR_CUR_MAP_TILESET    0xD367u
#define PKRED_ADDR_Y_COORD            0xD361u
#define PKRED_ADDR_X_COORD            0xD362u

#define PKRED_ADDR_PARTY_COUNT   0xD163u

#define PKRED_ADDR_IS_IN_BATTLE      0xD057u
#define PKRED_ADDR_TILE_IN_FRONT     0xCFC6u
#define PKRED_ADDR_BATTLE_TYPE       0xD05Au

#define PKRED_ADDR_BATTLE_MON_SPECIES   0xD014u
#define PKRED_ADDR_BATTLE_MON_HP        0xD015u
#define PKRED_ADDR_BATTLE_MON_STATUS    0xD018u
#define PKRED_ADDR_BATTLE_MON_LEVEL     0xD022u
#define PKRED_ADDR_BATTLE_MON_MAX_HP    0xD023u
#define PKRED_ADDR_ENEMY_MON_SPECIES    0xCFE5u
#define PKRED_ADDR_ENEMY_MON_HP         0xCFE6u
#define PKRED_ADDR_ENEMY_MON_STATUS     0xCFE9u
#define PKRED_ADDR_ENEMY_MON_LEVEL      0xCFF3u
#define PKRED_ADDR_ENEMY_MON_MAX_HP     0xCFF4u
#define PKRED_ADDR_EVENT_FLAGS       0xD747u
#define PKRED_ADDR_TILE_MAP          0xC3A0u
#define PKRED_TILE_BLANK             0x7Fu
#define PKRED_ADDR_SPRITE_STATE_DATA1 0xC100u
#define PKRED_SPRITE_STATE_STRIDE    16u
#define PKRED_SPRITE_PICTURE_ID      0u
#define PKRED_SPRITE_IMAGE_INDEX     2u
#define PKRED_SPRITE_Y_PIXELS        4u
#define PKRED_SPRITE_X_PIXELS        6u
#define PKRED_SPRITE_HIDDEN          0xFFu
#define PKRED_ADDR_OPTIONS           0xD355u
#define PKRED_OPTIONS_TEXT_DELAY_MASK 0x3Fu
#define PKRED_OPTIONS_BATTLE_SHIFT_BIT 6
#define PKRED_OPTIONS_BATTLE_ANIMATION_BIT 7
#define PKRED_ADDR_H_RANDOM_ADD      0xFFD3u
#define PKRED_ADDR_H_RANDOM_SUB      0xFFD4u
#define PKRED_ADDR_BATTLE_RESULT     0xCF0Bu
#define PKRED_BATTLE_RESULT_RAN      2u
#define PKRED_ADDR_BOX_COUNT         0xDA80u
#define PKRED_ADDR_PLAYER_SELECTED_MOVE 0xCCDCu

#define PKRED_ADDR_BATTLE_MON_TYPE1 0xD019u
#define PKRED_ADDR_BATTLE_MON_TYPE2 0xD01Au
#define PKRED_ADDR_BATTLE_MON_MOVES 0xD01Cu
#define PKRED_ADDR_BATTLE_MON_PP    0xD02Du
#define PKRED_STATUS_SLP_MASK 0x07u
#define PKRED_ADDR_ENEMY_MON_TYPE1  0xCFEAu
#define PKRED_ADDR_ENEMY_MON_TYPE2  0xCFEBu

#define PKRED_STATUS_PSN_BIT 3u
#define PKRED_STATUS_BRN_BIT 4u
#define PKRED_STATUS_FRZ_BIT 5u
#define PKRED_STATUS_PAR_BIT 6u

#define PKRED_ADDR_PLAYER_MONEY   0xD347u
#define PKRED_ADDR_OBTAINED_BADGES 0xD356u
#define PKRED_ADDR_TOWN_VISITED   0xD70Bu
#define PKRED_ADDR_POKEDEX_OWNED  0xD2F7u
#define PKRED_ADDR_POKEDEX_SEEN   0xD30Au
#define PKRED_POKEDEX_NUM_POKEMON 151
#define PKRED_ADDR_LAST_BLACKOUT_MAP 0xD719u

#define PKRED_NUM_BADGES 8
#define PKRED_BADGE_BIT_BOULDER 0u
#define PKRED_BADGE_BIT_CASCADE 1u
#define PKRED_BADGE_BIT_THUNDER 2u
#define PKRED_BADGE_BIT_RAINBOW 3u
#define PKRED_BADGE_BIT_SOUL    4u
#define PKRED_BADGE_BIT_MARSH   5u
#define PKRED_BADGE_BIT_VOLCANO 6u
#define PKRED_BADGE_BIT_EARTH   7u

#define PKRED_ADDR_NUM_BAG_ITEMS 0xD31Du
#define PKRED_ADDR_BAG_ITEMS     0xD31Eu
#define PKRED_BAG_ITEM_CAPACITY  20

#define PKRED_ITEM_MASTER_BALL   0x01u
#define PKRED_ITEM_ULTRA_BALL    0x02u
#define PKRED_ITEM_GREAT_BALL    0x03u
#define PKRED_ITEM_POKE_BALL     0x04u
#define PKRED_ITEM_TOWN_MAP      0x05u
#define PKRED_ITEM_BICYCLE       0x06u
#define PKRED_ITEM_SAFARI_BALL   0x08u
#define PKRED_ITEM_ANTIDOTE      0x0Bu
#define PKRED_ITEM_BURN_HEAL     0x0Cu
#define PKRED_ITEM_ICE_HEAL      0x0Du
#define PKRED_ITEM_AWAKENING     0x0Eu
#define PKRED_ITEM_PARLYZ_HEAL   0x0Fu
#define PKRED_ITEM_FULL_RESTORE  0x10u
#define PKRED_ITEM_MAX_POTION    0x11u
#define PKRED_ITEM_HYPER_POTION  0x12u
#define PKRED_ITEM_SUPER_POTION  0x13u
#define PKRED_ITEM_POTION        0x14u
#define PKRED_ITEM_ESCAPE_ROPE   0x1Du
#define PKRED_ITEM_REPEL         0x1Eu
#define PKRED_ITEM_OLD_AMBER     0x1Fu
#define PKRED_ITEM_DOME_FOSSIL   0x29u
#define PKRED_ITEM_HELIX_FOSSIL  0x2Au
#define PKRED_ITEM_SECRET_KEY    0x2Bu
#define PKRED_ITEM_BIKE_VOUCHER  0x2Du
#define PKRED_ITEM_CARD_KEY      0x30u
#define PKRED_ITEM_FULL_HEAL     0x34u
#define PKRED_ITEM_REVIVE        0x35u
#define PKRED_ITEM_MAX_REVIVE    0x36u
#define PKRED_ITEM_S_S_TICKET    0x3Fu
#define PKRED_ITEM_GOLD_TEETH    0x40u
#define PKRED_ITEM_COIN_CASE     0x45u
#define PKRED_ITEM_OAKS_PARCEL   0x46u
#define PKRED_ITEM_SILPH_SCOPE   0x48u
#define PKRED_ITEM_POKE_FLUTE    0x49u
#define PKRED_ITEM_LIFT_KEY      0x4Au
#define PKRED_ITEM_ETHER         0x50u
#define PKRED_ITEM_MAX_ETHER     0x51u
#define PKRED_ITEM_ELIXER        0x52u
#define PKRED_ITEM_MAX_ELIXER    0x53u
#define PKRED_ITEM_MOON_STONE    0x0Au
#define PKRED_ITEM_FIRE_STONE    0x20u
#define PKRED_ITEM_THUNDER_STONE 0x21u
#define PKRED_ITEM_WATER_STONE   0x22u
#define PKRED_ITEM_RARE_CANDY    0x28u
#define PKRED_ITEM_LEAF_STONE    0x2Fu
#define PKRED_ITEM_FRESH_WATER   0x3Cu
#define PKRED_ITEM_SODA_POP      0x3Du
#define PKRED_ITEM_LEMONADE      0x3Eu
#define PKRED_ITEM_HM01_CUT      0xC4u
#define PKRED_ITEM_HM02_FLY      0xC5u
#define PKRED_ITEM_HM03_SURF     0xC6u
#define PKRED_ITEM_HM04_STRENGTH 0xC7u
#define PKRED_ITEM_HM05_FLASH    0xC8u

#define PKRED_BAG_TRACKED_ITEMS 23
static const uint8_t PKRED_BAG_TRACKED_ITEM_IDS[PKRED_BAG_TRACKED_ITEMS] = {
    PKRED_ITEM_MASTER_BALL, PKRED_ITEM_ULTRA_BALL, PKRED_ITEM_GREAT_BALL,
    PKRED_ITEM_POKE_BALL, PKRED_ITEM_SAFARI_BALL,
    PKRED_ITEM_POTION, PKRED_ITEM_SUPER_POTION, PKRED_ITEM_HYPER_POTION,
    PKRED_ITEM_MAX_POTION, PKRED_ITEM_FULL_RESTORE,
    PKRED_ITEM_REVIVE, PKRED_ITEM_MAX_REVIVE, PKRED_ITEM_FULL_HEAL,
    PKRED_ITEM_ANTIDOTE, PKRED_ITEM_BURN_HEAL, PKRED_ITEM_ICE_HEAL,
    PKRED_ITEM_AWAKENING, PKRED_ITEM_PARLYZ_HEAL,
    PKRED_ITEM_ETHER, PKRED_ITEM_MAX_ETHER, PKRED_ITEM_ELIXER, PKRED_ITEM_MAX_ELIXER,
    PKRED_ITEM_ESCAPE_ROPE,
};

#define PKRED_KEY_ITEMS 16
static const uint8_t PKRED_KEY_ITEM_IDS[PKRED_KEY_ITEMS] = {
    PKRED_ITEM_TOWN_MAP, PKRED_ITEM_BICYCLE, PKRED_ITEM_OLD_AMBER,
    PKRED_ITEM_DOME_FOSSIL, PKRED_ITEM_HELIX_FOSSIL, PKRED_ITEM_SECRET_KEY,
    PKRED_ITEM_BIKE_VOUCHER, PKRED_ITEM_CARD_KEY, PKRED_ITEM_S_S_TICKET,
    PKRED_ITEM_GOLD_TEETH, PKRED_ITEM_COIN_CASE, PKRED_ITEM_OAKS_PARCEL,
    PKRED_ITEM_SILPH_SCOPE, PKRED_ITEM_POKE_FLUTE, PKRED_ITEM_LIFT_KEY,
    PKRED_ITEM_SAFARI_BALL,
};

#define PKRED_ADDR_PLAYER_SPRITE_FACING_DIRECTION 0xC109u

#define PKRED_ADDR_LIST_SCROLL_OFFSET 0xCC36u
#define PKRED_ADDR_LIST_POINTER      0xCF8Bu
#define PKRED_ADDR_LIST_MENU_ID      0xCF94u
#define PKRED_ITEMLISTMENU           3u
#define PKRED_LIST_MENU_X            5
#define PKRED_LIST_MENU_Y            4
#define PKRED_LIST_MENU_ROWS         3
#define PKRED_TILE_CURSOR            0xEDu
#define PKRED_TILE_CURSOR_HOLLOW     0xECu
#define PKRED_BIKE_STATE             1u

#define PKRED_SPECIES_SNORLAX 0x84u
#define PKRED_MAP_ROUTE_12    0x17u
#define PKRED_MAP_ROUTE_16    0x1Bu
#define PKRED_ADDR_ROUTE12_SNORLAX 0xD7D8u
#define PKRED_FIGHT_ROUTE12_SNORLAX_BIT 6
#define PKRED_BEAT_ROUTE12_SNORLAX_BIT 7
#define PKRED_ADDR_ROUTE16_SNORLAX 0xD7E0u
#define PKRED_FIGHT_ROUTE16_SNORLAX_BIT 0
#define PKRED_BEAT_ROUTE16_SNORLAX_BIT 1

#define PKRED_POKECENTER_MAPS 12
static const uint8_t PKRED_POKECENTER_MAP_IDS[PKRED_POKECENTER_MAPS] = {
    41, 58, 64, 68, 81, 89, 133, 141, 154, 171, 174, 182,
};

#define PKRED_ADDR_OVERWORLD_MAP     0xC6E8u
#define PKRED_OVERWORLD_MAP_SIZE     1300
#define PKRED_ADDR_CUT_TILE          0xCD4Du
#define PKRED_CUT_TILE_GRASS         0x52u
#define PKRED_ADDR_UPDATE_SPRITES    0xCFCBu
#define PKRED_SPRITES_ANIMATING      0xFFu
#define PKRED_ADDR_MAP_PAL_OFFSET    0xD35Du
#define PKRED_ADDR_WALK_BIKE_SURF    0xD700u
#define PKRED_ADDR_STATUS_FLAGS1     0xD728u
#define PKRED_STATUS_FLAGS1_STRENGTH_BIT 0u
#define PKRED_ADDR_STATUS_FLAGS7     0xD733u
#define PKRED_STATUS_FLAGS7_USED_FLY_BIT 7u
#define PKRED_SURF_STATE             2u
#define PKRED_PAL_DARK_CAVE          6u
#define PKRED_PP_MASK                0x3Fu

#define PKRED_ADDR_TOP_MENU_ITEM_Y   0xCC24u
#define PKRED_ADDR_TOP_MENU_ITEM_X   0xCC25u
#define PKRED_ADDR_CURRENT_MENU_ITEM 0xCC26u
#define PKRED_ADDR_MAX_MENU_ITEM     0xCC28u
#define PKRED_ADDR_MENU_WATCHED_KEYS 0xCC29u
#define PKRED_ADDR_FIELD_MOVES       0xCD3Du
#define PKRED_START_MENU_X           0x0Bu
#define PKRED_START_MENU_Y           0x02u
#define PKRED_START_MENU_KEYS        0xCBu
#define PKRED_START_MENU_MAX_DEX     7u
#define PKRED_PARTY_MENU_Y           0x01u
#define PKRED_MENU_KEYS_A_B          0x03u
#define PKRED_NUM_FIELD_MOVES        4
#define PKRED_TILE_TEXT_FIRST        0x60u
#define PKRED_TILE_UP_ARROW          0xEDu
#define PKRED_FLY_MAP_ARROW_X        18
#define PKRED_TILE_CUT_TREE          0x3Du
#define PKRED_TILE_GYM_CUT_TREE      0x50u
#define PKRED_TILE_WATER             0x14u
#define PKRED_TILE_SHORE             0x32u
#define PKRED_TILE_SAFARI_SHORE      0x48u
#define PKRED_TILESET_OVERWORLD      0u
#define PKRED_TILESET_GYM            7u
#define PKRED_TILESET_SHIP_PORT      14u
#define PKRED_TILESET_PLATEAU        23u
static const uint8_t PKRED_WATER_TILESETS[] = {0, 3, 5, 7, 13, 14, 17, 22, 23};

static inline uint32_t pkred_hash_bytes(const uint8_t *p, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

#define PKRED_MOVE_CUT      15u
#define PKRED_MOVE_FLY      19u
#define PKRED_MOVE_SURF     57u
#define PKRED_MOVE_STRENGTH 70u
#define PKRED_MOVE_FLASH    148u

#define PKRED_ITEM_TM01 0xC9u
#define PKRED_NUM_TMS   50
static const uint8_t PKRED_TM_MOVES[PKRED_NUM_TMS] = {
    5,   13,  14,  18,  25,  92,  32,  34,  36,  38,  61,  55,  58,  59,  63,  6,   66,
    68,  69,  99,  72,  76,  82,  85,  87,  89,  90,  91,  94,  100, 102, 104, 115, 117,
    118, 120, 121, 126, 129, 130, 135, 138, 143, 156, 86,  149, 153, 157, 161, 164,
};

#define PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT 0xD5F4u

#ifndef PKRED_MOVE_PP_H
#define PKRED_MOVE_PP_H

#include <stdint.h>

#define PKRED_NUM_MOVES 165

static const uint8_t PKRED_MOVE_BASE_PP[PKRED_NUM_MOVES + 1] = {
    0, 35, 25, 10, 15, 20, 20, 15, 15, 15, 35, 30, 5, 10, 30, 30,
    35, 35, 20, 15, 20, 20, 10, 20, 30, 5, 25, 15, 15, 15, 25, 20,
    5, 35, 15, 20, 20, 20, 15, 30, 35, 20, 20, 30, 25, 40, 20, 15,
    20, 20, 20, 30, 25, 15, 30, 25, 5, 15, 10, 5, 20, 20, 20, 5,
    35, 20, 25, 20, 20, 20, 15, 20, 10, 10, 40, 25, 10, 35, 30, 15,
    20, 40, 10, 15, 30, 15, 20, 10, 15, 10, 5, 10, 10, 25, 10, 20,
    40, 30, 30, 20, 20, 15, 10, 40, 15, 20, 30, 20, 20, 10, 40, 40,
    30, 30, 30, 20, 30, 10, 10, 20, 5, 10, 30, 20, 20, 20, 5, 15,
    10, 20, 15, 15, 35, 20, 15, 10, 20, 30, 15, 40, 20, 15, 10, 5,
    10, 30, 10, 15, 20, 15, 40, 40, 10, 5, 15, 10, 10, 10, 15, 30,
    30, 10, 10, 20, 10, 10,
};

#endif

#define PKRED_NUM_TILESETS 24
#define PKRED_TILESET_COLL_MAX 19

static const uint8_t PKRED_TILESET_COLLISION[PKRED_NUM_TILESETS][PKRED_TILESET_COLL_MAX + 1] = {
    {0x00, 0x10, 0x1b, 0x20, 0x21, 0x23, 0x2c, 0x2d, 0x2e, 0x30, 0x31, 0x33, 0x39, 0x3c, 0x3e, 0x52, 0x54, 0x58, 0x5b, 0xff},
    {0x01, 0x02, 0x03, 0x11, 0x12, 0x13, 0x14, 0x1c, 0x1a, 0xff},
    {0x11, 0x1a, 0x1c, 0x3c, 0x5e, 0xff},
    {0x1e, 0x20, 0x2e, 0x30, 0x34, 0x37, 0x39, 0x3a, 0x40, 0x51, 0x52, 0x5a, 0x5c, 0x5e, 0x5f, 0xff},
    {0x01, 0x02, 0x03, 0x11, 0x12, 0x13, 0x14, 0x1c, 0x1a, 0xff},
    {0x11, 0x16, 0x19, 0x2b, 0x3c, 0x3d, 0x3f, 0x4a, 0x4c, 0x4d, 0x03, 0xff},
    {0x11, 0x1a, 0x1c, 0x3c, 0x5e, 0xff},
    {0x11, 0x16, 0x19, 0x2b, 0x3c, 0x3d, 0x3f, 0x4a, 0x4c, 0x4d, 0x03, 0xff},
    {0x01, 0x12, 0x14, 0x28, 0x32, 0x37, 0x44, 0x54, 0x5c, 0xff},
    {0x01, 0x12, 0x14, 0x1a, 0x1c, 0x37, 0x38, 0x3b, 0x3c, 0x5e, 0xff},
    {0x01, 0x12, 0x14, 0x1a, 0x1c, 0x37, 0x38, 0x3b, 0x3c, 0x5e, 0xff},
    {0x0b, 0x0c, 0x13, 0x15, 0x18, 0xff},
    {0x01, 0x12, 0x14, 0x1a, 0x1c, 0x37, 0x38, 0x3b, 0x3c, 0x5e, 0xff},
    {0x04, 0x0d, 0x17, 0x1d, 0x1e, 0x23, 0x34, 0x37, 0x39, 0x4a, 0xff},
    {0x0a, 0x1a, 0x32, 0x3b, 0xff},
    {0x01, 0x10, 0x13, 0x1b, 0x22, 0x42, 0x52, 0xff},
    {0x04, 0x0f, 0x15, 0x1f, 0x3b, 0x45, 0x47, 0x55, 0x56, 0xff},
    {0x05, 0x15, 0x18, 0x1a, 0x20, 0x21, 0x22, 0x2a, 0x2d, 0x30, 0xff},
    {0x14, 0x17, 0x1a, 0x1c, 0x20, 0x38, 0x45, 0xff},
    {0x01, 0x05, 0x11, 0x12, 0x14, 0x1a, 0x1c, 0x2c, 0x53, 0xff},
    {0x0c, 0x26, 0x16, 0x1e, 0x34, 0x37, 0xff},
    {0x0f, 0x1a, 0x1f, 0x26, 0x28, 0x29, 0x2c, 0x2d, 0x2e, 0x2f, 0x41, 0xff},
    {0x01, 0x10, 0x11, 0x13, 0x1b, 0x20, 0x21, 0x22, 0x30, 0x31, 0x32, 0x42, 0x43, 0x48, 0x52, 0x55, 0x58, 0x5e, 0xff},
    {0x1b, 0x23, 0x2c, 0x2d, 0x3b, 0x45, 0xff},
};

#endif
