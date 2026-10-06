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
  int fresh = 0;
  for (int i = 0; i < EVENT_COUNT; i++) {
    uint8_t done = env->cur.events[i];
    if (done && !env->prev_events[i]) {
      fresh++;
      if (env->verbose)
        printf("Event completed: %s\n", EVENT_LIST[i].name);
    }
    env->prev_events[i] = done;
  }
  return (float)fresh;
}

static int completed_event_count(const PkSnapshot *s) {
  int n = 0;
  for (int i = 0; i < EVENT_COUNT; i++)
    n += s->events[i] != 0;
  return n;
}

static bool in_pokecenter(uint8_t map_n) {
  for (int i = 0; i < PKRED_POKECENTER_MAPS; i++)
    if (PKRED_POKECENTER_MAP_IDS[i] == map_n)
      return true;
  return false;
}

static int empty_move_count(const PkMon *m) {
  int empty = 0;
  for (int k = 0; k < 4; k++)
    empty += m->moves[k] && !(m->pp[k] & PKRED_PP_MASK);
  return empty;
}

static bool mon_needs_heal(const PkMon *m) {
  return m->max_hp > 0 && (m->hp == 0 || empty_move_count(m) >= 2);
}

static bool party_needs_heal(const PkSnapshot *s) {
  for (int i = 0; i < s->party_count; i++)
    if (mon_needs_heal(&s->party[i]))
      return true;
  return false;
}

static bool party_restored(const PkSnapshot *cur, const PkSnapshot *prev) {
  for (int i = 0; i < cur->party_count; i++) {
    if (cur->party[i].hp != cur->party[i].max_hp)
      return false;
    for (int k = 0; k < 4; k++)
      if (prev->party[i].moves[k] && !(prev->party[i].pp[k] & PKRED_PP_MASK) &&
          !(cur->party[i].pp[k] & PKRED_PP_MASK))
        return false;
  }
  return true;
}

static float signal_healing(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  if (is_battle_active(cur) || cur->party_count == 0 || cur->party_count != prev->party_count)
    return 0.0f;
  if (cur->map_n != prev->map_n || !in_pokecenter(cur->map_n))
    return 0.0f;
  if (!party_needs_heal(prev) || !party_restored(cur, prev))
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

static float calculate_rewards(Env *env) {
  env->be->snapshot(env->impl, &env->cur);
  if (pkms_active(env))
    pkms_check(env);

  float w_explore = env->weight_exploration;
  if (env->exploration_death_scaling_enabled)
    w_explore /= (float)(env->blackout_count + 1);

  float explore = w_explore * signal_exploration(env);
  float catching = env->weight_catching * signal_catching(env);
  float seeing = env->weight_seeing * signal_seeing(env);
  float leveling = env->weight_leveling * signal_leveling(env);
  float events = env->weight_events * signal_events(env);
  float battling = env->weight_battling * signal_battling(env);
  float fleeing = env->weight_fleeing * signal_fleeing(env);

  float healing = env->weight_healing * signal_healing(env);
  float hm_taught = env->weight_hm_taught * signal_hm_taught(env);
  float hm_used = env->weight_hm_used * signal_hm_used(env);

  env->totals.healing += healing;
  env->totals.hm_taught += hm_taught;
  env->totals.hm_used += hm_used;
  env->totals.explore += explore;
  env->totals.catching += catching;
  env->totals.seeing += seeing;
  env->totals.leveling += leveling;
  env->totals.events += events;
  env->totals.battling += battling;
  env->totals.fleeing += fleeing;

  env->prev = env->cur;
  return explore + catching + seeing + leveling + events + battling + healing + hm_taught + hm_used - fleeing;
}

#endif
