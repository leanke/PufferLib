// pufferenv.h — Stable C ABI for PufferLib plugin environments.
// Version 1. This is the ONLY PufferLib header environments need to include.
// No vecenv.h, no tensor.h, no CUDA headers required.
//
// Environments compile to standalone .so files exposing pufferenv_get_vtable().
// PufferLib loads them at runtime via dlopen and dispatches through the vtable.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#define PUFFERENV_ABI_VERSION 1

// ---- Observation dtype tag ----
typedef enum {
    PUFFERENV_DTYPE_FLOAT32 = 0,  // float*     (FloatTensor / float PrecisionTensor)
    PUFFERENV_DTYPE_UINT8   = 1,  // uint8_t*   (ByteTensor)
    PUFFERENV_DTYPE_FLOAT16 = 2,  // uint16_t*  (BF16 PrecisionTensor)
} PufferEnvDtype;

// ---- Dict: layout-identical to Dict/DictItem in vecenv.h ----
// PufferLib casts its internal Dict* directly — no copy.
typedef struct {
    const char* key;
    double value;
    void* ptr;
} PufferDictItem;

typedef struct {
    PufferDictItem* items;
    int size;
    int capacity;
} PufferDict;

static inline double pufferenv_get(const PufferDict* d, const char* key) {
    for (int i = 0; i < d->size; i++) {
        if (strcmp(d->items[i].key, key) == 0) return d->items[i].value;
    }
    printf("pufferenv_get: key not found: %s\n", key);
    assert(0);
    return 0.0;
}

// key must be a string literal — stored as pointer, not copied (same as vecenv.h dict_set)
static inline void pufferenv_dict_set(PufferDict* d, const char* key, double value) {
    assert(d->size < d->capacity);
    for (int i = 0; i < d->size; i++) {
        if (strcmp(d->items[i].key, key) == 0) {
            d->items[i].value = value;
            return;
        }
    }
    d->items[d->size].key = key;
    d->items[d->size].value = value;
    d->size++;
}

// ---- Per-env slot: pointers into PufferLib's pinned/host buffers ----
typedef struct {
    void*          observations;  // typed pointer (float*, uint8_t*, or uint16_t*)
    float*         actions;
    float*         rewards;
    float*         terminals;
    unsigned char* action_mask;   // NULL if action_mask_size == 0
} PufferEnvSlot;

// ---- Opaque per-env handle (env's Env* cast to void*) ----
typedef void* PufferEnvHandle;

