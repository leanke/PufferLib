#ifndef GAMBATTE_WRAPPER_H
#define GAMBATTE_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#include "gambatte_c.h"

typedef uint32_t color_t;

#define GB_LOAD_FORCE_DMG  1

#define GB_SCREEN_WIDTH   160
#define GB_SCREEN_HEIGHT  144
#define GB_VIDEO_PITCH    256

#ifndef LIKELY
  #ifdef __GNUC__
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
  #else
    #define LIKELY(x)   (x)
  #endif
#endif

#ifdef __GNUC__
  #define PREFETCH_READ(ptr)  __builtin_prefetch((ptr), 0, 3)
  #define PREFETCH_WRITE(ptr) __builtin_prefetch((ptr), 1, 3)
#else
  #define PREFETCH_READ(ptr)  ((void)0)
  #define PREFETCH_WRITE(ptr) ((void)0)
#endif

#define STEP_ACTION_FRAMES(gb, keys, vbuf, press_n, total_n) do {  \
    gambatte_handle _gb = (gb);                                    \
    int _press = (press_n);                                        \
    int _total = (total_n);                                        \
    if (_press > _total) _press = _total;                          \
    if (LIKELY(_gb != NULL)) {                                     \
        gambatte_set_input(_gb, (keys) & 0xFF);                    \
        for (int _i = 0; _i < _press; _i++)                       \
            gambatte_run_frame(_gb, (vbuf));                       \
        gambatte_set_input(_gb, 0);                                \
        for (int _i = _press; _i < _total; _i++)                  \
            gambatte_run_frame(_gb, (vbuf));                       \
    }                                                              \
} while(0)

static void  *g_shared_rom_data = NULL;
static size_t g_shared_rom_size = 0;
static int    g_shared_rom_refs = 0;

static inline bool acquire_shared_rom(const char *path) {
    if (g_shared_rom_data) {
        g_shared_rom_refs++;
        return true;
    }
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    struct stat st;
    if (fstat(fileno(f), &st) != 0) { fclose(f); return false; }
    g_shared_rom_size = (size_t)st.st_size;
    g_shared_rom_data = malloc(g_shared_rom_size);
    if (!g_shared_rom_data) { fclose(f); return false; }
    if (fread(g_shared_rom_data, 1, g_shared_rom_size, f) != g_shared_rom_size) {
        free(g_shared_rom_data);
        g_shared_rom_data = NULL;
        g_shared_rom_size = 0;
        fclose(f);
        return false;
    }
    fclose(f);
    g_shared_rom_refs = 1;
    return true;
}

static inline void *get_shared_rom(void)       { return g_shared_rom_data; }
static inline size_t get_shared_rom_size(void)  { return g_shared_rom_size; }

static inline void release_shared_rom(void) {
    if (g_shared_rom_refs > 0) g_shared_rom_refs--;
    if (g_shared_rom_refs == 0 && g_shared_rom_data) {
        free(g_shared_rom_data);
        g_shared_rom_data = NULL;
        g_shared_rom_size = 0;
    }
}

typedef enum {
  GB_ACTION_A = 0,
  GB_ACTION_B,
  GB_ACTION_SELECT,
  GB_ACTION_START,
  GB_ACTION_RIGHT,
  GB_ACTION_LEFT,
  GB_ACTION_UP,
  GB_ACTION_DOWN,
  GB_ACTION_COUNT
} GBAction;

typedef struct {
  gambatte_handle   gb;
  color_t          *video_buffer;
  char              rom_path[256];
  char              state_path[256];
  int32_t           frame_skip;
  int32_t           press_frames;
  bool              render_enabled;
  bool              uses_shared_rom;

  uint8_t          *pool_state_buf;
  uint8_t          *pool_initial_state_buf;
  int               pool_worker_idx;
} Emulator;

static void gb_init_core(Emulator *env, const char *rom_path);
static inline bool c_save_state_file(Emulator *env, const char *path);

static inline uint32_t action_to_key(int action) {
  if (action < 0 || action >= GB_ACTION_COUNT)
    return 0;
  return (1 << action);
}

static inline uint8_t read_mem(Emulator *env, uint16_t addr) {
  return (env && env->gb) ? gambatte_read_mem(env->gb, addr) : 0;
}

static inline uint32_t read_bcd(Emulator *env, uint16_t addr) {
  uint8_t h = read_mem(env, addr);
  uint8_t m = read_mem(env, addr + 1);
  uint8_t l = read_mem(env, addr + 2);
  return ((h >> 4) * 100000) + ((h & 0xF) * 10000) + ((m >> 4) * 1000) +
         ((m & 0xF) * 100) + ((l >> 4) * 10) + (l & 0xF);
}

static inline void write_mem(Emulator *env, uint16_t addr, uint8_t value) {
  if (env && env->gb)
    gambatte_write_mem(env->gb, addr, value);
}

