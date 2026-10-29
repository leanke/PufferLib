#ifndef POKERED_REWARDS_H
#define POKERED_REWARDS_H

#include <string.h>

static int party_level_sum(const PkSnapshot *s) {
  int sum = 0;
  for (int i = 0; i < PARTY_SIZE; i++)
    sum += s->party[i].level;
  return sum;
}


static float signal_exploration_t(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;

  if (!is_directional_action(env->prev_action) || is_battle_active(cur))
    return 0.0f;
  if (cur->x == prev->x && cur->y == prev->y && cur->map_n == prev->map_n)
    return 0.0f;

  return 1.0f;
}

static float signal_exploration(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;

  if (!is_directional_action(env->prev_action) || is_battle_active(cur))
    return 0.0f;
  if (cur->x == prev->x && cur->y == prev->y && cur->map_n == prev->map_n)
    return 0.0f;

  uint32_t tile = coord_index(cur->map_n, cur->x, cur->y);
  if (!vbit_test_set(env->visited_coords, tile))
    return 0.0f;
  env->unique_coords_count++;

  int cell = env->exploration_cell_size;
  uint32_t cell_idx = coord_index(cur->map_n, cur->x / cell, cur->y / cell);
  return vbit_test_set(env->visited_cells, cell_idx) ? 1.0f : 0.0f;
}

static float signal_catching(Env *env) {
  if (env->cur.party_count == 0 || env->cur.pokedex_owned_count <= env->dex_owned_best)
    return 0.0f;
  env->dex_owned_best = env->cur.pokedex_owned_count;
  if (env->verbose)
    printf("Caught a new Pokemon! Pokedex owned: %d\n", env->cur.pokedex_owned_count);
  return 1.0f;
}

static float signal_seeing(Env *env) {
  if (env->cur.pokedex_seen_count <= env->dex_seen_best)
    return 0.0f;
  env->dex_seen_best = env->cur.pokedex_seen_count;
  if (env->verbose)
    printf("Saw a new Pokemon! Pokedex seen: %d\n", env->cur.pokedex_seen_count);
  return 1.0f;
}

static float signal_leveling(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  if (cur->party_count != prev->party_count)
    return 0.0f;

  int gained = 0;
  for (int i = 0; i < cur->party_count; i++) {
    if (prev->party[i].level == 0)
      continue;
    gained += (int)cur->party[i].level - (int)prev->party[i].level;
  }
  if (gained <= 0)
    return 0.0f;


  if (env->verbose) {
    int level_sum = party_level_sum(cur);
    printf("Leveled up! Party levels: %d -> %d\n", party_level_sum(prev), level_sum);
  }
  return (float)gained;
}

static float signal_battling(Env *env) {
  if (!(env->cur.step_events & PK_EV_BATTLE_WON))
    return 0.0f;
  if (env->verbose)
    printf("Won a battle!\n");
  return 1.0f;
}

static float signal_fleeing(Env *env) {
  if (!(env->cur.step_events & PK_EV_BATTLE_FLED))
    return 0.0f;
  if (env->flee_exempt_ghost && (env->cur.step_events & PK_EV_GHOST_BATTLE))
    return 0.0f;
  if (env->verbose)
    printf("Ran from a battle\n");
  return 1.0f;
}

static float signal_ghost_stall(Env *env) {
  return is_battle_active(&env->cur) && (env->cur.step_events & PK_EV_GHOST_BATTLE) ? 1.0f : 0.0f;
}

static float signal_events(Env *env) {
  const uint8_t *cur = env->cur.event_flags;
  if (!memcmp(cur, env->prev_events, PK_EVENT_FLAG_BYTES))
    return 0.0f;
  int fresh = 0;
  for (int b = 0; b < PK_EVENT_FLAG_BYTES; b++) {
    uint8_t rose = cur[b] & (uint8_t)~env->prev_events[b] & g_pk_events.mask[b];
    if (rose) {
      uint8_t pay = env->event_reward_once ? rose & (uint8_t)~env->paid_events[b] : rose;
      fresh += __builtin_popcount(pay);
      if (env->verbose)
        for (int k = 0; k < 8; k++)
          if (pay & (1u << k))
            printf("Event completed: %s\n", EVENT_LIST[g_pk_events.event_at[b * 8 + k]].name);
      env->paid_events[b] |= rose;
    }
    env->prev_events[b] = cur[b];
  }
  return (float)fresh;
}

