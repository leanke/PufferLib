#ifndef MGBA_ENV_H
#define MGBA_ENV_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <mgba-util/vfs.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/gb/core.h>
#include <mgba/gb/interface.h>

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144
#define SCREEN_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)
#define TOTAL_OBSERVATIONS (SCREEN_PIXELS * 3)

typedef enum {
  GB_KEY_A = (1 << 0),      // 0x01
  GB_KEY_B = (1 << 1),      // 0x02
  GB_KEY_SELECT = (1 << 2), // 0x04
  GB_KEY_START = (1 << 3),  // 0x08
  GB_KEY_RIGHT = (1 << 4),  // 0x10
  GB_KEY_LEFT = (1 << 5),   // 0x20
  GB_KEY_UP = (1 << 6),     // 0x40
  GB_KEY_DOWN = (1 << 7),   // 0x80
} GBKey;

typedef enum {
  GB_ACTION_NOOP = 0,
  GB_ACTION_A,
  GB_ACTION_B,
  GB_ACTION_SELECT,
  GB_ACTION_START,
  GB_ACTION_RIGHT,
  GB_ACTION_LEFT,
  GB_ACTION_UP,
  GB_ACTION_DOWN,
  GB_ACTION_COUNT
} GBAction;

static inline uint32_t action_to_key(int action) {
  if (action <= 0 || action >= GB_ACTION_COUNT)
    return 0;
  return (1 << (action - 1));
}

#define PKMN_X_ADDR 0xD362
#define PKMN_Y_ADDR 0xD361
#define PKMN_MAP_ADDR 0xD35E
#define PKMN_BADGES_ADDR 0xD355
#define PKMN_PARTY_COUNT_ADDR 0xD163
#define PKMN_MONEY_ADDR 0xD347

#define REWARD_BADGE 0.02f   // 1.0f
#define REWARD_POKEMON 0.01f // 0.f
#define REWARD_MAP 0.001f    // 0.2f
#define REWARD_MOVE 0.0025f
#define STAGNATION_LIMIT 1000

typedef struct {
  float episode_return, episode_length, score, total_steps;
  float prev_badges, prev_pokemon_count, n;
} Log;

typedef struct {
  Log log;
  float *observations;
  int *actions;
  float *rewards;
  unsigned char *terminals;
  unsigned char *truncations;

  struct mCore *core;
  color_t *video_buffer;

  int32_t frame_count, step_count, max_episode_length;
  float score;

  uint8_t prev_badges, prev_pokemon_count, prev_x, prev_y, prev_map;
  uint32_t prev_money;
  float prev_reward;
  int32_t stagnation;
  uint8_t x, y, map_n;

  char rom_path[256];
  int32_t frame_skip;
  bool render_enabled;
} mGBA;

void mgba_init_core(mGBA *env, const char *rom_path);
void c_reset(mGBA *env);
void c_step(mGBA *env);
void c_render(mGBA *env);
void c_close(mGBA *env);
void allocate(mGBA *env);
void free_allocated(mGBA *env);
void add_log(mGBA *env);
bool c_save_state(mGBA *env, int slot);
bool c_load_state(mGBA *env, int slot);
bool c_save_state_file(mGBA *env, const char *path);
bool c_load_state_file(mGBA *env, const char *path);

static inline uint8_t read_mem(mGBA *env, uint16_t addr) {
  return env && env->core ? (uint8_t)env->core->rawRead8(env->core, addr, -1)
                          : 0;
}
static inline uint32_t read_bcd_money(mGBA *env, uint16_t addr) {
  uint8_t h = read_mem(env, addr);
  uint8_t m = read_mem(env, addr + 1);
  uint8_t l = read_mem(env, addr + 2);
  return ((h >> 4) * 100000) + ((h & 0xF) * 10000) + ((m >> 4) * 1000) +
         ((m & 0xF) * 100) + ((l >> 4) * 10) + (l & 0xF);
}
static inline void update_observations(mGBA *env) {
  if (!env || !env->video_buffer || !env->observations)
    return;
  for (int i = 0; i < SCREEN_PIXELS; i++) {
    color_t p = env->video_buffer[i];
    env->observations[i * 3] = (float)((p >> 16) & 0xFF);
    env->observations[i * 3 + 1] = (float)((p >> 8) & 0xFF);
    env->observations[i * 3 + 2] = (float)(p & 0xFF);
  }
}
static inline void set_keys(mGBA *env, uint32_t action) {
  if (env && env->core)
    env->core->setKeys(env->core, action & 0xFF);
}

