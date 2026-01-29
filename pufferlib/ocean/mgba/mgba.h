#ifndef MGBA_ENV_H
#define MGBA_ENV_H

#include "mgba_core.h"
// #include "events.h"

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144
#define SCREEN_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)
#define EXTRA_OBS 5 // Add 5 for the ram obs (coords, badges, p_count)
#define TOTAL_OBSERVATIONS (SCREEN_PIXELS * 3 + EXTRA_OBS)



#define PKMN_X_ADDR 0xD362
#define PKMN_Y_ADDR 0xD361
#define PKMN_MAP_ADDR 0xD35E
#define PKMN_BADGES_ADDR 0xD356
#define PKMN_PARTY_COUNT_ADDR 0xD163
#define PKMN_MONEY_ADDR 0xD347
#define PKM_LEVEL_ADDR_1 0xD18C
#define PKM_LEVEL_ADDR_2 0xD1B8
#define PKM_LEVEL_ADDR_3 0xD1E4
#define PKM_LEVEL_ADDR_4 0xD210
#define PKM_LEVEL_ADDR_5 0xD23C
#define PKM_LEVEL_ADDR_6 0xD268
#define PKM_
// PARTY_ADDR = [0xD164, 0xD165, 0xD166, 0xD167, 0xD168, 0xD169]
// #define PKMN1_ADDR 0xD16B
// #define PKMN2_ADDR 0xD197
// #define PKMN3_ADDR 0xD1C3
// #define PKMN4_ADDR 0xD1EF
// #define PKMN5_ADDR 0xD21B
// #define PKMN6_ADDR 0xD247
// hm_ids = [0xC4, 0xC5, 0xC6, 0xC7, 0xC8]



#define REWARD_BADGE 1.0f   // 1.0f
#define REWARD_POKEMON 0.5f // 0.f
// #define REWARD_MAP 0.001f    // 0.2f
#define REWARD_UNIQUE_COORD 0.0025f
#define REWARD_LEVEL 0.25f
// #define STAGNATION_LIMIT 1000


#define MAX_MAPS 256 // def way to big for the map but oh well
#define MAX_X 256
#define MAX_Y 256
#define VISITED_COORDS_SIZE (MAX_MAPS * MAX_X * MAX_Y)

typedef struct {
  float episode_length;
  float episode_return;
  float total_steps;
  float unique_coords;
  float level_sum; 

  float badges;
  float money;
  float party_count;
  float pkmn1_lvl;
  float pkmn2_lvl;
  float pkmn3_lvl;
  float pkmn4_lvl; 
  float pkmn5_lvl;
  float pkmn6_lvl;

  float n;
} Log;

// typedef struct {
//   uint8_t poke_id;
//   uint8_t type1;
//   uint16_t current_hp;
//   uint16_t max_hp;
//   uint8_t status;
//   uint8_t level;
//   uint16_t attack;
//   uint16_t defense;
//   uint16_t speed;
//   uint16_t special;
// } Pkmn;



typedef struct {

  uint8_t x; // read_mem(env, PKMN_X_ADDR);
  uint8_t y; // read_mem(env, PKMN_Y_ADDR);
  uint8_t map_n; // read_mem(env, PKMN_MAP_ADDR);

  uint8_t badges; // read_mem(env, PKMN_BADGES_ADDR);
  uint32_t money; // read_bcd(env, PKMN_MONEY_ADDR);

  uint8_t party_count; // read_mem(env, PKMN_PARTY_COUNT_ADDR);
  uint8_t pkmn1_lvl; // read_mem(env, PKM_LEVEL_ADDR_1);
  uint8_t pkmn2_lvl; // read_mem(env, PKM_LEVEL_ADDR_2);
  uint8_t pkmn3_lvl; // read_mem(env, PKM_LEVEL_ADDR_3);
  uint8_t pkmn4_lvl; // read_mem(env, PKM_LEVEL_ADDR_4);
  uint8_t pkmn5_lvl; // read_mem(env, PKM_LEVEL_ADDR_5);
  uint8_t pkmn6_lvl; // read_mem(env, PKM_LEVEL_ADDR_6);

} RamState;

typedef struct {
  Log log;
  Emu emu;
  RamState ram;
  RamState prev_ram;

  float *observations;
  int *actions;
  float *rewards;
  unsigned char *terminals;
  unsigned char *truncations;
  
  int32_t frame_count;
  int32_t step_count;
  int32_t max_episode_length;
  float score;

  int32_t stagnation;
  uint8_t *visited_coords; 
  uint8_t *prev_visited_coords; 
  uint32_t unique_coords_count;  


  bool full_reset;
} mGBA;