static const uint8_t PK_KEY_REWARD_ITEMS[] = {PKRED_ITEM_SILPH_SCOPE, PKRED_ITEM_LIFT_KEY, PKRED_ITEM_CARD_KEY,
                                              PKRED_ITEM_SECRET_KEY, PKRED_ITEM_GOLD_TEETH};
#define PK_KEY_REWARD_ITEM_COUNT ((int)sizeof(PK_KEY_REWARD_ITEMS))
#define PK_KEY_PROGRESS_DRINK (1u << PK_KEY_REWARD_ITEM_COUNT)
#define PK_KEY_PROGRESS_GAVE_DRINK (1u << (PK_KEY_REWARD_ITEM_COUNT + 1))

static uint8_t key_progress_mask(const PkSnapshot *s) {
  uint8_t m = 0;
  for (int i = 0; i < PK_KEY_REWARD_ITEM_COUNT; i++)
    if (pk_bag_has(s, PK_KEY_REWARD_ITEMS[i]))
      m |= (uint8_t)(1u << i);
  if (pk_bag_has(s, PKRED_ITEM_FRESH_WATER) || pk_bag_has(s, PKRED_ITEM_SODA_POP) || pk_bag_has(s, PKRED_ITEM_LEMONADE))
    m |= PK_KEY_PROGRESS_DRINK;
  if (s->gave_guards_drink)
    m |= PK_KEY_PROGRESS_GAVE_DRINK;
  return m;
}

static float signal_key_items(Env *env) {
  uint8_t got = key_progress_mask(&env->cur) & (uint8_t)~env->key_progress;
  if (!got)
    return 0.0f;
  env->key_progress |= got;
  if (env->verbose)
    printf("Key progress (mask %d)\n", got);
  return (float)__builtin_popcount(got);
}

static float signal_badges(Env *env) {
  uint8_t got = env->cur.badges & (uint8_t)~env->prev.badges;
  if (got && env->verbose)
    printf("Got a badge (mask %d)\n", got);
  return (float)__builtin_popcount(got);
}

static int completed_event_count(const PkSnapshot *s) { return pk_completed_events(s->event_flags); }

static bool in_pokecenter(uint8_t map_n) {
  for (int i = 0; i < PKRED_POKECENTER_MAPS; i++)
    if (PKRED_POKECENTER_MAP_IDS[i] == map_n)
      return true;
  return false;
}

static float party_pp_left(const PkSnapshot *s) {
  int pp = 0, max = 0;
  for (int i = 0; i < s->party_count; i++)
    for (int k = 0; k < 4; k++) {
      int m = move_max_pp(s->party[i].moves[k], s->party[i].pp[k]);
      if (m) {
        pp += s->party[i].pp[k] & PKRED_PP_MASK;
        max += m;
      }
    }
  return max ? (float)pp / (float)max : 1.0f;
}

static bool party_needs_heal(const Env *env, const PkSnapshot *s) {
  return s->hp_fraction < env->healing_health_left || party_pp_left(s) < env->healing_pp_left;
}

static bool party_restored(const PkSnapshot *s) { return s->hp_fraction >= 1.0f && party_pp_left(s) >= 1.0f; }

static float signal_healing(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  if (is_battle_active(cur) || cur->party_count == 0 || cur->party_count != prev->party_count)
    return 0.0f;
  if (cur->map_n != prev->map_n || !in_pokecenter(cur->map_n))
    return 0.0f;
  if (!party_needs_heal(env, prev) || !party_restored(cur))
    return 0.0f;
  if (env->verbose)
    printf("Healed the party at a Pokecenter\n");
  return 1.0f;
}

static int hm_index(uint8_t move) {
  for (int h = 0; h < PK_NUM_HMS; h++)
    if (PK_HM_MOVES[h] == move)
      return h;
  return -1;
}

#define PK_TM_PENDING_STEPS 64

static int tm_index(uint8_t move) {
  for (int t = 0; t < PKRED_NUM_TMS; t++)
    if (PKRED_TM_MOVES[t] == move)
      return t;
  return -1;
}

