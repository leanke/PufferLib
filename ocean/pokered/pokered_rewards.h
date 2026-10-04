#ifndef POKERED_REWARDS_H
#define POKERED_REWARDS_H

// Reward = sum of seven weighted signals (weights live in config/pokered.ini):
//   exploration  first visit to a new exploration cell (overworld movement only)
//   catching     Pokedex "owned" count went up
//   seeing       Pokedex "seen" count went up
//   leveling     party levels went up
//   events       each newly completed story/trainer event (flat, 1 per event)
//   battling     a battle was won (enemy fainted, party still standing)
//   death        party wiped out (applied by the step loop, see puf_step_body)
//   fleeing      a battle was escaped by running (a penalty, off when weight_fleeing = 0)
// Each signal_*() returns an unscaled amount; calculate_rewards() applies the
// weight and records the weighted total in env->totals for logging.

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

// Levels gained across the party this step. Early levels (party sum < 15) are
// worth 4x later ones.
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

// 1 on the step a battle ends with the enemy fainted and our party alive.
// Running away, catching (enemy still has HP) and whiting out all score 0.
// Redcore counts wins itself (its last turn and the battle's end resolve in one
// step, so the last in-battle snapshot still shows the enemy alive); on the
// emulator the enemy is seen at 0 HP for several steps before the battle ends.
static float signal_battling(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  bool won = cur->battles_won != prev->battles_won;
  if (!won && is_battle_active(prev) && !is_battle_active(cur))
    won = prev->enemy_mon.hp == 0 && cur->hp_fraction > 0.0f;
  if (!won)
    return 0.0f;
  if (env->verbose)
    printf("Won a battle!\n");
  return 1.0f;
}

// 1 on the step a battle ends because the player ran away. Redcore counts escapes
// itself; on the emulator wEscapedFromBattle is set while the "Got away safely!" text
// is still up, so it is latched during the battle and read when the battle ends.
static float signal_fleeing(Env *env) {
  const PkSnapshot *cur = &env->cur, *prev = &env->prev;
  if (is_battle_active(cur) && cur->escaped)
    env->escape_latched = true;
  bool fled = cur->battles_fled != prev->battles_fled;
  if (is_battle_active(prev) && !is_battle_active(cur)) {
    fled = fled || env->escape_latched;
    env->escape_latched = false;
  } else if (!is_battle_active(cur)) {
    env->escape_latched = false;
  }
  if (!fled)
    return 0.0f;
  if (env->verbose)
    printf("Ran from a battle\n");
  return 1.0f;
}

// Counts events completed this step (each pays the same) and hands the
// non-trainer ones to the backend so it can capture milestone save states.
static float signal_events(Env *env) {
  int fresh = 0;
  for (int i = 0; i < EVENT_COUNT; i++) {
    uint8_t done = env->cur.events[i];
    if (done && !env->prev_events[i]) {
      fresh++;
      if (env->verbose)
        printf("Event completed: %s\n", EVENT_LIST[i].name);
      if (env->be->milestone_event && !strstr(EVENT_LIST[i].name, "Trainer"))
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

// Refreshes env->cur from the backend, scores the transition prev -> cur and
// advances prev.
static float calculate_rewards(Env *env) {
  env->be->snapshot(env->impl, &env->cur);
  if (env->be->milestone_map)
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