void update_ram(mGBA *env);
void c_reset(mGBA *env);
void c_step(mGBA *env);
void c_render(mGBA *env);
void c_close(mGBA *env);
void allocate(mGBA *env);
void free_allocated(mGBA *env);
void add_log(mGBA *env);

static inline void update_observations(mGBA *env) {
  if (!env || !env->emu.video_buffer || !env->observations)
    return;
  for (int i = 0; i < SCREEN_PIXELS; i++) {
    color_t p = env->emu.video_buffer[i];
    // printf("Pixel %d: 0x%06X\n", i, p);
    env->observations[i * 3] = (float)((p >> 16) & 0xFF);
    env->observations[i * 3 + 1] = (float)((p >> 8) & 0xFF);
    env->observations[i * 3 + 2] = (float)(p & 0xFF);
  }
  int offset = SCREEN_PIXELS * 3;
  env->observations[offset + 0] = (float)env->ram.x;
  env->observations[offset + 1] = (float)env->ram.y;
  env->observations[offset + 2] = (float)env->ram.map_n;
  env->observations[offset + 3] = (float)env->ram.badges;
  env->observations[offset + 4] = (float)env->ram.party_count;
}


static inline uint32_t coord_index(uint8_t map, uint8_t x, uint8_t y) {
  return ((uint32_t)map << 16) | ((uint32_t)x << 8) | (uint32_t)y;
}
static inline bool is_coord_visited(mGBA *env, uint32_t idx) {
  if (!env || !env->visited_coords) return false;
  if (idx >= VISITED_COORDS_SIZE) return false;
  return env->visited_coords[idx];
}
static inline void mark_coord_visited(mGBA *env, uint32_t idx) {
  if (!env || !env->visited_coords) return;
  if (idx >= VISITED_COORDS_SIZE) return;
  env->visited_coords[idx] = 1;
}
static inline void clear_visited_coords(mGBA *env) {
  if (env && env->visited_coords) {
    memset(env->visited_coords, 0, VISITED_COORDS_SIZE);
  }
}


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
  free(env->visited_coords);
  free(env->prev_visited_coords);
}
void add_log(mGBA *env) {
  RamState *ram = &env->ram;
  env->log.episode_length = env->step_count;
  env->log.episode_return = env->score;
  env->log.total_steps += env->step_count;
  env->log.unique_coords = env->unique_coords_count;
  env->log.level_sum = calc_level_sum(ram);

  env->log.badges = ram->badges;
  env->log.money = ram->money;
  env->log.party_count = ram->party_count;
  env->log.pkmn1_lvl = ram->pkmn1_lvl;
  env->log.pkmn2_lvl = ram->pkmn2_lvl;
  env->log.pkmn3_lvl = ram->pkmn3_lvl;
  env->log.pkmn4_lvl = ram->pkmn4_lvl;
  env->log.pkmn5_lvl = ram->pkmn5_lvl;
  env->log.pkmn6_lvl = ram->pkmn6_lvl;

  env->log.n++;
}

