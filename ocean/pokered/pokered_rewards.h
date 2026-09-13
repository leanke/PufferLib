#ifndef POKERED_REWARDS_H
#define POKERED_REWARDS_H

#include <string.h>

static int calc_level_sum(CoreState *core) {
  int sum = 0;
  for (int i = 0; i < 6; i++)
    sum += core->levels[i];
  return sum;
}

static float EVENT_WEIGHTS[EVENT_COUNT];
static bool event_weights_ready = false;

static void init_event_weights(void) {
  for (size_t i = 0; i < EVENT_COUNT; ++i) {
    EVENT_WEIGHTS[i] = strstr(EVENT_LIST[i].name, "Trainer") ? 0.5f : 1.5f;
  }
  event_weights_ready = true;
}

static float calc_event_weighted_sum(Emulator *emu, uint8_t *prev_events, bool verbose) {
  if (!event_weights_ready)
    init_event_weights();

  float sum = 0.0f;
  for (size_t i = 0; i < EVENT_COUNT; ++i) {
    uint8_t value = read_mem(emu, EVENT_LIST[i].address);
    uint8_t completed = (value >> EVENT_LIST[i].bit) & 1;
    if (completed) {
      if (prev_events && !prev_events[i] && verbose)
        printf("Event completed: %s\n", EVENT_LIST[i].name);
      sum += EVENT_WEIGHTS[i];
    }
    if (prev_events)
      prev_events[i] = completed;
  }
  return sum;
}

static float compute_exploration_signal(Env *env) {
  int action = env->prev_action;
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;

  if (!is_directional_action(action) || is_battle_active(&env->gstate.battle))
    return 0.0f;

  if (core->x == prev->x && core->y == prev->y && core->map_n == prev->map_n)
    return 0.0f;

  uint32_t idx = core->idx;
  if (idx >= VISITED_COORDS_SIZE)
    return 0.0f;

  if (env->visited_coords[idx])
    return 0.0f;

  env->visited_coords[idx] = 1;
  env->unique_coords_count++;
  return 1.0f;
}

static float compute_catching_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  int delta = (int)core->pokedex_owned_count - (int)prev->pokedex_owned_count;
  if (delta > 0) {
    if (env->verbose)
      printf("Caught a new Pokemon! Pokedex owned: %d\n", core->pokedex_owned_count);
    return (float)delta;
  }
  return 0.0f;
}

static float compute_seeing_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  int delta = (int)core->pokedex_seen_count - (int)prev->pokedex_seen_count;
  if (delta > 0) {
    if (env->verbose)
      printf("Saw a new Pokemon! Pokedex seen: %d\n", core->pokedex_seen_count);
    return (float)delta;
  }
  return 0.0f;
}

static float compute_events_signal(Env *env) {
  float event_sum = calc_event_weighted_sum(&env->emu, env->prev_events,
                                            env->verbose);
  float signal = (event_sum > env->prev_event_sum)
      ? (event_sum - env->prev_event_sum)
      : 0.0f;
  env->prev_event_sum = event_sum;
  return signal;
}

static float compute_healing_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  if (core->party_count != prev->party_count || core->party_count == 0)
    return 0.0f;

  float delta = core->hp_fraction - prev->hp_fraction;
  if (delta > 0.0f) {
    if (env->verbose)
      printf("Healed! Party HP fraction: %.3f -> %.3f\n", prev->hp_fraction, core->hp_fraction);
    return delta;
  }
  return 0.0f;
}

static float compute_leveling_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  int level_sum = calc_level_sum(core);
  int prev_level_sum = calc_level_sum(prev);
  int delta = level_sum - prev_level_sum;
  if (delta > 0 && core->party_count == prev->party_count) {
    if (env->verbose)
      printf("You have leveled up! New level sum: %d\n", level_sum);
    return (float)delta * 0.1f;
  }
  return 0.0f;
}

static float calculate_rewards(Env *env) {
  PREFETCH_READ(&env->gstate);
  PREFETCH_READ(env->visited_coords);

  update_core_state(env);
  update_battle_state(&env->gstate.battle, &env->emu);

  float s_explore = compute_exploration_signal(env);
  float s_catching = compute_catching_signal(env);
  float s_seeing = compute_seeing_signal(env);
  float s_events = compute_events_signal(env);
  float s_leveling = compute_leveling_signal(env);
  float s_healing = compute_healing_signal(env);

  env->stats.total_explore_signal += s_explore * env->weight_exploration;
  env->stats.total_catching_signal += s_catching * env->weight_catching;
  env->stats.total_seeing_signal += s_seeing * env->weight_seeing;
  env->stats.total_events_signal += s_events * env->weight_events;
  env->stats.total_leveling_signal += s_leveling * env->weight_leveling;
  env->stats.total_healing_signal += s_healing * env->weight_healing;

  env->gstate.prev_core = env->gstate.core;

  return env->weight_exploration * s_explore + env->weight_catching * s_catching +
         env->weight_seeing * s_seeing + env->weight_events * s_events +
         env->weight_leveling * s_leveling + env->weight_healing * s_healing -
         env->weight_time;
}

#endif /* POKERED_REWARDS_H */
