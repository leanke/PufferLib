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
          (obs_t)(in_map && vbit_get(env->visited_coords, coord_index(s->map_n, (uint8_t)wx, (uint8_t)wy)) ? 1 : 0);
    }
  }
}

static void observe_mon(const PkMon *m, obs_t *out) {
  out[0] = (obs_t)m->species;
  out[1] = (obs_t)m->level;
  out[2] = (obs_t)(PK_OBS_HP_SCALE == 1.0f ? mon_hp_fraction(m) : lrintf(mon_hp_fraction(m) * PK_OBS_HP_SCALE));
}

static void observe_battle(const PkSnapshot *s, obs_t *out) {
  memset(out, 0, BATTLE_OBS * sizeof(obs_t));
  if (!is_battle_active(s))
    return;
  out[0] = (obs_t)(s->in_battle == 2 ? BATTLE_TYPE_TRAINER : BATTLE_TYPE_WILD);
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
    out[i * BAG_FIELDS + 0] = (obs_t)s->bag[i].item;
    out[i * BAG_FIELDS + 1] = (obs_t)s->bag[i].count;
  }
}

static void observe_tiles(const PkSnapshot *s, obs_t *out) {
  memset(out, 0, SCALED_PIXELS * sizeof(obs_t));
  for (int i = 0; i < PK_TILE_MAP_CELLS; i++)
    out[TILE_OBS_OFFSET + i] = (obs_t)s->tile_map[i];
  obs_t *sprites = out + SPRITE_OBS_OFFSET;
  for (int k = 0; k < PK_SPRITES; k++) {
    if (!s->sprites[k].picture || s->sprites[k].image == PKRED_SPRITE_HIDDEN)
      continue;
    int row = (s->sprites[k].y + 4) / 8, col = s->sprites[k].x / 8;
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++) {
        int r = row + dy, c = col + dx;
        if (r >= 0 && r < PK_TILE_MAP_H && c >= 0 && c < PK_TILE_MAP_W)
          sprites[r * PK_TILE_MAP_W + c] = (obs_t)(k == 0 ? TILE_SPRITE_PLAYER : TILE_SPRITE_NPC);
      }
  }
}

static void update_observations(Env *env) {
  if (!env || !env->agents[0].observations)
    return;

  obs_t *obs = env->agents[0].observations;
  const PkSnapshot *s = &env->cur;

  if (env->obs_tiles) {
    observe_tiles(s, obs);
  } else if (env->screen_obs_enabled && sizeof(obs_t) == sizeof(float)) {
    env->be->screen(env->impl, (float *)obs);
  } else if (env->screen_obs_enabled) {
    if (!env->screen_buf)
      env->screen_buf = (float *)malloc(SCALED_PIXELS * sizeof(float));
    env->be->screen(env->impl, env->screen_buf);
    for (int i = 0; i < SCALED_PIXELS; i++)
      obs[i] = (obs_t)env->screen_buf[i];
  } else
    memset(obs, 0, SCALED_PIXELS * sizeof(obs_t));

  observe_visited_mask(env, obs + VISITED_MASK_OFFSET);
  observe_battle(s, obs + BATTLE_OBS_OFFSET);
  observe_party(s, obs + PARTY_OBS_OFFSET);
  observe_bag(s, obs + BAG_OBS_OFFSET);
}

#endif