static void silent_log(struct mLogger *logger, int category,
                       enum mLogLevel level, const char *format, va_list args) {
  (void)logger;
  (void)category;
  (void)level;
  (void)format;
  (void)args;
}
static struct mLogger s_silentLogger = {.log = silent_log, .filter = NULL};

void allocate(mGBA *env) {
  env->observations = (float *)calloc(TOTAL_OBSERVATIONS, sizeof(float));
  env->actions = (int *)calloc(1, sizeof(int));
  env->rewards = (float *)calloc(1, sizeof(float));
  env->terminals = (unsigned char *)calloc(1, sizeof(unsigned char));
  env->truncations = (unsigned char *)calloc(1, sizeof(unsigned char));
}
void free_allocated(mGBA *env) {
  free(env->observations);
  free(env->actions);
  free(env->rewards);
  free(env->terminals);
  free(env->truncations);
}
void add_log(mGBA *env) {
  env->log.episode_length = env->step_count;
  env->log.episode_return = env->score;
  env->log.score = env->score;
  env->log.total_steps += env->step_count;
  env->log.prev_pokemon_count = env->prev_pokemon_count;
  env->log.prev_badges = env->prev_badges;
  env->log.n++;
}

void mgba_init_core(mGBA *env, const char *rom_path) {
  if (!env)
    return;

  mLogSetDefaultLogger(&s_silentLogger);

  env->core = mCoreFind(rom_path);
  if (!env->core || !env->core->init(env->core)) {
    fprintf(stderr, "Failed to initialize mGBA core\n");
    env->core = NULL;
    return;
  }

  mCoreInitConfig(env->core, NULL);
  mCoreConfigSetValue(&env->core->config, "sgb.borders", "0");
  mCoreConfigSetValue(&env->core->config, "gb.model", "DMG");
  env->core->loadConfig(env->core, &env->core->config);

  if (!mCoreLoadFile(env->core, rom_path)) {
    fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
    env->core->deinit(env->core);
    env->core = NULL;
    return;
  }

  unsigned int w, h;
  env->core->desiredVideoDimensions(env->core, &w, &h);
  env->video_buffer = (color_t *)calloc(w * h + 256, sizeof(color_t));
  if (env->video_buffer) {
    env->core->setVideoBuffer(env->core, env->video_buffer, w);
  }

  env->core->reset(env->core);
  strncpy(env->rom_path, rom_path, sizeof(env->rom_path) - 1);
}
static float calculate_rewards(mGBA *env) {
  float reward = 0.0f;

  uint8_t badges = read_mem(env, PKMN_BADGES_ADDR);
  if (badges > env->prev_badges) {
    reward += REWARD_BADGE;
    env->prev_badges = badges;
  }
  uint8_t pokemon = read_mem(env, PKMN_PARTY_COUNT_ADDR);
  if (pokemon > env->prev_pokemon_count && pokemon <= 6) {
    reward += REWARD_POKEMON;
    env->prev_pokemon_count = pokemon;
  }
  uint8_t map = read_mem(env, PKMN_MAP_ADDR);
  if (map != env->prev_map) {
    reward += REWARD_MAP;
    env->prev_map = map;
    env->stagnation = 0;
  }
  uint8_t x = read_mem(env, PKMN_X_ADDR);
  uint8_t y = read_mem(env, PKMN_Y_ADDR);
  if (x != env->prev_x || y != env->prev_y) {
    reward += REWARD_MOVE;
    env->prev_x = x;
    env->prev_y = y;
    env->stagnation = 0;
  } else {
    env->stagnation++;
  }

  env->x = x;
  env->y = y;
  env->map_n = map;
  return reward;
}
void c_reset(mGBA *env) {
  if (!env || !env->core)
    return;

  const char *state_path = "./states/pre-choice.state";

  struct VFile *vf = VFileOpen(state_path, O_RDONLY);
  if (vf) {
    if (mCoreLoadStateNamed(env->core, vf, SAVESTATE_ALL)) {
      vf->close(vf);
      env->step_count = env->frame_count = 0;
      env->score = 0.0f;
      env->stagnation = 0;

      env->prev_badges = read_mem(env, PKMN_BADGES_ADDR);
      env->prev_pokemon_count = read_mem(env, PKMN_PARTY_COUNT_ADDR);
      env->prev_money = read_bcd_money(env, PKMN_MONEY_ADDR);
      env->prev_x = read_mem(env, PKMN_X_ADDR);
      env->prev_y = read_mem(env, PKMN_Y_ADDR);
      env->prev_map = read_mem(env, PKMN_MAP_ADDR);

      update_observations(env);
      env->rewards[0] = 0;
      env->terminals[0] = 0;
      return;
    }
    vf->close(vf);
    fprintf(stderr, "Warning: Failed to load state from: %s\n", state_path);
  } else {
    fprintf(stderr, "Warning: Could not open state file: %s\n", state_path);
  }
  env->core->reset(env->core);
  env->step_count = env->frame_count = 0;
  env->score = 0.0f;
  env->stagnation = 0;

  env->prev_badges = read_mem(env, PKMN_BADGES_ADDR);
  env->prev_pokemon_count = read_mem(env, PKMN_PARTY_COUNT_ADDR);
  env->prev_money = read_bcd_money(env, PKMN_MONEY_ADDR);
  env->prev_x = read_mem(env, PKMN_X_ADDR);
  env->prev_y = read_mem(env, PKMN_Y_ADDR);
  env->prev_map = read_mem(env, PKMN_MAP_ADDR);

  for (int i = 0; i < 4; i++)
    env->core->runFrame(env->core);
  update_observations(env);

  env->rewards[0] = 0;
  env->terminals[0] = 0;
}
void c_step(mGBA *env) {
  if (!env || !env->core)
    return;

  env->rewards[0] = 0;
  env->terminals[0] = 0;
  env->step_count++;

  set_keys(env, action_to_key(env->actions[0]));
  int skip = env->frame_skip > 0 ? env->frame_skip : 1;
  for (int i = 0; i < skip; i++) {
    env->core->runFrame(env->core);
    env->frame_count++;
  }
  set_keys(env, 0);

  update_observations(env);
  float reward = calculate_rewards(env);
  env->rewards[0] = reward;
  env->score += reward;

  if (env->step_count >=
      env->max_episode_length) { // || env->stagnation > STAGNATION_LIMIT
    env->terminals[0] = 1;
    add_log(env);
    c_reset(env);
  }
}
void c_render(mGBA *env) { (void)env; }
void c_close(mGBA *env) {
  if (!env)
    return;

  if (env->core) {
    env->core->setVideoBuffer(env->core, NULL, 0);
    mCoreConfigDeinit(&env->core->config);
    env->core->deinit(env->core);
    env->core = NULL;
  }

  if (env->video_buffer) {
    free(env->video_buffer);
    env->video_buffer = NULL;
  }
}

