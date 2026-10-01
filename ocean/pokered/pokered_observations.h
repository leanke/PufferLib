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
  const PkMon *bm = &s->battle_mon, *em = &s->enemy_mon;
  int b = BATTLE_OBS_OFFSET;
  obs[b + 0] = in_battle ? 1.0f : 0.0f;
  obs[b + 2] = in_battle ? (float)s->selected_move : 0.0f;
  obs[b + 5] = in_battle ? mon_hp_fraction(bm) : 0.0f;
  obs[b + 9] = in_battle ? mon_hp_fraction(em) : 0.0f;
  if (env->battle_status_obs_enabled) {
    obs[b + 1] = (float)s->battle_type;
    obs[b + 3] = in_battle ? (float)bm->species : 0.0f;
    obs[b + 4] = in_battle ? (float)bm->level : 0.0f;
    obs[b + 6] = in_battle ? (float)bm->status : 0.0f;
    obs[b + 7] = in_battle ? (float)em->species : 0.0f;
    obs[b + 8] = in_battle ? (float)em->level : 0.0f;
    obs[b + 10] = in_battle ? (float)em->status : 0.0f;
    obs[b + 11] = (float)s->fainted_count;
    obs[b + 20] = in_battle ? (float)bm->type1 : 0.0f;
    obs[b + 21] = in_battle ? (float)bm->type2 : 0.0f;
    obs[b + 22] = in_battle ? (float)em->type1 : 0.0f;
    obs[b + 23] = in_battle ? (float)em->type2 : 0.0f;
  } else {
    obs[b + 1] = 0.0f;
    obs[b + 3] = 0.0f;
    obs[b + 4] = 0.0f;
    obs[b + 6] = 0.0f;
    obs[b + 7] = 0.0f;
    obs[b + 8] = 0.0f;
    obs[b + 10] = 0.0f;
    obs[b + 11] = 0.0f;
    obs[b + 20] = 0.0f;
    obs[b + 21] = 0.0f;
    obs[b + 22] = 0.0f;
    obs[b + 23] = 0.0f;
  }

  if (env->battle_moveset_obs_enabled) {
    for (int m = 0; m < 4; m++) {
      obs[b + 12 + m] = in_battle ? (float)bm->moves[m] : 0.0f;
      obs[b + 16 + m] = in_battle ? (float)bm->pp[m] : 0.0f;
    }
  } else {
    for (int m = 0; m < 4; m++) {
      obs[b + 12 + m] = 0.0f;
      obs[b + 16 + m] = 0.0f;
    }
  }

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
  for (int i = 0; i < PKRED_NUM_BADGES; i++)
    obs[g + 1 + i] = env->progress_badges_bits_enabled ? (float)((s->badges >> i) & 1) : 0.0f;

  static const uint8_t HM_ITEM_IDS[5] = {
      PKRED_ITEM_HM01_CUT, PKRED_ITEM_HM02_FLY, PKRED_ITEM_HM03_SURF,
      PKRED_ITEM_HM04_STRENGTH, PKRED_ITEM_HM05_FLASH,
  };
  for (int i = 0; i < 5; i++)
    obs[g + 1 + PKRED_NUM_BADGES + i] =
        env->hm_bag_obs_enabled ? (s->bag_qty[HM_ITEM_IDS[i]] > 0 ? 1.0f : 0.0f) : 0.0f;

  int progress_tail = g + 1 + PKRED_NUM_BADGES + 5;
  if (env->blackout_map_obs_enabled) {
    obs[progress_tail + 0] = (float)env->blackout_count;
    obs[progress_tail + 1] = (float)s->last_blackout_map;
  } else {
    obs[progress_tail + 0] = 0.0f;
    obs[progress_tail + 1] = 0.0f;
  }
  obs[progress_tail + 2] = (float)s->pokedex_owned_count;
  obs[progress_tail + 3] = (float)s->pokedex_seen_count;
  obs[progress_tail + 4] = env->progress_money_obs_enabled ? (float)s->money : 0.0f;

  int pa = PARTY_OBS_OFFSET;
  for (int i = 0; i < PARTY_SIZE; i++) {
    const PkMon *m = &s->party[i];
    int slot = pa + i * PARTY_FIELDS;
    obs[slot + 0] = (float)m->species;
    obs[slot + 1] = (float)m->level;
    obs[slot + 2] = (float)m->hp;
    obs[slot + 3] = (float)m->max_hp;
    for (int k = 0; k < 4; k++)
      obs[slot + 5 + k] = (float)m->moves[k];

    if (env->party_status_pp_obs_enabled) {
      obs[slot + 4] = (float)m->status;
      for (int k = 0; k < 4; k++)
        obs[slot + 9 + k] = (float)m->pp[k];
      obs[slot + 13] = (i < s->party_count && m->hp == 0) ? 1.0f : 0.0f;
    } else {
      obs[slot + 4] = 0.0f;
      for (int k = 0; k < 4; k++)
        obs[slot + 9 + k] = 0.0f;
      obs[slot + 13] = 0.0f;
    }

    if (env->party_type_obs_enabled) {
      obs[slot + 14] = (float)m->type1;
      obs[slot + 15] = (float)m->type2;
    } else {
      obs[slot + 14] = 0.0f;
      obs[slot + 15] = 0.0f;
    }
  }

  int bg = BAG_OBS_OFFSET;
  obs[bg + 0] = env->bag_obs_enabled ? (float)s->num_bag_items : 0.0f;
  for (int i = 0; i < PKRED_BAG_TRACKED_ITEMS; i++)
    obs[bg + 1 + i] = env->bag_obs_enabled ? (float)s->bag_qty[PKRED_BAG_TRACKED_ITEM_IDS[i]] : 0.0f;
  for (int i = 0; i < PKRED_KEY_ITEMS; i++)
    obs[bg + 1 + PKRED_BAG_TRACKED_ITEMS + i] =
        env->key_item_obs_enabled ? (s->bag_qty[PKRED_KEY_ITEM_IDS[i]] > 0 ? 1.0f : 0.0f) : 0.0f;
}

#endif