// void read_pkmn(Emu *emu, Pkmn *pkmn, uint16_t start_addr) {
//   pkmn->poke_id = read_mem(emu, start_addr);
//   pkmn->type1 = read_mem(emu, start_addr + 0x05);
//   pkmn->current_hp = read_uint16(emu, start_addr + 0x01);
//   pkmn->max_hp = read_uint16(emu, start_addr + 0x22);
//   pkmn->status = read_mem(emu, start_addr + 0x04);
//   pkmn->level = read_mem(emu, start_addr + 0x21);
//   pkmn->attack = read_uint16(emu, start_addr + 0x24);
//   pkmn->defense = read_uint16(emu, start_addr + 0x26);
//   pkmn->speed = read_uint16(emu, start_addr + 0x28);
//   pkmn->special = read_uint16(emu, start_addr + 0x2A);
// }
void update_ram(mGBA *env) {
    env->ram.x = read_mem(&env->emu, PKMN_X_ADDR);
    env->ram.y = read_mem(&env->emu, PKMN_Y_ADDR);
    env->ram.map_n = read_mem(&env->emu, PKMN_MAP_ADDR);
    env->ram.badges = read_mem(&env->emu, PKMN_BADGES_ADDR);
    env->ram.money = read_bcd(&env->emu, PKMN_MONEY_ADDR);
    env->ram.party_count = read_mem(&env->emu, PKMN_PARTY_COUNT_ADDR);
    env->ram.pkmn1_lvl = read_mem(&env->emu, PKM_LEVEL_ADDR_1);
    env->ram.pkmn2_lvl = read_mem(&env->emu, PKM_LEVEL_ADDR_2);
    env->ram.pkmn3_lvl = read_mem(&env->emu, PKM_LEVEL_ADDR_3);
    env->ram.pkmn4_lvl = read_mem(&env->emu, PKM_LEVEL_ADDR_4);
    env->ram.pkmn5_lvl = read_mem(&env->emu, PKM_LEVEL_ADDR_5);
    env->ram.pkmn6_lvl = read_mem(&env->emu, PKM_LEVEL_ADDR_6);
}
int calc_level_sum(RamState *ram) {
  int level_sum = 0;
  level_sum += ram->pkmn1_lvl;
  level_sum += ram->pkmn2_lvl;
  level_sum += ram->pkmn3_lvl;
  level_sum += ram->pkmn4_lvl;
  level_sum += ram->pkmn5_lvl;
  level_sum += ram->pkmn6_lvl;
  return level_sum;
}
static float calculate_rewards(mGBA *env) {
  float reward = 0.0f;
  update_ram(env);
  RamState *ram = &env->ram;
  RamState *prev_ram = &env->prev_ram;
  uint32_t idx = coord_index(ram->map_n, ram->x, ram->y);
  int level_sum = calc_level_sum(ram);
  int prev_level_sum = calc_level_sum(prev_ram);

  if (ram->badges > prev_ram->badges) {
    reward += REWARD_BADGE;
    printf("You beat a gym! Badge count: %d\n", ram->badges);
  }

  if (ram->party_count > prev_ram->party_count && ram->party_count <= 6) {
    reward += REWARD_POKEMON;
    printf("You caught a new Pokemon! Party count: %d\n", ram->party_count);
  }

  // if (ram->map_n != prev_ram->map_n) {
  //   reward += REWARD_MAP;
  // }

  if (!is_coord_visited(env, idx)) {
    mark_coord_visited(env, idx);
    env->unique_coords_count++;
    reward += REWARD_UNIQUE_COORD; 
  }

  if (env->prev_visited_coords[idx] == 0) {
    reward += REWARD_UNIQUE_COORD; // fake memory?
    env->prev_visited_coords[idx] = 1;
  }
  if (level_sum > prev_level_sum && ram->party_count > prev_ram->party_count) {
    reward += REWARD_LEVEL;
  }

  // add a func for events.h


  env->prev_ram = env->ram;
  return reward;
}

void c_reset(mGBA *env) {
  if (!env || !env->emu.core)
    return;
  if (env->full_reset) {
    initial_load_state(&env->emu, env->emu.state_path);
  }

  update_ram(env);
  env->prev_ram = env->ram;
  update_observations(env);
  clear_visited_coords(env);
  uint32_t idx = coord_index(env->ram.map_n, env->ram.x, env->ram.y);
  mark_coord_visited(env, idx);

  env->rewards[0] = 0;
  env->terminals[0] = 0;
  env->step_count = env->frame_count = 0;
  env->score = 0.0f;
  env->stagnation = 0;
  env->unique_coords_count = 1;

  for (int i = 0; i < 4; i++)
    env->emu.core->runFrame(env->emu.core);

}
void c_step(mGBA *env) {
  if (!env || !env->emu.core)
    return;

  env->rewards[0] = 0;
  env->terminals[0] = 0;
  env->step_count++;

  set_keys(&env->emu, action_to_key(env->actions[0]));
  int skip = env->emu.frame_skip > 0 ? env->emu.frame_skip : 1;
  for (int i = 0; i < skip; i++) {
    env->emu.core->runFrame(env->emu.core);
    env->frame_count++;
  }
  set_keys(&env->emu, 0);
  float reward = calculate_rewards(env);

  update_observations(env);
  env->rewards[0] = reward;
  env->score += reward;

  if (env->step_count >= env->max_episode_length) { // || env->stagnation > STAGNATION_LIMIT
    env->terminals[0] = 1;
    add_log(env);
    env->prev_visited_coords = env->visited_coords;
    c_reset(env);
  }
}
void c_render(mGBA *env) { (void)env; }
void c_close(mGBA *env) {
  if (!env)
    return;

  if (env->emu.core) {
    env->emu.core->setVideoBuffer(env->emu.core, NULL, 0);
    mCoreConfigDeinit(&env->emu.core->config);
    env->emu.core->deinit(env->emu.core);
    env->emu.core = NULL;
  }

  if (env->emu.video_buffer) {
    free(env->emu.video_buffer);
    env->emu.video_buffer = NULL;
  }
}



#endif // MGBA_ENV_H
