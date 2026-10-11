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

static float mon_pp_fraction(const PkMon *m) {
  int pp = 0, max = 0;
  for (int k = 0; k < 4; k++) {
    int mx = move_max_pp(m->moves[k], m->pp[k]);
    if (mx) {
      pp += m->pp[k] & PKRED_PP_MASK;
      max += mx;
    }
  }
  return max ? fminf(1.0f, (float)pp / (float)max) : 0.0f;
}

static inline obs_t obs_fraction(float f) {
  return (obs_t)(PK_OBS_HP_SCALE == 1.0f ? f : lrintf(f * PK_OBS_HP_SCALE));
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

static void observe_mon(const Env *env, const PkMon *m, obs_t *out) {
  out[MON_SPECIES] = (obs_t)m->species;
  out[MON_LEVEL] = (obs_t)m->level;
  out[MON_HP] = obs_fraction(mon_hp_fraction(m));
  out[MON_STATUS] = (obs_t)(env->obs_mon_detail_enabled ? m->status : 0);
  out[MON_PP] = env->obs_mon_detail_enabled ? obs_fraction(mon_pp_fraction(m)) : (obs_t)0;
}

static void observe_info(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  memset(out, 0, INFO_OBS * sizeof(obs_t));
  if (!env->obs_info_enabled)
    return;
  out[INFO_MAP] = (obs_t)s->map_n;
  out[INFO_TILESET] = (obs_t)s->tileset;
  out[INFO_BADGES] = (obs_t)s->badges;
  out[INFO_FACING] = (obs_t)(s->facing & 3);
}

static void observe_battle(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  memset(out, 0, BATTLE_OBS * sizeof(obs_t));
  if (!is_battle_active(s))
    return;
  out[0] = (obs_t)(s->in_battle == 2 ? BATTLE_TYPE_TRAINER : BATTLE_TYPE_WILD);
  observe_mon(env, &s->battle_mon, out + BATTLE_PLAYER_MON_OFFSET);
  observe_mon(env, &s->enemy_mon, out + BATTLE_ENEMY_MON_OFFSET);
}

static void observe_party(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  memset(out, 0, PARTY_OBS * sizeof(obs_t));
  for (int i = 0; i < PARTY_SIZE && i < s->party_count; i++)
    observe_mon(env, &s->party[i], out + i * MON_FIELDS);
}

static void observe_bag(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  memset(out, 0, BAG_OBS * sizeof(obs_t));
  for (int i = 0; i < BAG_SLOTS && i < s->bag_count; i++) {
    out[i * BAG_FIELDS + BAG_ITEM] = (obs_t)s->bag[i].item;
    out[i * BAG_FIELDS + BAG_COUNT] = (obs_t)s->bag[i].count;
  }
  if (!env->obs_item_context_enabled)
    return;
  for (int i = 0; i < BAG_SLOTS && i < s->bag_count; i++) {
    out[i * BAG_FIELDS + BAG_USABLE] = (obs_t)item_usable(s, s->bag[i].item);
    out[i * BAG_FIELDS + BAG_CURSOR] = (obs_t)(s->bag_cursor == i + 1);
  }
  for (int i = 0; i < PARTY_SIZE && i < s->party_count; i++)
    out[BAG_HM_LEARNABLE + i] = (obs_t)mon_hm_learnable(s, &s->party[i]);
}

static void observe_walkable(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  memset(out, 0, PK_TILE_MAP_CELLS * sizeof(obs_t));
  if (!env->obs_walkable_enabled || is_battle_active(s) || s->tileset >= PKRED_NUM_TILESETS)
    return;
  uint8_t walkable[256] = {0};
  for (const uint8_t *t = PKRED_TILESET_COLLISION[s->tileset]; *t != 0xff; t++)
    walkable[*t] = 1;
  for (int by = 0; by < BLOCKS_TALL; by++)
    for (int bx = 0; bx < BLOCKS_WIDE; bx++) {
      int r = by * 2, c = bx * 2;
      if (!walkable[s->tile_map[(r + 1) * PK_TILE_MAP_W + c]])
        continue;
      for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++)
          out[(r + dy) * PK_TILE_MAP_W + c + dx] = (obs_t)1;
    }
}

static void observe_tiles(const Env *env, obs_t *out) {
  const PkSnapshot *s = &env->cur;
  for (int i = 0; i < PK_TILE_MAP_CELLS; i++)
    out[TILE_OBS_OFFSET + i] = (obs_t)s->tile_map[i];
  obs_t *sprites = out + SPRITE_OBS_OFFSET;
  memset(sprites, 0, PK_TILE_MAP_CELLS * sizeof(obs_t));
  for (int k = 0; k < PK_SPRITES; k++) {
    if (!s->sprites[k].picture || s->sprites[k].image == PKRED_SPRITE_HIDDEN)
      continue;
    obs_t id = (obs_t)(env->obs_sprite_ids_enabled ? s->sprites[k].picture : 1);
    int row = (s->sprites[k].y + 4) / 8, col = s->sprites[k].x / 8;
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++) {
        int r = row + dy, c = col + dx;
        if (r >= 0 && r < PK_TILE_MAP_H && c >= 0 && c < PK_TILE_MAP_W)
          sprites[r * PK_TILE_MAP_W + c] = id;
      }
  }
  observe_walkable(env, out + WALKABLE_OBS_OFFSET);
}

static void update_observations(Env *env) {
  if (!env || !env->agents[0].observations)
    return;

  obs_t *obs = env->agents[0].observations;

  if (env->obs_tiles) {
    observe_tiles(env, obs);
  } else if (env->screen_obs_enabled && sizeof(obs_t) == sizeof(float)) {
    env->be->screen(env->impl, (float *)(obs + SCREEN_OBS_OFFSET));
  } else if (env->screen_obs_enabled) {
    if (!env->screen_buf)
      env->screen_buf = (float *)malloc(SCALED_PIXELS * sizeof(float));
    env->be->screen(env->impl, env->screen_buf);
    for (int i = 0; i < SCALED_PIXELS; i++)
      obs[SCREEN_OBS_OFFSET + i] = (obs_t)env->screen_buf[i];
  } else
    memset(obs + SCREEN_OBS_OFFSET, 0, SCALED_PIXELS * sizeof(obs_t));

  observe_info(env, obs + INFO_OBS_OFFSET);
  observe_visited_mask(env, obs + VISITED_MASK_OFFSET);
  observe_battle(env, obs + BATTLE_OBS_OFFSET);
  observe_party(env, obs + PARTY_OBS_OFFSET);
  observe_bag(env, obs + BAG_OBS_OFFSET);
}

#endif