static int tms_consumed(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  int taught = 0;
  for (int t = 0; t < PKRED_NUM_TMS && env->tm_pending; t++) {
    uint64_t bit = 1ull << t;
    uint8_t item = (uint8_t)(PKRED_ITEM_TM01 + t);
    if ((env->tm_pending & bit) && bag_item_count(cur, item) < bag_item_count(prev, item)) {
      env->tm_pending &= ~bit;
      taught++;
    }
  }
  if (env->tm_pending && ++env->tm_pending_steps > PK_TM_PENDING_STEPS)
    env->tm_pending = 0;
  return taught;
}

static int item_taught(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  int taught = 0;
  if (cur->party_count == prev->party_count) {
    for (int i = 0; i < cur->party_count; i++) {
      if (cur->party[i].species == 0 || cur->party[i].species != prev->party[i].species)
        continue;
      for (int k = 0; k < 4; k++) {
        uint8_t move = cur->party[i].moves[k];
        if (mon_knows(&prev->party[i], move))
          continue;
        int h = hm_index(move), t;
        if (h >= 0) {
          taught++;
        } else if ((t = tm_index(move)) >= 0) {
          env->tm_pending |= 1ull << t;
          env->tm_pending_steps = 0;
        }
      }
    }
  }
  int tms = tms_consumed(env);
  if (env->verbose) {
    if (taught > 0)
      printf("Taught an HM to a Pokemon\n");
    if (tms > 0)
      printf("Taught a TM to a Pokemon\n");
  }
  return taught + tms;
}

static uint8_t hm_uses(const PkSnapshot *cur, const PkSnapshot *prev) {
  if (is_battle_active(cur) || is_battle_active(prev))
    return 0;
  bool same_map = cur->map_n == prev->map_n;
  uint8_t used = 0;
  if (cur->cut_used && cur->cut_used != PKRED_CUT_TILE_GRASS)
    used |= PK_HM_USE_CUT;
  if (!prev->surfing && cur->surfing)
    used |= PK_HM_USE_SURF;
  if (!prev->strength_active && cur->strength_active)
    used |= PK_HM_USE_STRENGTH;
  if (same_map && prev->dark_cave && !cur->dark_cave)
    used |= PK_HM_USE_FLASH;
  if (!prev->used_fly && cur->used_fly)
    used |= PK_HM_USE_FLY;
  return used;
}

static float signal_hm(Env *env) {
  uint8_t used = hm_uses(&env->cur, &env->prev);
  for (int i = 0; i < 5; i++) {
    if (!(used & (1u << i)))
      continue;
    if (env->hm_use_cap >= 0 && env->hm_use_count[i] >= env->hm_use_cap)
      used &= (uint8_t)~(1u << i);
    else
      env->hm_use_count[i]++;
  }
  if (used && env->verbose)
    printf("Used an HM outside of battle (mask %d)\n", used);
  return (float)__builtin_popcount(used);
}

static uint8_t consumed_kinds(const PkSnapshot *cur, const PkSnapshot *prev) {
  uint8_t kinds = 0;
  for (int i = 0; i < prev->bag_count; i++) {
    if (bag_item_count(cur, prev->bag[i].item) >= prev->bag[i].count)
      continue;
    switch (item_kind(prev->bag[i].item)) {
    case PK_ITEM_HEAL: kinds |= 1u << PK_USE_HP; break;
    case PK_ITEM_FULL_RESTORE: kinds |= 1u << PK_USE_HP | 1u << PK_USE_CURE; break;
    case PK_ITEM_CURE: kinds |= 1u << PK_USE_CURE; break;
    case PK_ITEM_REVIVE: kinds |= 1u << PK_USE_REVIVE; break;
    case PK_ITEM_PP: kinds |= 1u << PK_USE_PP; break;
    case PK_ITEM_CANDY: kinds |= 1u << PK_USE_CANDY; break;
    case PK_ITEM_STONE: kinds |= 1u << PK_USE_STONE; break;
    default: break;
    }
  }
  return kinds;
}

static void use_effect(PkItemUse *u, int kind, float amount) {
  u->effect |= (uint8_t)(1u << kind);
  u->effect_age[kind] = 0;
  u->amount[kind] += amount;
}

