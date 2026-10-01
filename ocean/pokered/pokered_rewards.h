#ifndef POKERED_REWARDS_H
#define POKERED_REWARDS_H

#include <string.h>

static int calc_level_sum(const PkSnapshot *core) {
  int sum = 0;
  for (int i = 0; i < 6; i++)
    sum += core->party[i].level;
  return sum;
}

static float EVENT_WEIGHTS[EVENT_COUNT];
static bool event_weights_ready = false;

#define EVENT_WEIGHT_TRAINER 0.5f
#define EVENT_WEIGHT_STORY   1.5f
#define EVENT_WEIGHT_BADGE   3.0f

static const char* BADGE_EVENT_NAMES[] = {
    "Beat Brock", "Beat Misty", "Beat Lt Surge", "Beat Erika", "Beat Koga",
    "Beat Sabrina", "Beat Blaine", "Beat Viridian Gym Giovanni",
    "Beat Champion Rival",
};
#define BADGE_EVENT_NAMES_COUNT (sizeof(BADGE_EVENT_NAMES) / sizeof(BADGE_EVENT_NAMES[0]))

static void init_event_weights(void) {
  for (size_t i = 0; i < EVENT_COUNT; ++i) {
    bool is_badge = false;
    for (size_t j = 0; j < BADGE_EVENT_NAMES_COUNT; ++j) {
      if (strcmp(EVENT_LIST[i].name, BADGE_EVENT_NAMES[j]) == 0) {
        is_badge = true;
        break;
      }
    }
    if (is_badge)
      EVENT_WEIGHTS[i] = EVENT_WEIGHT_BADGE;
    else
      EVENT_WEIGHTS[i] = strstr(EVENT_LIST[i].name, "Trainer") ? EVENT_WEIGHT_TRAINER : EVENT_WEIGHT_STORY;
  }
  event_weights_ready = true;
}

static float event_weighted_sum(const uint8_t *events) {
  if (!event_weights_ready)
    init_event_weights();
  float sum = 0.0f;
  for (size_t i = 0; i < EVENT_COUNT; ++i)
    if (events[i])
      sum += EVENT_WEIGHTS[i];
  return sum;
}

static float compute_exploration_signal(Env *env) {
  int action = env->prev_action;
  const PkSnapshot *core = &env->cur;
  const PkSnapshot *prev = &env->prev;

  if (!is_directional_action(action) || is_battle_active(core))
    return 0.0f;

  if (core->x == prev->x && core->y == prev->y && core->map_n == prev->map_n)
    return 0.0f;

  uint32_t idx = coord_index(core->map_n, core->x, core->y);
  if (idx >= VISITED_COORDS_SIZE)
    return 0.0f;

  bool new_tile = vbit_test_set(env->visited_coords, idx);
  if (new_tile) {
    env->unique_coords_count++;
    if (env->map_visited_counts[core->map_n] < UINT16_MAX)
      env->map_visited_counts[core->map_n]++;
  }
  if (!new_tile)
    return 0.0f;

  int cell = env->exploration_cell_size;
  uint32_t cell_idx = coord_index(core->map_n, core->x / cell, core->y / cell);
  if (!vbit_test_set(env->visited_cells, cell_idx))
    return 0.0f;
  return 1.0f;
}

static float compute_catching_signal(Env *env) {
  const PkSnapshot *core = &env->cur;
  const PkSnapshot *prev = &env->prev;
  int delta = (int)core->pokedex_owned_count - (int)prev->pokedex_owned_count;
  if (delta > 0) {
    if (env->verbose)
      printf("Caught a new Pokemon! Pokedex owned: %d\n", core->pokedex_owned_count);
    return 1.0f;
  }
  return 0.0f;
}

static float compute_seeing_signal(Env *env) {
  const PkSnapshot *core = &env->cur;
  const PkSnapshot *prev = &env->prev;
  int delta = (int)core->pokedex_seen_count - (int)prev->pokedex_seen_count;
  if (delta > 0) {
    if (env->verbose)
      printf("Saw a new Pokemon! Pokedex seen: %d\n", core->pokedex_seen_count);
    return 1.0f;
  }
  return 0.0f;
}

