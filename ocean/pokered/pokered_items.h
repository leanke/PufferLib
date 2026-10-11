#ifndef POKERED_ITEMS_H
#define POKERED_ITEMS_H

#include "data/species_items.h"

typedef enum {
  PK_ITEM_OTHER,
  PK_ITEM_HEAL,
  PK_ITEM_FULL_RESTORE,
  PK_ITEM_CURE,
  PK_ITEM_REVIVE,
  PK_ITEM_PP,
  PK_ITEM_CANDY,
  PK_ITEM_STONE,
  PK_ITEM_TM,
  PK_ITEM_HM,
  PK_ITEM_BALL,
  PK_ITEM_BICYCLE,
  PK_ITEM_FLUTE,
} PkItemKind;

enum { PK_USE_HP, PK_USE_CURE, PK_USE_REVIVE, PK_USE_PP, PK_USE_CANDY, PK_USE_STONE, PK_USE_KINDS };

typedef struct {
  uint8_t consumed, effect;
  uint8_t consumed_age[PK_USE_KINDS], effect_age[PK_USE_KINDS];
  float amount[PK_USE_KINDS];
} PkItemUse;

enum {
  PK_KEY_USE_BICYCLE = 1u << 0,
  PK_KEY_USE_FLUTE_ROUTE12 = 1u << 1,
  PK_KEY_USE_FLUTE_ROUTE16 = 1u << 2,
};

#define PK_NUM_HMS 5
static const uint8_t PK_HM_MOVES[PK_NUM_HMS] = {PKRED_MOVE_CUT, PKRED_MOVE_FLY, PKRED_MOVE_SURF,
                                                PKRED_MOVE_STRENGTH, PKRED_MOVE_FLASH};
static const uint8_t PK_STONE_ITEMS[] = {PKRED_ITEM_MOON_STONE, PKRED_ITEM_FIRE_STONE, PKRED_ITEM_THUNDER_STONE,
                                        PKRED_ITEM_WATER_STONE, PKRED_ITEM_LEAF_STONE};

static PkItemKind item_kind(uint8_t item) {
  switch (item) {
  case PKRED_ITEM_POTION: case PKRED_ITEM_SUPER_POTION: case PKRED_ITEM_HYPER_POTION: case PKRED_ITEM_MAX_POTION:
  case PKRED_ITEM_FRESH_WATER: case PKRED_ITEM_SODA_POP: case PKRED_ITEM_LEMONADE:
    return PK_ITEM_HEAL;
  case PKRED_ITEM_FULL_RESTORE:
    return PK_ITEM_FULL_RESTORE;
  case PKRED_ITEM_ANTIDOTE: case PKRED_ITEM_BURN_HEAL: case PKRED_ITEM_ICE_HEAL: case PKRED_ITEM_AWAKENING:
  case PKRED_ITEM_PARLYZ_HEAL: case PKRED_ITEM_FULL_HEAL:
    return PK_ITEM_CURE;
  case PKRED_ITEM_REVIVE: case PKRED_ITEM_MAX_REVIVE:
    return PK_ITEM_REVIVE;
  case PKRED_ITEM_ETHER: case PKRED_ITEM_MAX_ETHER: case PKRED_ITEM_ELIXER: case PKRED_ITEM_MAX_ELIXER:
    return PK_ITEM_PP;
  case PKRED_ITEM_RARE_CANDY:
    return PK_ITEM_CANDY;
  case PKRED_ITEM_MOON_STONE: case PKRED_ITEM_FIRE_STONE: case PKRED_ITEM_THUNDER_STONE: case PKRED_ITEM_WATER_STONE:
  case PKRED_ITEM_LEAF_STONE:
    return PK_ITEM_STONE;
  case PKRED_ITEM_MASTER_BALL: case PKRED_ITEM_ULTRA_BALL: case PKRED_ITEM_GREAT_BALL: case PKRED_ITEM_POKE_BALL:
  case PKRED_ITEM_SAFARI_BALL:
    return PK_ITEM_BALL;
  case PKRED_ITEM_BICYCLE:
    return PK_ITEM_BICYCLE;
  case PKRED_ITEM_POKE_FLUTE:
    return PK_ITEM_FLUTE;
  }
  if (item >= PKRED_ITEM_HM01_CUT && item < PKRED_ITEM_HM01_CUT + PK_NUM_HMS)
    return PK_ITEM_HM;
  if (item >= PKRED_ITEM_TM01 && item < PKRED_ITEM_TM01 + PKRED_NUM_TMS)
    return PK_ITEM_TM;
  return PK_ITEM_OTHER;
}

static uint8_t cure_status_mask(uint8_t item) {
  switch (item) {
  case PKRED_ITEM_ANTIDOTE: return 1u << PKRED_STATUS_PSN_BIT;
  case PKRED_ITEM_BURN_HEAL: return 1u << PKRED_STATUS_BRN_BIT;
  case PKRED_ITEM_ICE_HEAL: return 1u << PKRED_STATUS_FRZ_BIT;
  case PKRED_ITEM_PARLYZ_HEAL: return 1u << PKRED_STATUS_PAR_BIT;
  case PKRED_ITEM_AWAKENING: return PKRED_STATUS_SLP_MASK;
  }
  return 0xFF;
}

