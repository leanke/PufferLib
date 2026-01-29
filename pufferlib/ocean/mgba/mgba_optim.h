// mgba_optim.h - Optimization macros for mGBA RL environment
// opus got me scared here :(
#ifndef MGBA_OPTIM_H
#define MGBA_OPTIM_H

#include <stdint.h>
#include <stdbool.h>

// Branch prediction hints
#ifndef LIKELY
  #ifdef __GNUC__
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
  #else
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
  #endif
#endif

// Force inline and path hints
#ifdef __GNUC__
  #define FORCE_INLINE __attribute__((always_inline)) inline
  #define HOT_PATH     __attribute__((hot))
  #define COLD_PATH    __attribute__((cold))
  #define RESTRICT     __restrict__
#else
  #define FORCE_INLINE inline
  #define HOT_PATH
  #define COLD_PATH
  #define RESTRICT
#endif

// Cache prefetch hints
#ifdef __GNUC__
  #define PREFETCH_READ(ptr)  __builtin_prefetch((ptr), 0, 3)
  #define PREFETCH_WRITE(ptr) __builtin_prefetch((ptr), 1, 3)
  #define PREFETCH_NTA(ptr)   __builtin_prefetch((ptr), 0, 0)
#else
  #define PREFETCH_READ(ptr)  ((void)0)
  #define PREFETCH_WRITE(ptr) ((void)0)
  #define PREFETCH_NTA(ptr)   ((void)0)
#endif

// Thread-local storage
#ifdef __GNUC__
  #define THREAD_LOCAL __thread
#elif defined(_MSC_VER)
  #define THREAD_LOCAL __declspec(thread)
#else
  #define THREAD_LOCAL
#endif

// Execute N frames with same key held (reduces Python-C boundary crossings)
#define STEP_N_FRAMES(core, keys, n) do {           \
    struct mCore* _core = (core);                   \
    if (LIKELY(_core != NULL)) {                    \
        _core->setKeys(_core, (keys) & 0xFF);       \
        for (int _i = 0; _i < (n); _i++) {          \
            _core->runFrame(_core);                 \
        }                                           \
    }                                               \
} while(0)

// Execute N frames with per-frame action array
#define STEP_N_FRAMES_VARIED(core, actions, n) do { \
    struct mCore* _core = (core);                   \
    if (LIKELY(_core != NULL)) {                    \
        for (int _i = 0; _i < (n); _i++) {          \
            _core->setKeys(_core, (actions)[_i]);   \
            _core->runFrame(_core);                 \
        }                                           \
    }                                               \
} while(0)

// Shared ROM stubs (disabled - mmap issues with Python multiprocessing)
static inline void release_shared_rom(void) {}
static inline void* get_shared_rom(void) { return NULL; }
static inline size_t get_shared_rom_size(void) { return 0; }

// Configure core for headless RL mode (disables audio)
static inline void configure_headless_mode(struct mCore* core) {
    if (UNLIKELY(!core)) return;
    core->setAudioBufferSize(core, 0);
    mCoreConfigSetValue(&core->config, "audio.quality", "0");
    mCoreConfigSetValue(&core->config, "audio.volume", "0");
}

// Memory alignment
#define CACHE_LINE_SIZE 64
#define SIMD_ALIGNMENT  32

#ifdef __GNUC__
  #define ALIGNED(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
  #define ALIGNED(n) __declspec(align(n))
#else
  #define ALIGNED(n)
#endif

static inline void* aligned_malloc(size_t size, size_t alignment) {
    void* ptr = NULL;
#if defined(_MSC_VER)
    ptr = _aligned_malloc(size, alignment);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    ptr = aligned_alloc(alignment, ((size + alignment - 1) / alignment) * alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) ptr = NULL;
#endif
    return ptr;
}

static inline void aligned_free(void* ptr) {
#if defined(_MSC_VER)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// Performance counters (debug only, enable with -DENABLE_PERF_COUNTERS)
#ifdef ENABLE_PERF_COUNTERS
  #include <time.h>
  
  typedef struct {
      uint64_t step_count;
      double total_step_time;
      double total_obs_time;
      double last_fps;
  } PerfCounters;
  
  static THREAD_LOCAL PerfCounters g_perf = {0};
  
  #define PERF_START(name) \
      struct timespec _perf_start_##name; \
      clock_gettime(CLOCK_MONOTONIC, &_perf_start_##name)
  
  #define PERF_END(name, counter) do { \
      struct timespec _perf_end; \
      clock_gettime(CLOCK_MONOTONIC, &_perf_end); \
      double _elapsed = (_perf_end.tv_sec - _perf_start_##name.tv_sec) + \
                       (_perf_end.tv_nsec - _perf_start_##name.tv_nsec) / 1e9; \
      g_perf.counter += _elapsed; \
  } while(0)
  
  #define PERF_INC_STEP() (g_perf.step_count++)
#else
  #define PERF_START(name)        ((void)0)
  #define PERF_END(name, counter) ((void)0)
  #define PERF_INC_STEP()         ((void)0)
#endif

#endif // MGBA_OPTIM_H
