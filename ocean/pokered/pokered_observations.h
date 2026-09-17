#ifndef POKERED_OBSERVATIONS_H
#define POKERED_OBSERVATIONS_H

#include <math.h>
#include <stddef.h>

static void update_observations(Env *env) {
  if (!env || !env->emu.video_buffer || !env->agents[0].observations)
    return;

  PREFETCH_READ(env->emu.video_buffer);
  PREFETCH_WRITE(env->agents[0].observations);
  const color_t *vbuf = env->emu.video_buffer;
  obs_t *obs = env->agents[0].observations;
  CoreState *core = &env->gstate.core;
  Emulator *emu = &env->emu;

  for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
    for (int sx = 0; sx < SCALED_WIDTH; sx++) {
      int src_y = sy * 2;
      int src_x = sx * 2;

      uint32_t gray_sum = 0;
      for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
          int src_idx = (src_y + dy) * GB_VIDEO_PITCH + (src_x + dx);
          color_t pixel = vbuf[src_idx];
          uint32_t r = (pixel >> 16) & 0xFF;
          uint32_t g = (pixel >> 8) & 0xFF;
          uint32_t b = pixel & 0xFF;
          gray_sum += r * 77 + g * 150 + b * 29;
        }
      }
      obs[sy * SCALED_WIDTH + sx] = (float)(gray_sum >> 10);
    }
  }

  int o = SCALED_PIXELS;

  obs[o + 0] = (float)__builtin_popcount(core->badges);
  obs[o + 1] = (float)core->x;
  obs[o + 2] = (float)core->y;
  obs[o + 3] = (float)core->map_n;
  obs[o + 4] = (float)(read_mem(emu, PKRED_ADDR_PLAYER_SPRITE_FACING_DIRECTION) / 4);
  obs[o + 5] = (float)core->party_count;
  obs[o + 6] = env->map_exhaustion_obs_enabled
      ? fminf(1.0f, (float)env->map_visited_counts[core->map_n] / env->map_exhaustion_norm)
      : 0.0f;

  bool in_battle = is_battle_active(&env->gstate.battle);
  obs[o + 7] = in_battle ? 1.0f : 0.0f;
  obs[o + 8] = in_battle ? (float)read_mem(emu, PKRED_ADDR_PLAYER_SELECTED_MOVE) : 0.0f;
  obs[o + 9] = in_battle ? battle_mon_hp_fraction(emu) : 0.0f;
  obs[o + 10] = in_battle ? enemy_mon_hp_fraction(emu) : 0.0f;

  PREFETCH_READ(env->visited_coords);
  int v = VISITED_OBS_OFFSET;
  int half = VISITED_WINDOW / 2;
  for (int wy = 0; wy < VISITED_WINDOW; wy++) {
    int cy = (int)core->y + (wy - half);
    for (int wx = 0; wx < VISITED_WINDOW; wx++) {
      int cx = (int)core->x + (wx - half);
      float visited = 0.0f;
      if (cx >= 0 && cx < MAX_X && cy >= 0 && cy < MAX_Y) {
        visited = (float)env->visited_coords[coord_index(core->map_n, (uint8_t)cx, (uint8_t)cy)];
      }
      obs[v + wy * VISITED_WINDOW + wx] = visited;
    }
  }

  int p = PARTY_OBS_OFFSET;
  for (int i = 0; i < PARTY_SIZE; i++) {
    uint16_t base = PKRED_ADDR_PARTY_MON(i);
    uint8_t id     = read_mem(emu, base + offsetof(PkredPartyMon, species));
    uint8_t level  = read_mem(emu, base + offsetof(PkredPartyMon, level));
    uint16_t hp    = read_big_endian_16(emu, base + offsetof(PkredPartyMon, hp_hi));
    uint16_t maxhp = read_big_endian_16(emu, base + offsetof(PkredPartyMon, max_hp_hi));

    obs[p + i * PARTY_FIELDS + 0] = (float)id;
    obs[p + i * PARTY_FIELDS + 1] = (float)level;
    obs[p + i * PARTY_FIELDS + 2] = (float)hp;
    obs[p + i * PARTY_FIELDS + 3] = (float)maxhp;
  }
}

static int calc_pokedex_count(Emulator *emu, uint16_t addr, int size) {
  int count = 0;
  for (int i = 0; i < size; i++)
    count += __builtin_popcount(read_mem(emu, addr + i));
  return count;
}

static void update_core_state(Env *env) {
  CoreState *core = &env->gstate.core;
  Emulator *emu = &env->emu;

  core->x = read_mem(emu, PKRED_ADDR_X_COORD);
  core->y = read_mem(emu, PKRED_ADDR_Y_COORD);
  core->map_n = read_mem(emu, PKRED_ADDR_CUR_MAP);
  core->idx = coord_index(core->map_n, core->x, core->y);
  core->badges = read_mem(emu, PKRED_ADDR_OBTAINED_BADGES);
  core->party_count = read_mem(emu, PKRED_ADDR_PARTY_COUNT);
  for (int i = 0; i < 6; i++) {
    core->levels[i] = read_mem(emu, PKRED_ADDR_PARTY_MON(i) + offsetof(PkredPartyMon, level));
    for (int m = 0; m < 4; m++)
      core->moves[i][m] = read_mem(emu, PKRED_ADDR_PARTY_MON(i) + offsetof(PkredPartyMon, moves) + m);
  }
  core->pokedex_owned_count = (uint8_t)calc_pokedex_count(emu, PKRED_ADDR_POKEDEX_OWNED, POKEDEX_OWNED_SIZE);
  core->pokedex_seen_count = (uint8_t)calc_pokedex_count(emu, PKRED_ADDR_POKEDEX_SEEN, POKEDEX_SEEN_SIZE);
  core->hp_fraction = party_hp_fraction(emu);
}

#endif /* POKERED_OBSERVATIONS_H */