static void detect_use_effects(PkItemUse *u, const PkSnapshot *cur, const PkSnapshot *prev) {
  for (int i = 0; i < cur->party_count; i++) {
    const PkMon *c = &cur->party[i], *p = &prev->party[i];
    if (!c->species || !p->species)
      continue;
    if (c->level > p->level) {
      use_effect(u, PK_USE_CANDY, (float)(c->level - p->level));
      continue;
    }
    if (c->species != p->species) {
      if (PKRED_SPECIES_STONES[p->species])
        use_effect(u, PK_USE_STONE, 1.0f);
      continue;
    }
    if (c->max_hp != p->max_hp || !c->max_hp)
      continue;
    if (!p->hp && c->hp)
      use_effect(u, PK_USE_REVIVE, 1.0f);
    else if (p->hp && c->hp > p->hp)
      use_effect(u, PK_USE_HP, (float)(c->hp - p->hp) / (float)c->max_hp);
    if (p->status && !c->status && c->hp)
      use_effect(u, PK_USE_CURE, 1.0f);
    int max_pp, pp_gain = move_pp_total(c, &max_pp) - move_pp_total(p, NULL);
    if (pp_gain > 0 && max_pp && !memcmp(c->moves, p->moves, sizeof(c->moves)))
      use_effect(u, PK_USE_PP, (float)pp_gain / (float)max_pp);
  }
}

static float item_consumables(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  PkItemUse *u = &env->item_use;
  if (!is_battle_active(cur) && !is_battle_active(prev) && cur->party_count == prev->party_count &&
      !in_pokecenter(cur->map_n)) {
    uint8_t kinds = consumed_kinds(cur, prev);
    for (int k = 0; k < PK_USE_KINDS; k++)
      if (kinds & (1u << k))
        u->consumed_age[k] = 0;
    u->consumed |= kinds;
    detect_use_effects(u, cur, prev);
  }
  float paid = 0.0f;
  for (int k = 0; k < PK_USE_KINDS; k++) {
    uint8_t bit = (uint8_t)(1u << k);
    if ((u->consumed & u->effect & bit)) {
      paid += u->amount[k];
      if (env->verbose)
        printf("Used an item (kind %d, %.2f)\n", k, u->amount[k]);
      u->effect &= (uint8_t)~bit;
      u->amount[k] = 0.0f;
    }
    if ((u->consumed & bit) && ++u->consumed_age[k] > PK_TM_PENDING_STEPS)
      u->consumed &= (uint8_t)~bit;
    if ((u->effect & bit) && ++u->effect_age[k] > PK_TM_PENDING_STEPS) {
      u->effect &= (uint8_t)~bit;
      u->amount[k] = 0.0f;
    }
  }
  return paid;
}

static int item_key_items(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  uint8_t used = 0;
  if (!prev->biking && cur->biking)
    used |= PK_KEY_USE_BICYCLE;
  if (is_battle_active(cur) && cur->enemy_mon.species == PKRED_SPECIES_SNORLAX)
    used |= cur->map_n == PKRED_MAP_ROUTE_12 ? PK_KEY_USE_FLUTE_ROUTE12
            : cur->map_n == PKRED_MAP_ROUTE_16 ? PK_KEY_USE_FLUTE_ROUTE16 : 0;
  used &= (uint8_t)~env->key_item_mask;
  env->key_item_mask |= used;
  if (used && env->verbose)
    printf("Used a key item (mask %d)\n", used);
  return __builtin_popcount(used);
}

static float signal_item_used(Env *env) {
  return (float)item_taught(env) + item_consumables(env) + (float)item_key_items(env);
}

static float signal_none(Env *env) {
  (void)env;
  return 0.0f;
}

static float calculate_rewards(Env *env) {
  float gained = 0.0f, lost = 0.0f;
#define PK_SIG_EVAL(name, fn, key, penalty, summed)                               \
  if (summed) {                                                                   \
    float v = env->weight[PK_SIG_##name] * fn(env);                               \
    env->totals.name += v;                                                        \
    if (penalty)                                                                  \
      lost += v;                                                                  \
    else                                                                          \
      gained += v;                                                                \
  }
  PK_REWARD_SIGNALS(PK_SIG_EVAL)
#undef PK_SIG_EVAL

  env->prev = env->cur;
  return gained + lost;
}

#endif