static float compute_events_signal(Env *env) {
  if (!event_weights_ready)
    init_event_weights();
  float sum = 0.0f;
  for (size_t i = 0; i < EVENT_COUNT; ++i) {
    uint8_t completed = env->cur.events[i];
    if (completed) {
      if (!env->prev_events[i]) {
        if (env->verbose)
          printf("Event completed: %s\n", EVENT_LIST[i].name);
        if (EVENT_WEIGHTS[i] != EVENT_WEIGHT_TRAINER && env->be->milestone_event)
          env->be->milestone_event(env->impl, (int)i);
      }
      sum += EVENT_WEIGHTS[i];
    }
    env->prev_events[i] = completed;
  }
  float delta = sum - env->prev_event_sum;
  env->prev_event_sum = sum;
  return delta > 0.0f ? delta : 0.0f;
}

static const uint8_t POKECENTER_MAPS[] = {
    0x29,
    0x3A,
    0x40,
    0x44,
    0x51,
    0x59,
    0x85,
    0x8D,
    0x9A,
    0xAB,
    0xB6,
};
#define POKECENTER_MAP_COUNT (sizeof(POKECENTER_MAPS) / sizeof(POKECENTER_MAPS[0]))

static bool POKECENTER_MAP_SET[256];
static bool pokecenter_map_set_ready = false;

static inline bool is_pokecenter_map(uint8_t map_n) {
  if (!pokecenter_map_set_ready) {
    for (size_t i = 0; i < POKECENTER_MAP_COUNT; i++)
      POKECENTER_MAP_SET[POKECENTER_MAPS[i]] = true;
    pokecenter_map_set_ready = true;
  }
  return POKECENTER_MAP_SET[map_n];
}

static float compute_healing_signal(Env *env) {
  const PkSnapshot *core = &env->cur;
  const PkSnapshot *prev = &env->prev;
  if (core->party_count != prev->party_count || core->party_count == 0)
    return 0.0f;
  if (env->party_wiped)
    return 0.0f;
  if (calc_level_sum(core) != calc_level_sum(prev))
    return 0.0f;
  if (is_pokecenter_map(core->map_n) && core->hp_fraction >= 0.999f)
    return 0.0f;

  float delta = core->hp_fraction - prev->hp_fraction;
  if (delta > 0.0f) {
    if (env->verbose)
      printf("Healed! Party HP fraction: %.3f -> %.3f\n", prev->hp_fraction, core->hp_fraction);
    return 1.0f;
  }
  return 0.0f;
}

static float compute_pokecenter_heal_signal(Env *env) {
  const PkSnapshot *core = &env->cur;
  const PkSnapshot *prev = &env->prev;
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
  const PkSnapshot *core = &env->cur;
  const PkSnapshot *prev = &env->prev;

  if (!is_pokecenter_map(core->map_n)) {
    env->pokecenter_visit_step = -1;
    return 0.0f;
  }

  if (core->map_n != prev->map_n || env->pokecenter_visit_step < 0)
    env->pokecenter_visit_step = 0;
  else
    env->pokecenter_visit_step++;

  if (env->pokecenter_visit_step >= env->pokecenter_visit_steps)
    return 0.0f;
  if (core->party_count == 0 || core->hp_fraction >= 0.999f)
    return 0.0f;

  if (env->verbose)
    printf("Visited a Pokemon Center while hurt (HP fraction %.3f, step %d)!\n",
           core->hp_fraction, env->pokecenter_visit_step);
  return 1.0f;
}

