#ifndef POKERED_REWARDS_H
#define POKERED_REWARDS_H

#include <string.h>

static int party_level_sum(const PkSnapshot *s) {
  int sum = 0;
  for (int i = 0; i < PARTY_SIZE; i++)
    sum += s->party[i].level;
  return sum;
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
  if (env->map_visited_counts[cur->map_n] < UINT16_MAX)
    env->map_visited_counts[cur->map_n]++;

  int cell = env->exploration_cell_size;
  uint32_t cell_idx = coord_index(cur->map_n, cur->x / cell, cur->y / cell);
  return vbit_test_set(env->visited_cells, cell_idx) ? 1.0f : 0.0f;
}

static float signal_catching(Env *env) {
  if (env->cur.pokedex_owned_count <= env->prev.pokedex_owned_count)
    return 0.0f;
  if (env->verbose)
    printf("Caught a new Pokemon! Pokedex owned: %d\n", env->cur.pokedex_owned_count);
  return 1.0f;
}

static float signal_seeing(Env *env) {
  if (env->cur.pokedex_seen_count <= env->prev.pokedex_seen_count)
    return 0.0f;
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

  int level_sum = party_level_sum(cur);
  if (env->verbose)
    printf("Leveled up! Party levels: %d -> %d\n", party_level_sum(prev), level_sum);
  return level_sum < 15 ? (float)gained : (float)gained / 4.0f;
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
  if (env->verbose)
    printf("Ran from a battle\n");
  return 1.0f;
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

static int completed_event_count(const PkSnapshot *s) { return pk_completed_events(s->event_flags); }

static bool in_pokecenter(uint8_t map_n) {
  for (int i = 0; i < PKRED_POKECENTER_MAPS; i++)
    if (PKRED_POKECENTER_MAP_IDS[i] == map_n)
      return true;
  return false;
}

static int move_max_pp(uint8_t move, uint8_t pp) {
  if (move == 0 || move > PKRED_NUM_MOVES)
    return 0;
  int base = PKRED_MOVE_BASE_PP[move];
  int bonus = base / 5 < 7 ? base / 5 : 7;
  return base + (pp >> 6) * bonus;
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

static bool is_hm_move(uint8_t move) {
  return move == PKRED_MOVE_CUT || move == PKRED_MOVE_FLY || move == PKRED_MOVE_SURF ||
         move == PKRED_MOVE_STRENGTH || move == PKRED_MOVE_FLASH;
}

static bool mon_has_move(const PkMon *m, uint8_t move) {
  for (int k = 0; k < 4; k++)
    if (m->moves[k] == move)
      return true;
  return false;
}

static bool party_has_move(const PkSnapshot *s, uint8_t move) {
  for (int i = 0; i < s->party_count; i++)
    if (mon_has_move(&s->party[i], move))
      return true;
  return false;
}

static float signal_hm_taught(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  if (cur->party_count != prev->party_count)
    return 0.0f;

  int taught = 0;
  for (int i = 0; i < cur->party_count; i++) {
    if (cur->party[i].species == 0 || cur->party[i].species != prev->party[i].species)
      continue;
    for (int k = 0; k < 4; k++) {
      uint8_t move = cur->party[i].moves[k];
      if (is_hm_move(move) && !mon_has_move(&prev->party[i], move))
        taught++;
    }
  }
  if (taught > 0 && env->verbose)
    printf("Taught an HM to a Pokemon\n");
  return (float)taught;
}

static uint8_t hm_uses(const PkSnapshot *cur, const PkSnapshot *prev) {
  if (is_battle_active(cur) || is_battle_active(prev))
    return 0;
  bool same_map = cur->map_n == prev->map_n;
  uint8_t used = 0;
  if (cur->cut_used)
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

static float signal_hm_used(Env *env) {
  uint8_t used = hm_uses(&env->cur, &env->prev);
  if (env->hm_used_once)
    used &= (uint8_t)~env->hm_used_mask;
  env->hm_used_mask |= used;
  if (used && env->verbose)
    printf("Used an HM outside of battle (mask %d)\n", used);
  return (float)__builtin_popcount(used);
}

static float signal_none(Env *env) {
  (void)env;
  return 0.0f;
}

static float calculate_rewards(Env *env) {
  float gained = 0.0f, lost = 0.0f;
#define PK_SIG_EVAL(name, fn, key, penalty, summed)                               \
  if (summed) {                                                                   \
    float w = env->weight[PK_SIG_##name];                                         \
    if (PK_SIG_##name == PK_SIG_explore && env->exploration_death_scaling_enabled) \
      w /= (float)(env->episode.blackouts + 1);                                   \
    float v = w * fn(env);                                                        \
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
