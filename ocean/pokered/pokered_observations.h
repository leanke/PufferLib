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

static void observe_visited_mask(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  for (int by = 0; by < BLOCKS_TALL; by++) {
    int wy = (int)s->y + (by - PLAYER_BLOCK_ROW);
    for (int bx = 0; bx < BLOCKS_WIDE; bx++) {
      int wx = (int)s->x + (bx - PLAYER_BLOCK_COL);
      bool in_map = wx >= 0 && wx < MAX_X && wy >= 0 && wy < MAX_Y;
      out[by * BLOCKS_WIDE + bx] =
          in_map && vbit_get(env->visited_coords, coord_index(s->map_n, (uint8_t)wx, (uint8_t)wy)) ? 1.0f : 0.0f;
    }
  }
}

static void observe_mon(const PkMon *m, obs_t *out) {
  out[0] = (float)m->species;
  out[1] = (float)m->level;
  out[2] = mon_hp_fraction(m);
}

static void observe_battle(const PkSnapshot *s, obs_t *out) {
  memset(out, 0, BATTLE_OBS * sizeof(obs_t));
  if (!is_battle_active(s))
    return;
  out[0] = s->in_battle == 2 ? (float)BATTLE_TYPE_TRAINER : (float)BATTLE_TYPE_WILD;
  observe_mon(&s->battle_mon, out + BATTLE_PLAYER_MON_OFFSET);
  observe_mon(&s->enemy_mon, out + BATTLE_ENEMY_MON_OFFSET);
}

static void observe_party(const PkSnapshot *s, obs_t *out) {
  memset(out, 0, PARTY_OBS * sizeof(obs_t));
  for (int i = 0; i < PARTY_SIZE && i < s->party_count; i++)
    observe_mon(&s->party[i], out + i * MON_FIELDS);
}

static void observe_bag(const PkSnapshot *s, obs_t *out) {
  memset(out, 0, BAG_OBS * sizeof(obs_t));
  for (int i = 0; i < BAG_SLOTS && i < s->bag_count; i++) {
    out[i * BAG_FIELDS + 0] = (float)s->bag[i].item;
    out[i * BAG_FIELDS + 1] = (float)s->bag[i].count;
  }
}

static void update_observations(Env *env) {
  if (!env || !env->agents[0].observations)
    return;

  obs_t *obs = env->agents[0].observations;
  const PkSnapshot *s = &env->cur;

  if (env->screen_obs_enabled)
    env->be->screen(env->impl, obs);
  else
    memset(obs, 0, SCALED_PIXELS * sizeof(obs_t));

  observe_visited_mask(env, obs + VISITED_MASK_OFFSET);
  observe_battle(s, obs + BATTLE_OBS_OFFSET);
  observe_party(s, obs + PARTY_OBS_OFFSET);
  observe_bag(s, obs + BAG_OBS_OFFSET);
}

#endif