static float compute_leveling_signal(Env *env) {
  const PkSnapshot *core = &env->cur;
  const PkSnapshot *prev = &env->prev;
  if (core->party_count != prev->party_count)
    return 0.0f;

  int delta = 0;
  for (int i = 0; i < core->party_count; i++) {
    if (prev->party[i].level == 0)
      continue;
    delta += (int)core->party[i].level - (int)prev->party[i].level;
  }

  if (delta > 0) {
    int level_sum = calc_level_sum(core);
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

#define HM_REWARDED_MASK_ALL 0x1F

static float compute_hm_learned_signal(Env *env) {
  if (env->hm_rewarded_mask == HM_REWARDED_MASK_ALL)
    return 0.0f;

  const PkSnapshot *core = &env->cur;
  bool learned_any = false;
  for (int h = 0; h < 5; h++) {
    if (env->hm_rewarded_mask & (1 << h))
      continue;
    for (int i = 0; i < core->party_count && i < 6; i++) {
      bool has_move = false;
      for (int m = 0; m < 4; m++) {
        if (core->party[i].moves[m] == HM_MOVE_IDS[h]) { has_move = true; break; }
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

static int ball_count(const PkSnapshot *s) {
  static const uint8_t BALL_IDS[] = {1, 2, 3, 4, 8};
  int n = 0;
  for (size_t i = 0; i < sizeof(BALL_IDS); i++)
    n += s->bag_qty[BALL_IDS[i]];
  return n;
}

static float compute_run_signal(Env *env) {
  const PkSnapshot *core = &env->cur;
  const PkSnapshot *prev = &env->prev;
  if (prev->in_battle != 1 || core->in_battle != 0)
    return 0.0f;
  if (prev->enemy_mon.hp == 0 || core->hp_fraction <= 0.0f)
    return 0.0f;
  if (core->party_count != prev->party_count || ball_count(core) < ball_count(prev))
    return 0.0f;
  if (env->verbose)
    printf("Ran from a wild battle!\n");
  return 1.0f;
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
  env->be->snapshot(env->impl, &env->cur);
  if (env->be->milestone_map)
    env->be->milestone_map(env->impl, env->cur.map_n, env->prev.map_n);

  float weight_exploration = env->weight_exploration * compute_exploration_anneal_scale(env);
  if (env->exploration_death_scaling_enabled)
    weight_exploration /= (float)(env->blackout_count + 1);

  float s_explore = compute_exploration_signal(env);
  float s_catching = compute_catching_signal(env);
  float s_seeing = compute_seeing_signal(env);
  float s_events = compute_events_signal(env);
  float s_leveling = compute_leveling_signal(env);
  float s_healing = compute_healing_signal(env);
  float s_hm_learned = compute_hm_learned_signal(env);
  float s_pokecenter = compute_pokecenter_heal_signal(env);
  float s_pokecenter_visit = compute_pokecenter_visit_signal(env);
  float s_run = compute_run_signal(env);

  env->stats.total_explore_signal += s_explore * weight_exploration;
  env->stats.total_catching_signal += s_catching * env->weight_catching;
  env->stats.total_seeing_signal += s_seeing * env->weight_seeing;
  env->stats.total_events_signal += s_events * env->weight_events;
  env->stats.total_leveling_signal += s_leveling * env->weight_leveling;
  env->stats.total_healing_signal += s_healing * env->weight_healing;
  env->stats.total_hm_learned_signal += s_hm_learned * env->weight_hm_learned;
  env->stats.total_pokecenter_signal += s_pokecenter * env->weight_pokecenter;
  env->stats.total_pokecenter_visit_signal += s_pokecenter_visit * env->weight_pokecenter_visit;
  env->stats.total_run_signal -= s_run * env->weight_run;

  env->prev = env->cur;

  return weight_exploration * s_explore + env->weight_catching * s_catching +
         env->weight_seeing * s_seeing + env->weight_events * s_events +
         env->weight_leveling * s_leveling + env->weight_healing * s_healing +
         env->weight_hm_learned * s_hm_learned + env->weight_pokecenter * s_pokecenter +
         env->weight_pokecenter_visit * s_pokecenter_visit -
         env->weight_run * s_run -
         env->weight_time;
}

#endif
