#ifndef POKERED_RAM_MAP_H
#define POKERED_RAM_MAP_H

#include <stdint.h>

/* Combine a big-endian hi/lo byte pair into a uint16_t. */
#define PKRED_BE16(hi, lo) ((uint16_t)(((uint16_t)(hi) << 8) | (uint8_t)(lo)))


#if defined(__GNUC__) || defined(__clang__)
#  define PKRED_PACKED __attribute__((packed))
#else
#  define PKRED_PACKED
#endif
#ifdef _MSC_VER
#  pragma pack(push, 1)
#endif

/* party_struct = box_struct + level/stats (wram.asm:9-39). Used for each
 * of the 6 party slots. Length: 0x2C (44). */
typedef struct PKRED_PACKED {
    uint8_t species;
    uint8_t hp_hi, hp_lo;          /* current HP, big-endian */
    uint8_t box_level;             /* level, shadowed here for out-of-battle display */
    uint8_t status;
    uint8_t type1, type2;
    uint8_t catch_rate;            /* also holds held item in Yellow; unused item in R/B */
    uint8_t moves[4];
    uint8_t ot_id_hi, ot_id_lo;
    uint8_t exp[3];                /* 24-bit, big-endian */
    uint8_t hp_exp_hi, hp_exp_lo;
    uint8_t attack_exp_hi, attack_exp_lo;
    uint8_t defense_exp_hi, defense_exp_lo;
    uint8_t speed_exp_hi, speed_exp_lo;
    uint8_t special_exp_hi, special_exp_lo;
    uint8_t dv_hi, dv_lo;
    uint8_t pp[4];
    uint8_t level;                 /* authoritative level */
    uint8_t max_hp_hi, max_hp_lo;
    uint8_t attack_hi, attack_lo;
    uint8_t defense_hi, defense_lo;
    uint8_t speed_hi, speed_lo;
    uint8_t special_hi, special_lo;
} PkredPartyMon; /* sizeof == 0x2C */

#ifdef _MSC_VER
#  pragma pack(pop)
#endif

#if __STDC_VERSION__ >= 201112L
#  include <assert.h>
   static_assert(sizeof(PkredPartyMon) == 0x2C, "PkredPartyMon size mismatch");
#endif


/* Party (6 slots, 0x2C apart). */
#define PKRED_ADDR_PARTY_MON1 0xD16Bu
#define PKRED_PARTY_MON_STRIDE 0x2Cu
#define PKRED_ADDR_PARTY_MON(n) (PKRED_ADDR_PARTY_MON1 + (n) * PKRED_PARTY_MON_STRIDE) /* n = 0..5 */

/* -- Player / world -- */
#define PKRED_ADDR_CUR_MAP            0xD35Eu /* wCurMap */
#define PKRED_ADDR_Y_COORD            0xD361u /* wYCoord: player's row on current map */
#define PKRED_ADDR_X_COORD            0xD362u /* wXCoord: player's column on current map */

/* -- Party -- */
#define PKRED_ADDR_PARTY_COUNT   0xD163u /* wPartyCount: 0-6 */

/* -- Battle state -- */
#define PKRED_ADDR_IS_IN_BATTLE      0xD057u /* wIsInBattle: 0=no, 1=wild, 2=trainer, 0xFF(-1)=lost */
#define PKRED_ADDR_BATTLE_TYPE       0xD05Au /* wBattleType: 0=normal, 1=old man, 2=safari */

/* -- Progress / collection -- */
#define PKRED_ADDR_PLAYER_MONEY   0xD347u /* wPlayerMoney: 3-byte BCD, big-endian digit pairs */
#define PKRED_ADDR_OBTAINED_BADGES 0xD356u /* wObtainedBadges: 1 bit per gym badge, bit0=Boulder..bit7=Earth */
#define PKRED_ADDR_POKEDEX_OWNED  0xD2F7u /* wPokedexOwned: 151-bit flag array, 19 bytes */
#define PKRED_ADDR_POKEDEX_SEEN   0xD30Au /* wPokedexSeen: 151-bit flag array, 19 bytes */
#define PKRED_POKEDEX_NUM_POKEMON 151

/* -- Sprites / movement -- */
#define PKRED_ADDR_PLAYER_SPRITE_FACING_DIRECTION 0xC109u /* wSpritePlayerStateData1 + 3 */

/* -- Scripted-event overrides (env-specific workarounds) -- */
#define PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT 0xD5F4u /* wViridianCityCurScript */
#define PKRED_ADDR_WD72E                    0xD72Eu /* wd72e */
#define PKRED_WD72E_DISABLE_BATTLES_BIT     4

#endif /* POKERED_RAM_MAP_H */