// ---- The vtable every plugin must export ----
typedef struct {
    uint32_t abi_version;  // must == PUFFERENV_ABI_VERSION

    // Metadata — called once after dlopen to query env properties
    int            (*get_obs_size)        (void);
    int            (*get_num_atns)        (void);
    const int*     (*get_act_sizes)       (void);
    int            (*get_num_act_sizes)   (void);
    PufferEnvDtype (*get_obs_dtype)       (void);
    size_t         (*get_obs_elem_size)   (void);
    int            (*get_action_mask_size)(void);  // 0 if unused
    int            (*uses_perm)           (void);  // 1 if env uses agent permutation
    int            (*uses_tags)           (void);  // 1 if env uses selfplay tags

    // Batch lifecycle — allocate/free all envs for a vectorized set.
    // vec_init allocates num_envs env instances, fills buffer partition arrays,
    // returns an array of num_envs opaque handles.
    // Buffer arrays (buf_starts, buf_counts) are pre-allocated by PufferLib.
    PufferEnvHandle* (*vec_init)(
        int* num_envs_out,
        int* buf_starts,
        int* buf_counts,
        const PufferDict* vec_kwargs,
        const PufferDict* env_kwargs);

    // vec_close: env-global cleanup + free env data + free handles array.
    void (*vec_close)(PufferEnvHandle* handles, int num_envs);

    // Per-env lifecycle
    void (*env_init)      (PufferEnvHandle h, const PufferDict* kwargs);
    int  (*env_num_agents)(PufferEnvHandle h);
    void (*env_step)      (PufferEnvHandle h);
    void (*env_reset)     (PufferEnvHandle h);
    void (*env_render)    (PufferEnvHandle h);
    void (*env_close)     (PufferEnvHandle h);

    // Inject buffer pointers into env (called after vec_init, before first reset)
    void (*set_slot)(PufferEnvHandle h, const PufferEnvSlot* slot);

    // Optional: custom vectorized step (replaces default OMP env_step loop)
    // NULL = use default: #pragma omp parallel for over env_step
    void (*vec_step)(PufferEnvHandle* handles, int num_envs);

    // Optional: custom step for a range (used in threaded rolling window)
    // NULL = use default: OMP loop over env_step for [env_start, env_start+env_count)
    void (*vec_step_range)(PufferEnvHandle* handles, int env_start, int env_count, int num_workers);

    // Optional: chess-style per-slot pointer arrays (NULL if uses_perm() == 0)
    // Called when agent permutation changes; env re-maps its internal slot pointers.
    void (*setup_perm)(PufferEnvHandle h, const int* global_perm,
                       int slot_base, int num_slots,
                       void* obs_base, size_t obs_elem_stride,
                       float* act_base, float* rew_base,
                       float* term_base, unsigned char* mask_base);

    // Logging: expose per-env log block and aggregation.
    // Log struct is a flat array of float, last field is float n (sample count).
    void* (*get_log_ptr)       (PufferEnvHandle h);  // returns &env->log
    int   (*get_log_num_fields)(void);               // sizeof(Log) / sizeof(float)
    // log_export: called with averaged log values; writes to PufferDict out.
    // Equivalent to old my_log(). agg_log is a float[] of size get_log_num_fields().
    void  (*log_export)        (const void* agg_log, PufferDict* out);

    // Optional: selfplay tags (NULL if uses_tags() == 0)
    void (*set_tag)             (PufferEnvHandle h, int tag);
    int  (*get_tag)             (PufferEnvHandle h);
    int  (*get_boundary_reached)(PufferEnvHandle h);
    void (*set_boundary_reached)(PufferEnvHandle h, int val);

    // Optional: shared state (replaces MY_SHARED / MY_SHARED_CLOSE)
    void* (*shared)      (PufferEnvHandle h, PufferDict* kwargs);
    void  (*shared_close)(PufferEnvHandle h);
    void* (*get)         (PufferEnvHandle h, PufferDict* out);
    int   (*put)         (PufferEnvHandle h, PufferDict* kwargs);

} PufferEnvVTable;

// Every environment .so must export this symbol:
//   PufferEnvVTable* pufferenv_get_vtable(void);

// ============================================================================
// Helpers for writing vec_init_impl (standard allocation pattern)
// ============================================================================

// Build handles array from a contiguous env block after env_init loop.
// Updates handles in-place after potential realloc of envs.
// Returns the new base pointer (result of realloc).
static inline void* pufferenv_realloc_envs(
        void* envs, size_t env_struct_size, int num_envs,
        PufferEnvHandle* handles) {
    void* new_envs = realloc(envs, (size_t)num_envs * env_struct_size);
    for (int i = 0; i < num_envs; i++)
        handles[i] = (char*)new_envs + (size_t)i * env_struct_size;
    return new_envs;
}

// Fill buf_starts and buf_counts given the agent count per env.
// agents_per_env_fn(handle) returns the num_agents for that env.
static inline void pufferenv_fill_partitions(
        PufferEnvHandle* handles, int num_envs,
        int (*agents_per_env_fn)(PufferEnvHandle),
        int total_agents, int num_buffers,
        int* buf_starts, int* buf_counts) {
    int agents_per_buffer = total_agents / num_buffers;
    int buf = 0, buf_agents = 0;
    buf_starts[0] = 0;
    buf_counts[0] = 0;
    for (int i = 0; i < num_envs; i++) {
        buf_agents += agents_per_env_fn(handles[i]);
        buf_counts[buf]++;
        if (buf_agents >= agents_per_buffer && buf < num_buffers - 1) {
            buf++;
            buf_starts[buf] = i + 1;
            buf_counts[buf] = 0;
            buf_agents = 0;
        }
    }
}