static void gb_init_core(Emulator *env, const char *rom_path) {
  if (!env) return;

  env->uses_shared_rom = false;

  env->gb = gambatte_create();
  if (!env->gb) {
    fprintf(stderr, "Failed to create Gambatte instance\n");
    return;
  }

  bool rom_loaded = false;
  if (acquire_shared_rom(rom_path)) {
    if (gambatte_load(env->gb, get_shared_rom(),
                      (unsigned)get_shared_rom_size(),
                      GB_LOAD_FORCE_DMG) == 0) {
      env->uses_shared_rom = true;
      rom_loaded = true;
    } else {
      release_shared_rom();
    }
  }
  if (!rom_loaded) {
    FILE *f = fopen(rom_path, "rb");
    if (!f) {
      fprintf(stderr, "Failed to open ROM: %s\n", rom_path);
      gambatte_destroy(env->gb);
      env->gb = NULL;
      return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
      fclose(f);
      gambatte_destroy(env->gb);
      env->gb = NULL;
      return;
    }
    void *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
      free(buf);
      fclose(f);
      gambatte_destroy(env->gb);
      env->gb = NULL;
      return;
    }
    fclose(f);
    if (gambatte_load(env->gb, buf, (unsigned)sz, GB_LOAD_FORCE_DMG) != 0) {
      fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
      free(buf);
      gambatte_destroy(env->gb);
      env->gb = NULL;
      return;
    }
    free(buf);
  }

  env->video_buffer =
      (color_t *)calloc(GB_VIDEO_PITCH * GB_SCREEN_HEIGHT, sizeof(color_t));

  strncpy(env->rom_path, rom_path, sizeof(env->rom_path) - 1);
}

static inline bool c_save_state_file(Emulator *env, const char *path) {
  if (!env || !env->gb || !path) return false;
  return gambatte_save_state_file(env->gb, path);
}

typedef struct {
    gambatte_handle *workers;
    bool            *busy;
    int              size;
    size_t           state_size;
    pthread_mutex_t  lock;
    pthread_cond_t   cond;
} GBPool;

static GBPool g_gb_pool = {0};

static void gb_pool_init(int size, const char *rom_path) {
  if (g_gb_pool.workers) return;
    if (size < 1) size = 1;

    pthread_mutex_init(&g_gb_pool.lock, NULL);
    pthread_cond_init(&g_gb_pool.cond, NULL);
    g_gb_pool.size = size;
    g_gb_pool.workers = (gambatte_handle *)calloc((size_t)size, sizeof(gambatte_handle));
    g_gb_pool.busy = (bool *)calloc((size_t)size, sizeof(bool));

    for (int i = 0; i < size; i++) {
        Emulator tmp = {0};
        gb_init_core(&tmp, rom_path);
        if (!tmp.gb) {
            fprintf(stderr, "pokered: pool worker %d failed to init Gambatte core\n", i);
            exit(1);
        }
        g_gb_pool.workers[i] = tmp.gb;
        free(tmp.video_buffer);
    }
    g_gb_pool.state_size = gambatte_state_size_raw(g_gb_pool.workers[0]);
}

static int gb_pool_acquire(void) {
    pthread_mutex_lock(&g_gb_pool.lock);
    int idx = -1;
    while (idx < 0) {
        for (int i = 0; i < g_gb_pool.size; i++) {
            if (!g_gb_pool.busy[i]) { idx = i; break; }
        }
        if (idx < 0) pthread_cond_wait(&g_gb_pool.cond, &g_gb_pool.lock);
    }
    g_gb_pool.busy[idx] = true;
    pthread_mutex_unlock(&g_gb_pool.lock);
    return idx;
}

static void gb_pool_release(int idx) {
    pthread_mutex_lock(&g_gb_pool.lock);
    g_gb_pool.busy[idx] = false;
    pthread_cond_signal(&g_gb_pool.cond);
    pthread_mutex_unlock(&g_gb_pool.lock);
}

static inline void gb_pool_acquire_for(Emulator *emu) {
    emu->pool_worker_idx = gb_pool_acquire();
    emu->gb = g_gb_pool.workers[emu->pool_worker_idx];
    gambatte_load_state_raw(emu->gb, emu->pool_state_buf);
}

static inline void gb_pool_release_for(Emulator *emu) {
    gambatte_save_state_raw(emu->gb, emu->pool_state_buf);
    gb_pool_release(emu->pool_worker_idx);
    emu->gb = NULL;
}

static inline void gb_pool_load_initial_state(Emulator *emu, const char *state_path) {
    int idx = gb_pool_acquire();
    gambatte_handle worker = g_gb_pool.workers[idx];
    if (!gambatte_load_state_file(worker, state_path)) {
        fprintf(stderr, "Warning: Failed to load state file: %s\n", state_path);
    }
    gambatte_save_state_raw(worker, emu->pool_initial_state_buf);
    memcpy(emu->pool_state_buf, emu->pool_initial_state_buf, g_gb_pool.state_size);
    gb_pool_release(idx);
}

#endif
