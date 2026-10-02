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

static void update_observations(Env *env) {
  if (!env || !env->agents[0].observations)
    return;

  obs_t *obs = env->agents[0].observations;
  const PkSnapshot *s = &env->cur;

  if (!env->screen_obs_enabled)
    memset(obs, 0, SCALED_PIXELS * sizeof(obs_t));
  else
    env->be->screen(env->impl, obs);

  {
    int mo = VISITED_MASK_OFFSET;
    for (int by = 0; by < BLOCKS_TALL; by++) {
      int wcy = (int)s->y + (by - PLAYER_BLOCK_ROW);
      for (int bx = 0; bx < BLOCKS_WIDE; bx++) {
        int wcx = (int)s->x + (bx - PLAYER_BLOCK_COL);
        float visited = 0.0f;
        if (wcx >= 0 && wcx < MAX_X && wcy >= 0 && wcy < MAX_Y)
          visited = vbit_get(env->visited_coords, coord_index(s->map_n, (uint8_t)wcx, (uint8_t)wcy)) ? 1.0f : 0.0f;
        obs[mo + by * BLOCKS_WIDE + bx] = visited;
      }
    }
  }

  bool in_battle = is_battle_active(s);
  int b = BATTLE_OBS_OFFSET;
  obs[b + 0] = in_battle ? 1.0f : 0.0f;
  obs[b + 1] = in_battle ? (float)s->selected_move : 0.0f;
  obs[b + 2] = in_battle ? mon_hp_fraction(&s->battle_mon) : 0.0f;
  obs[b + 3] = in_battle ? mon_hp_fraction(&s->enemy_mon) : 0.0f;

  int p = POSITION_OBS_OFFSET;
  obs[p + 0] = (float)s->x;
  obs[p + 1] = (float)s->y;
  obs[p + 2] = (float)s->map_n;
  obs[p + 3] = (float)s->facing;
  obs[p + 4] = env->map_exhaustion_obs_enabled
      ? fminf(1.0f, (float)env->map_visited_counts[s->map_n] / env->map_exhaustion_norm)
      : 0.0f;

  int g = PROGRESS_OBS_OFFSET;
  obs[g + 0] = (float)__builtin_popcount(s->badges);
  obs[g + 1] = (float)s->pokedex_owned_count;
  obs[g + 2] = (float)s->pokedex_seen_count;

  int pa = PARTY_OBS_OFFSET;
  for (int i = 0; i < PARTY_SIZE; i++) {
    const PkMon *m = &s->party[i];
    int slot = pa + i * PARTY_FIELDS;
    obs[slot + 0] = (float)m->species;
    obs[slot + 1] = (float)m->level;
    obs[slot + 2] = (float)m->hp;
    obs[slot + 3] = (float)m->max_hp;
    for (int k = 0; k < 4; k++)
      obs[slot + 4 + k] = (float)m->moves[k];
  }
}

#endif
