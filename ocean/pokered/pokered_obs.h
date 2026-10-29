#ifndef POKERED_OBSERVATIONS_H
#define POKERED_OBSERVATIONS_H

#include <math.h>
#include <string.h>

static inline bool is_battle_active(const PkSnapshot *s) {
  return s->in_battle == 1 || s->in_battle == 2;
}

static inline float mon_hp_fraction(const PkMon *m) {
  return (m->max_hp > 0) ? (float)m->hp / (float)m->max_hp : 0.0f;
}

static inline obs_t obs_fraction(float f) {
  return (obs_t)(PK_OBS_HP_SCALE == 1.0f ? f : lrintf(f * PK_OBS_HP_SCALE));
}

static void observe_visited_mask(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  for (int by = 0; by < BLOCKS_TALL; by++) {
    int wy = (int)s->y + (by - PLAYER_BLOCK_ROW);
    for (int bx = 0; bx < BLOCKS_WIDE; bx++) {
      int wx = (int)s->x + (bx - PLAYER_BLOCK_COL);
      bool in_map = wx >= 0 && wx < MAX_X && wy >= 0 && wy < MAX_Y;
      out[by * BLOCKS_WIDE + bx] =
          (obs_t)(in_map && vbit_get(env->visited_coords, coord_index(s->map_n, (uint8_t)wx, (uint8_t)wy)) ? 1 : 0);
    }
  }
}

static void observe_mon(const Env *env, const PkMon *m, obs_t *out, bool moves_visible) {
  out[MON_SPECIES] = (obs_t)m->species;
  out[MON_LEVEL] = (obs_t)m->level;
  out[MON_HP] = obs_fraction(mon_hp_fraction(m));
  out[MON_STATUS] = (obs_t)m->status;
  for (int k = 0; k < MON_MOVE_SLOTS; k++) {
    int mx = moves_visible ? move_max_pp(m->moves[k], m->pp[k]) : 0;
    out[MON_MOVES + k] = (obs_t)(moves_visible ? m->moves[k] : 0);
    out[MON_MOVE_PP + k] = mx
        ? obs_fraction(fminf(1.0f, (float)(m->pp[k] & PKRED_PP_MASK) / (float)mx)) : (obs_t)0;
  }
}

static const uint8_t PK_OBS_KEY_ITEMS[] = {
    PKRED_ITEM_SILPH_SCOPE, PKRED_ITEM_LIFT_KEY,    PKRED_ITEM_CARD_KEY,     PKRED_ITEM_SECRET_KEY,
    PKRED_ITEM_POKE_FLUTE,  PKRED_ITEM_GOLD_TEETH,  PKRED_ITEM_S_S_TICKET,   PKRED_ITEM_BICYCLE,
    PKRED_ITEM_HM01_CUT,    PKRED_ITEM_HM02_FLY,    PKRED_ITEM_HM03_SURF,    PKRED_ITEM_HM04_STRENGTH,
    PKRED_ITEM_HM05_FLASH,
};
typedef char pk_obs_key_items_fit[sizeof(PK_OBS_KEY_ITEMS) + 1 == INFO_KEY_ITEM_BITS ? 1 : -1];

static void observe_key_items(const PkSnapshot *s, obs_t *out) {
  for (size_t i = 0; i < sizeof(PK_OBS_KEY_ITEMS); i++)
    out[i] = (obs_t)pk_bag_has(s, PK_OBS_KEY_ITEMS[i]);
  out[sizeof(PK_OBS_KEY_ITEMS)] = (obs_t)s->gave_guards_drink;
}