static int bag_item_count(const PkSnapshot *s, uint8_t item) {
  for (int i = 0; i < s->bag_count; i++)
    if (s->bag[i].item == item)
      return s->bag[i].count;
  return 0;
}

static bool mon_knows(const PkMon *m, uint8_t move) {
  for (int k = 0; k < 4; k++)
    if (m->moves[k] == move)
      return true;
  return false;
}

static bool mon_can_learn(const PkMon *m, int tmhm_bit, uint8_t move) {
  return m->species && ((PKRED_SPECIES_TMHM[m->species] >> tmhm_bit) & 1) && !mon_knows(m, move);
}

static int move_max_pp(uint8_t move, uint8_t pp) {
  if (move == 0 || move > PKRED_NUM_MOVES)
    return 0;
  int base = PKRED_MOVE_BASE_PP[move];
  int bonus = base / 5 < 7 ? base / 5 : 7;
  return base + (pp >> 6) * bonus;
}

static int move_pp_total(const PkMon *m, int *max_out) {
  int pp = 0, max = 0;
  for (int k = 0; k < 4; k++) {
    int mx = move_max_pp(m->moves[k], m->pp[k]);
    if (mx) {
      pp += m->pp[k] & PKRED_PP_MASK;
      max += mx;
    }
  }
  if (max_out)
    *max_out = max;
  return pp;
}

static bool event_bit(const PkSnapshot *s, uint16_t addr, int bit) {
  return (s->event_flags[addr - PKRED_ADDR_EVENT_FLAGS] >> bit) & 1;
}

static bool flute_snorlax_here(const PkSnapshot *s) {
  if (s->map_n == PKRED_MAP_ROUTE_12)
    return !event_bit(s, PKRED_ADDR_ROUTE12_SNORLAX, PKRED_BEAT_ROUTE12_SNORLAX_BIT);
  if (s->map_n == PKRED_MAP_ROUTE_16)
    return !event_bit(s, PKRED_ADDR_ROUTE16_SNORLAX, PKRED_BEAT_ROUTE16_SNORLAX_BIT);
  return false;
}

static bool item_usable(const PkSnapshot *s, uint8_t item) {
  PkItemKind kind = item_kind(item);
  bool battle = s->in_battle == 1 || s->in_battle == 2;
  if (kind == PK_ITEM_BALL)
    return s->in_battle == 1;
  if (kind == PK_ITEM_BICYCLE)
    return !battle && !s->biking && !s->surfing;
  if (kind == PK_ITEM_FLUTE)
    return !battle && flute_snorlax_here(s);
  if (battle && (kind == PK_ITEM_TM || kind == PK_ITEM_HM || kind == PK_ITEM_STONE || kind == PK_ITEM_CANDY))
    return false;
  for (int i = 0; i < s->party_count; i++) {
    const PkMon *m = &s->party[i];
    int max_pp = 0;
    switch (kind) {
    case PK_ITEM_HEAL:
      if (m->hp && m->hp < m->max_hp) return true;
      break;
    case PK_ITEM_FULL_RESTORE:
      if (m->hp && (m->hp < m->max_hp || m->status)) return true;
      break;
    case PK_ITEM_CURE:
      if (m->hp && (m->status & cure_status_mask(item))) return true;
      break;
    case PK_ITEM_REVIVE:
      if (m->species && !m->hp) return true;
      break;
    case PK_ITEM_PP:
      if (move_pp_total(m, &max_pp) < max_pp) return true;
      break;
    case PK_ITEM_CANDY:
      if (m->species && m->level < 100) return true;
      break;
    case PK_ITEM_STONE:
      for (int k = 0; k < (int)sizeof(PK_STONE_ITEMS); k++)
        if (PK_STONE_ITEMS[k] == item && ((PKRED_SPECIES_STONES[m->species] >> k) & 1)) return true;
      break;
    case PK_ITEM_TM:
      if (mon_can_learn(m, item - PKRED_ITEM_TM01, PKRED_TM_MOVES[item - PKRED_ITEM_TM01])) return true;
      break;
    case PK_ITEM_HM:
      if (mon_can_learn(m, 50 + item - PKRED_ITEM_HM01_CUT, PK_HM_MOVES[item - PKRED_ITEM_HM01_CUT])) return true;
      break;
    default:
      return false;
    }
  }
  return false;
}

static uint8_t mon_hm_learnable(const PkSnapshot *s, const PkMon *m) {
  uint8_t mask = 0;
  for (int h = 0; h < PK_NUM_HMS; h++)
    if (bag_item_count(s, (uint8_t)(PKRED_ITEM_HM01_CUT + h)) && mon_can_learn(m, 50 + h, PK_HM_MOVES[h]))
      mask |= (uint8_t)(1u << h);
  return mask;
}

#endif
