#ifndef MGBA_CORE_H
#define MGBA_CORE_H

#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mgba-util/vfs.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/gb/core.h>
#include <mgba/gb/interface.h>

typedef struct {
  struct mCore *core;
  color_t *video_buffer;
  char rom_path[256];
  char state_path[256];
  int32_t frame_skip;
  bool render_enabled;
  bool uses_shared_rom;
} Emu;

#include "mgba_optim.h" // needed below Emu struct?

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



void mgba_init_core(Emu *env, const char *rom_path);
void initial_load_state(Emu *env, const char *state_path);
bool c_save_state_file(Emu *env, const char *path);
bool c_load_state_file(Emu *env, const char *path);

static void silent_log(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args) {
  (void)logger;
  (void)category;
  (void)level;
  (void)format;
  (void)args;
}
static struct mLogger s_silentLogger = {.log = silent_log, .filter = NULL};
static int g_stderr_backup = -1;
static int g_devnull_fd = -1;
static inline void suppress_stderr(void) {
  fflush(stderr);
  g_stderr_backup = dup(STDERR_FILENO);
  g_devnull_fd = open("/dev/null", O_WRONLY);
  if (g_devnull_fd >= 0) {
    dup2(g_devnull_fd, STDERR_FILENO);
  }
}
static inline void restore_stderr(void) {
  fflush(stderr);
  if (g_stderr_backup >= 0) {
    dup2(g_stderr_backup, STDERR_FILENO);
    close(g_stderr_backup);
    g_stderr_backup = -1;
  }
  if (g_devnull_fd >= 0) {
    close(g_devnull_fd);
    g_devnull_fd = -1;
  }
}

static inline uint32_t action_to_key(int action) {
  if (action <= 0 || action >= GB_ACTION_COUNT)
    return 0;
  return (1 << (action - 1));
}
static inline void set_keys(Emu *env, uint32_t action) {
  if (env && env->core)
    env->core->setKeys(env->core, action & 0xFF);
}
static inline uint8_t read_mem(Emu *env, uint16_t addr) {
  return env && env->core ? (uint8_t)env->core->rawRead8(env->core, addr, -1)
                          : 0;
}
static inline uint32_t read_bcd(Emu *env, uint16_t addr) {
  uint8_t h = read_mem(env, addr);
  uint8_t m = read_mem(env, addr + 1);
  uint8_t l = read_mem(env, addr + 2);
  return ((h >> 4) * 100000) + ((h & 0xF) * 10000) + ((m >> 4) * 1000) +
         ((m & 0xF) * 100) + ((l >> 4) * 10) + (l & 0xF);
}
static inline uint16_t read_uint16(Emu *env, uint16_t addr) {
  uint8_t low = read_mem(env, addr);
  uint8_t high = read_mem(env, addr + 1);
  return (uint16_t)(low | (high << 8));
}


void mgba_init_core(Emu *env, const char *rom_path) {
  if (!env)
    return;

  env->uses_shared_rom = false;

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
  
  configure_headless_mode(env->core);
  
  env->core->reset(env->core);
  strncpy(env->rom_path, rom_path, sizeof(env->rom_path) - 1);
}
bool c_save_state_file(Emu *env, const char *path) {
  if (!env || !env->core || !path)
    return false;
  struct VFile *vf = VFileOpen(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!vf)
    return false;
  bool result = mCoreSaveStateNamed(env->core, vf, SAVESTATE_ALL);
  vf->close(vf);
  return result;
}
bool c_load_state_file(Emu *env, const char *path) {
  if (!env || !env->core || !path)
    return false;
  struct VFile *vf = VFileOpen(path, O_RDONLY);
  if (!vf)
    return false;
  suppress_stderr();  // Silence libpng warnings
  bool result = mCoreLoadStateNamed(env->core, vf, SAVESTATE_ALL);
  restore_stderr();
  vf->close(vf);
  return result;
}
void initial_load_state(Emu *env, const char *state_path) {
  struct VFile *vf = VFileOpen(state_path, O_RDONLY);
  if (vf) {
    suppress_stderr();
    bool load_success = mCoreLoadStateNamed(env->core, vf, SAVESTATE_ALL);
    restore_stderr();
    vf->close(vf);
    if (!load_success) {
      fprintf(stderr, "Warning: Failed to load state from file: %s\n", state_path);
    }
  } else {
    fprintf(stderr, "Warning: Could not open state file: %s\n", state_path);
  }
}

#endif // MGBA_CORE_H