static void observe_info(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  memset(out, 0, INFO_OBS * sizeof(obs_t));
  out[INFO_MAP] = (obs_t)s->map_n;
  out[INFO_TILESET] = (obs_t)s->tileset;
  out[INFO_BADGES] = (obs_t)s->badges;
  out[INFO_FACING] = (obs_t)(s->facing & 3);
  out[INFO_X] = (obs_t)s->x;
  out[INFO_Y] = (obs_t)s->y;
  out[INFO_TOWNS_LO] = (obs_t)(s->towns_visited & 0xFF);
  out[INFO_TOWNS_HI] = (obs_t)((s->towns_visited >> 8) & ((1u << (TOWN_BITS - 8)) - 1));
  out[INFO_MOVEMENT] = (obs_t)(s->surfing ? MOVEMENT_SURF : s->biking ? MOVEMENT_BIKE : MOVEMENT_WALK);
  out[INFO_STRENGTH] = (obs_t)s->strength_active;
  out[INFO_MONEY] = (obs_t)(s->money / 10000 % 100);
  out[INFO_MONEY + 1] = (obs_t)(s->money / 100 % 100);
  out[INFO_MONEY + 2] = (obs_t)(s->money % 100);
  out[INFO_DEX_OWNED] = (obs_t)s->pokedex_owned_count;
  out[INFO_DEX_SEEN] = (obs_t)s->pokedex_seen_count;
  out[INFO_EVENTS] = obs_fraction((float)pk_completed_events(s->event_flags) / (float)EVENT_COUNT);
  int map_total;
  out[INFO_MAP_EVENTS_LEFT] = (obs_t)pk_map_events_left(s->event_flags, env->event_reward_once ? env->paid_events : NULL,
                                                        s->map_n, &map_total);
  out[INFO_MAP_EVENTS_TOTAL] = (obs_t)map_total;
  pk_progress_event_bits(s, env->obs_progress_gating, out + INFO_PROGRESS);
  observe_key_items(s, out + INFO_KEY_ITEMS);
}

static void observe_battle(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  memset(out, 0, BATTLE_OBS * sizeof(obs_t));
  if (!is_battle_active(s))
    return;
  bool ghost = pk_ghost_battle(s);
  out[0] = (obs_t)(s->in_battle == 2 ? BATTLE_TYPE_TRAINER : ghost ? BATTLE_TYPE_GHOST : BATTLE_TYPE_WILD);
  observe_mon(env, &s->battle_mon, out + BATTLE_PLAYER_MON_OFFSET, true);
  observe_mon(env, &s->enemy_mon, out + BATTLE_ENEMY_MON_OFFSET, false);
  if (ghost)
    out[BATTLE_ENEMY_MON_OFFSET + MON_SPECIES] = 0;
}

static void observe_party(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  memset(out, 0, PARTY_OBS * sizeof(obs_t));
  for (int i = 0; i < PARTY_SIZE && i < s->party_count; i++)
    observe_mon(env, &s->party[i], out + i * MON_FIELDS, true);
}

static void observe_bag(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  memset(out, 0, BAG_OBS * sizeof(obs_t));
  for (int i = 0; i < BAG_SLOTS && i < s->bag_count; i++) {
    out[i * BAG_FIELDS + BAG_ITEM] = (obs_t)s->bag[i].item;
    out[i * BAG_FIELDS + BAG_COUNT] = (obs_t)s->bag[i].count;
    out[i * BAG_FIELDS + BAG_USABLE] = (obs_t)item_usable(s, s->bag[i].item);
    out[i * BAG_FIELDS + BAG_CURSOR] = (obs_t)(s->bag_cursor == i + 1);
  }
  for (int i = 0; i < PARTY_SIZE && i < s->party_count; i++)
    out[BAG_HM_LEARNABLE + i] = (obs_t)mon_hm_learnable(s, &s->party[i]);
}

static void observe_tiles(const Env *env, obs_t *out) {
  for (int i = 0; i < PK_TILE_MAP_CELLS; i++)
    out[i] = (obs_t)env->cur.tile_map[i];
}

static void update_observations(Env *env) {
  if (!env || !env->agents[0].observations)
    return;

  obs_t *obs = env->agents[0].observations;

  observe_tiles(env, obs + TILE_OBS_OFFSET);
  observe_info(env, obs + INFO_OBS_OFFSET);
  observe_visited_mask(env, obs + VISITED_MASK_OFFSET);
  observe_battle(env, obs + BATTLE_OBS_OFFSET);
  observe_party(env, obs + PARTY_OBS_OFFSET);
  observe_bag(env, obs + BAG_OBS_OFFSET);
}

#endif
