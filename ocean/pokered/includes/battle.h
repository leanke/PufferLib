#include "../gambatte/gambatte_wrapper.h"
#include "ram_map.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  int8_t in_battle;
} BattleState;

static inline bool is_battle_active(const BattleState *b) {
  return b->in_battle == 1 || b->in_battle == 2;
}

static inline uint16_t read_big_endian_16(Emulator *emu, uint16_t addr) {
  return PKRED_BE16(read_mem(emu, addr), read_mem(emu, addr + 1));
}

static inline void update_battle_state(BattleState *battle, Emulator *emu) {
  battle->in_battle = (int8_t)read_mem(emu, PKRED_ADDR_IS_IN_BATTLE);
}

static inline float battle_mon_hp_fraction(Emulator *emu) {
  uint16_t hp = read_big_endian_16(emu, PKRED_ADDR_BATTLE_MON_HP);
  uint16_t maxhp = read_big_endian_16(emu, PKRED_ADDR_BATTLE_MON_MAX_HP);
  return (maxhp > 0) ? (float)hp / (float)maxhp : 0.0f;
}

static inline float enemy_mon_hp_fraction(Emulator *emu) {
  uint16_t hp = read_big_endian_16(emu, PKRED_ADDR_ENEMY_MON_HP);
  uint16_t maxhp = read_big_endian_16(emu, PKRED_ADDR_ENEMY_MON_MAX_HP);
  return (maxhp > 0) ? (float)hp / (float)maxhp : 0.0f;
}

static inline float party_hp_fraction(Emulator *emu) {
  uint8_t count = read_mem(emu, PKRED_ADDR_PARTY_COUNT);
  if (count == 0 || count > 6) return 1.0f;

  uint32_t total_hp = 0, total_maxhp = 0;
  for (int i = 0; i < count; i++) {
    uint16_t base = PKRED_ADDR_PARTY_MON(i);
    total_hp    += read_big_endian_16(emu, base + offsetof(PkredPartyMon, hp_hi));
    total_maxhp += read_big_endian_16(emu, base + offsetof(PkredPartyMon, max_hp_hi));
  }
  return (total_maxhp > 0) ? (float)total_hp / (float)total_maxhp : 1.0f;
}

static inline uint8_t battle_mon_species(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_BATTLE_MON_SPECIES);
}

static inline uint8_t battle_mon_level(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_BATTLE_MON_LEVEL);
}

static inline uint8_t battle_mon_status(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_BATTLE_MON_STATUS);
}

static inline uint8_t enemy_mon_species(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_ENEMY_MON_SPECIES);
}

static inline uint8_t enemy_mon_level(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_ENEMY_MON_LEVEL);
}

static inline uint8_t enemy_mon_status(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_ENEMY_MON_STATUS);
}

static inline uint8_t battle_mon_type1(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_BATTLE_MON_TYPE1);
}

static inline uint8_t battle_mon_type2(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_BATTLE_MON_TYPE2);
}

static inline uint8_t enemy_mon_type1(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_ENEMY_MON_TYPE1);
}

static inline uint8_t enemy_mon_type2(Emulator *emu) {
  return read_mem(emu, PKRED_ADDR_ENEMY_MON_TYPE2);
}

static inline uint8_t battle_mon_move(Emulator *emu, int m) {
  return read_mem(emu, PKRED_ADDR_BATTLE_MON_MOVES + m);
}

static inline uint8_t battle_mon_pp(Emulator *emu, int m) {
  return read_mem(emu, PKRED_ADDR_BATTLE_MON_PP + m);
}

static inline uint8_t party_fainted_count(Emulator *emu) {
  uint8_t count = read_mem(emu, PKRED_ADDR_PARTY_COUNT);
  if (count == 0 || count > 6) return 0;

  uint8_t fainted = 0;
  for (int i = 0; i < count; i++) {
    uint16_t base = PKRED_ADDR_PARTY_MON(i);
    if (read_big_endian_16(emu, base + offsetof(PkredPartyMon, hp_hi)) == 0)
      fainted++;
  }
  return fainted;
}
