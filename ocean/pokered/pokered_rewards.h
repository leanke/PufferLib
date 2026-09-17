#ifndef POKERED_REWARDS_H
#define POKERED_REWARDS_H

#include <string.h>
#include "includes/milestones.h"


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

static float calc_event_weighted_sum(Emulator *emu, uint8_t *prev_events, bool verbose,
                                      bool capture_milestones, bool *milestone_captured) {
  if (!event_weights_ready)
    init_event_weights();

  float sum = 0.0f;
  for (size_t i = 0; i < EVENT_COUNT; ++i) {
    uint8_t value = read_mem(emu, EVENT_LIST[i].address);
    uint8_t completed = (value >> EVENT_LIST[i].bit) & 1;
    if (completed) {
      if (prev_events && !prev_events[i]) {
        if (verbose)
          printf("Event completed: %s\n", EVENT_LIST[i].name);
        bool already_tried = milestone_captured && milestone_captured[i];
        if (capture_milestones && EVENT_WEIGHTS[i] != 0.5f && g_milestone_pool.state_size > 0 &&
            !already_tried) {
          uint8_t *snapshot = (uint8_t*)malloc(g_milestone_pool.state_size);
          gambatte_save_state_raw(emu->gb, snapshot);
          milestone_pool_try_capture((int)i, snapshot);
          free(snapshot);
          if (milestone_captured)
            milestone_captured[i] = true;
        }
      }
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

  bool new_tile = !env->visited_coords[idx];
  if (new_tile) {
    env->visited_coords[idx] = 1;
    env->unique_coords_count++;
    if (env->map_visited_counts[core->map_n] < UINT16_MAX)
      env->map_visited_counts[core->map_n]++;
  }
  if (!new_tile)
    return 0.0f;

  int cell = env->exploration_cell_size;
  uint32_t cell_idx = coord_index(core->map_n, core->x / cell, core->y / cell);
  if (env->visited_cells[cell_idx])
    return 0.0f;
  env->visited_cells[cell_idx] = 1;
  return 1.0f;
}

static float compute_catching_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  int delta = (int)core->pokedex_owned_count - (int)prev->pokedex_owned_count;
  if (delta > 0) {
    if (env->verbose)
      printf("Caught a new Pokemon! Pokedex owned: %d\n", core->pokedex_owned_count);
    return 1.0f;
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
    return 1.0f;
  }
  return 0.0f;
}

static float compute_events_signal(Env *env) {
  bool capture_milestones = env->milestones_enabled && env->event_milestones_enabled;
  float event_sum = calc_event_weighted_sum(&env->emu, env->prev_events,
                                            env->verbose, capture_milestones,
                                            env->milestone_captured);
  bool triggered = event_sum > env->prev_event_sum;
  env->prev_event_sum = event_sum;
  return triggered ? 1.0f : 0.0f;
}

static float compute_healing_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  if (core->party_count != prev->party_count || core->party_count == 0)
    return 0.0f;
  if (env->party_wiped)
    return 0.0f;
  if (calc_level_sum(core) != calc_level_sum(prev))
    return 0.0f;

  float delta = core->hp_fraction - prev->hp_fraction;
  if (delta > 0.0f) {
    if (env->verbose)
      printf("Healed! Party HP fraction: %.3f -> %.3f\n", prev->hp_fraction, core->hp_fraction);
    return 1.0f;
  }
  return 0.0f;
}

static const uint8_t POKECENTER_MAPS[] = {
    0x29, // Viridian Pokecenter
    0x3A, // Pewter Pokecenter
    0x40, // Cerulean Pokecenter
    0x44, // Mt Moon Pokecenter
    0x51, // Rock Tunnel Pokecenter
    0x59, // Vermilion Pokecenter
    0x85, // Celadon Pokecenter
    0x8D, // Lavender Pokecenter
    0x9A, // Fuchsia Pokecenter
    0xAB, // Cinnabar Pokecenter
    0xB6, // Saffron Pokecenter
};
#define POKECENTER_MAP_COUNT (sizeof(POKECENTER_MAPS) / sizeof(POKECENTER_MAPS[0]))

static bool is_pokecenter_map(uint8_t map_n) {
  for (size_t i = 0; i < POKECENTER_MAP_COUNT; i++)
    if (POKECENTER_MAPS[i] == map_n)
      return true;
  return false;
}

static float compute_pokecenter_heal_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  if (core->party_count != prev->party_count || core->party_count == 0)
    return 0.0f;
  if (env->party_wiped)
    return 0.0f;
  if (!is_pokecenter_map(core->map_n))
    return 0.0f;
  if (prev->hp_fraction >= 0.999f || core->hp_fraction < 0.999f)
    return 0.0f;

  if (env->verbose)
    printf("Healed at the Pokemon Center!\n");
  return 1.0f;
}

static float compute_pokecenter_visit_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  if (core->map_n == prev->map_n)
    return 0.0f;
  if (!is_pokecenter_map(core->map_n))
    return 0.0f;
  if (core->party_count == 0 || core->hp_fraction >= 0.999f)
    return 0.0f;

  if (env->verbose)
    printf("Visited a Pokemon Center while hurt (HP fraction %.3f)!\n", core->hp_fraction);
  return 1.0f;
}

