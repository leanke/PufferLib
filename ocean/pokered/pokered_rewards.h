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
      if ((env->be->caps & PK_CAP_MILESTONES) && !strstr(EVENT_LIST[i].name, "Trainer"))
        env->be->milestone_event(env->impl, i);
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

static float calculate_rewards(Env *env) {
  env->be->snapshot(env->impl, &env->cur);
  if (env->be->caps & PK_CAP_MILESTONES)
    env->be->milestone_map(env->impl, env->cur.map_n, env->prev.map_n);

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

  env->totals.explore += explore;
  env->totals.catching += catching;
  env->totals.seeing += seeing;
  env->totals.leveling += leveling;
  env->totals.events += events;
  env->totals.battling += battling;
  env->totals.fleeing += fleeing;

  env->prev = env->cur;
  return explore + catching + seeing + leveling + events + battling - fleeing;
}

#endif