bool c_save_state(mGBA *env, int slot) {
  if (!env || !env->core || slot < 0 || slot > 9)
    return false;
  return mCoreSaveState(env->core, slot, SAVESTATE_ALL);
}

bool c_load_state(mGBA *env, int slot) {
  if (!env || !env->core || slot < 0 || slot > 9)
    return false;
  bool result = mCoreLoadState(env->core, slot, SAVESTATE_ALL);
  if (result) {
    update_observations(env);
    env->prev_badges = read_mem(env, PKMN_BADGES_ADDR);
    env->prev_pokemon_count = read_mem(env, PKMN_PARTY_COUNT_ADDR);
    env->prev_money = read_bcd_money(env, PKMN_MONEY_ADDR);
    env->prev_x = read_mem(env, PKMN_X_ADDR);
    env->prev_y = read_mem(env, PKMN_Y_ADDR);
    env->prev_map = read_mem(env, PKMN_MAP_ADDR);
    env->stagnation = 0;
  }
  return result;
}

bool c_save_state_file(mGBA *env, const char *path) {
  if (!env || !env->core || !path)
    return false;
  struct VFile *vf = VFileOpen(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!vf)
    return false;
  bool result = mCoreSaveStateNamed(env->core, vf, SAVESTATE_ALL);
  vf->close(vf);
  return result;
}

bool c_load_state_file(mGBA *env, const char *path) {
  if (!env || !env->core || !path)
    return false;
  struct VFile *vf = VFileOpen(path, O_RDONLY);
  if (!vf)
    return false;
  bool result = mCoreLoadStateNamed(env->core, vf, SAVESTATE_ALL);
  vf->close(vf);
  if (result) {
    update_observations(env);
    env->prev_badges = read_mem(env, PKMN_BADGES_ADDR);
    env->prev_pokemon_count = read_mem(env, PKMN_PARTY_COUNT_ADDR);
    env->prev_money = read_bcd_money(env, PKMN_MONEY_ADDR);
    env->prev_x = read_mem(env, PKMN_X_ADDR);
    env->prev_y = read_mem(env, PKMN_Y_ADDR);
    env->prev_map = read_mem(env, PKMN_MAP_ADDR);
    env->stagnation = 0;
  }
  return result;
}

#endif // MGBA_ENV_H