static float compute_leveling_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  if (core->party_count != prev->party_count)
    return 0.0f;

  int delta = 0;
  for (int i = 0; i < core->party_count; i++)
    delta += (int)core->levels[i] - (int)prev->levels[i];

  if (delta > 0) {
    int level_sum = calc_level_sum(&env->gstate.core);
    if (env->verbose)
      printf("Leveled up! Party levels: %d -> %d\n", calc_level_sum(prev), level_sum);
    if (level_sum < 15)
      return (float)delta;
  return ((float)(delta)) / 4.0f;

}
  return 0.0f;
}

static const uint8_t HM_MOVE_IDS[5] = {
    PKRED_MOVE_CUT, PKRED_MOVE_FLY, PKRED_MOVE_SURF, PKRED_MOVE_STRENGTH, PKRED_MOVE_FLASH,
};

static float compute_hm_learned_signal(Env *env) {
  CoreState *core = &env->gstate.core;
  bool learned_any = false;
  for (int h = 0; h < 5; h++) {
    if (env->hm_rewarded_mask & (1 << h))
      continue;
    for (int i = 0; i < core->party_count && i < 6; i++) {
      bool has_move = false;
      for (int m = 0; m < 4; m++) {
        if (core->moves[i][m] == HM_MOVE_IDS[h]) { has_move = true; break; }
      }
      if (has_move) {
        env->hm_rewarded_mask |= (1 << h);
        learned_any = true;
        if (env->verbose)
          printf("Taught HM move id %d to party slot %d!\n", HM_MOVE_IDS[h], i);
        break;
      }
    }
  }
  return learned_any ? 1.0f : 0.0f;
}

static void capture_map_milestones(Env *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  if (!env->milestones_enabled || core->map_n == prev->map_n || g_milestone_pool.state_size == 0)
    return;

  for (size_t i = 0; i < MAP_MILESTONE_COUNT; i++) {
    if (core->map_n != MAP_MILESTONES[i].map_id)
      continue;
    if (MAP_MILESTONES[i].is_town && !env->town_milestones_enabled)
      continue;
    int slot = MAP_MILESTONE_SLOT_BASE + (int)i;
    if (env->milestone_captured && env->milestone_captured[slot])
      continue;
    if (env->verbose)
      printf("Milestone reached: %s\n", MAP_MILESTONES[i].name);
    uint8_t *snapshot = (uint8_t*)malloc(g_milestone_pool.state_size);
    gambatte_save_state_raw(env->emu.gb, snapshot);
    milestone_pool_try_capture(slot, snapshot);
    free(snapshot);
    if (env->milestone_captured)
      env->milestone_captured[slot] = true;
  }
}

static float compute_exploration_anneal_scale(Env *env) {
  long start = env->weight_exploration_anneal_start;
  long end = env->weight_exploration_anneal_end;
  if (end <= start)
    return 1.0f;
  float frac = (float)(env->total_agent_steps - start) / (float)(end - start);
  return 1.0f - fminf(fmaxf(frac, 0.0f), 1.0f);
}



static float calculate_rewards(Env *env) {
  PREFETCH_READ(&env->gstate);
  PREFETCH_READ(env->visited_coords);

  update_core_state(env);
  update_battle_state(&env->gstate.battle, &env->emu);
  capture_map_milestones(env);

  float weight_exploration = env->weight_exploration * compute_exploration_anneal_scale(env);

  float s_explore = compute_exploration_signal(env);
  float s_catching = compute_catching_signal(env);
  float s_seeing = compute_seeing_signal(env);
  float s_events = compute_events_signal(env);
  float s_leveling = compute_leveling_signal(env);
  float s_healing = compute_healing_signal(env);
  float s_hm_learned = compute_hm_learned_signal(env);
  float s_pokecenter = compute_pokecenter_heal_signal(env);
  float s_pokecenter_visit = compute_pokecenter_visit_signal(env);

  env->stats.total_explore_signal += s_explore * weight_exploration;
  env->stats.total_catching_signal += s_catching * env->weight_catching;
  env->stats.total_seeing_signal += s_seeing * env->weight_seeing;
  env->stats.total_events_signal += s_events * env->weight_events;
  env->stats.total_leveling_signal += s_leveling * env->weight_leveling;
  env->stats.total_healing_signal += s_healing * env->weight_healing;
  env->stats.total_hm_learned_signal += s_hm_learned * env->weight_hm_learned;
  env->stats.total_pokecenter_signal += s_pokecenter * env->weight_pokecenter;
  env->stats.total_pokecenter_visit_signal += s_pokecenter_visit * env->weight_pokecenter_visit;

  env->gstate.prev_core = env->gstate.core;

  return weight_exploration * s_explore + env->weight_catching * s_catching +
         env->weight_seeing * s_seeing + env->weight_events * s_events +
         env->weight_leveling * s_leveling + env->weight_healing * s_healing +
         env->weight_hm_learned * s_hm_learned + env->weight_pokecenter * s_pokecenter +
         env->weight_pokecenter_visit * s_pokecenter_visit -
         env->weight_time;
}

#endif /* POKERED_REWARDS_H */
