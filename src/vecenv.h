// vecenv.h - Static env binding: types and declarations.
// Implementation is in vecenv.c (compiled separately into _core.so).
// Environments no longer include this file — they include pufferenv.h only.

#pragma once

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "tensor.h"
#include "pufferenv.h"

// Dict types (layout-identical to PufferDict/PufferDictItem in pufferenv.h)
typedef PufferDictItem DictItem;
typedef PufferDict Dict;

static inline Dict* create_dict(int capacity) {
    Dict* dict = (Dict*)calloc(1, sizeof(Dict));
    dict->capacity = capacity;
    dict->items = (DictItem*)calloc(capacity, sizeof(DictItem));
    return dict;
}

static inline DictItem* dict_get_unsafe(Dict* dict, const char* key) {
    for (int i = 0; i < dict->size; i++) {
        if (strcmp(dict->items[i].key, key) == 0) {
            return &dict->items[i];
        }
    }
    return NULL;
}

static inline DictItem* dict_get(Dict* dict, const char* key) {
    DictItem* item = dict_get_unsafe(dict, key);
    if (item == NULL) printf("dict_get failed to find key: %s\n", key);
    assert(item != NULL);
    return item;
}

static inline void dict_set(Dict* dict, const char* key, double value) {
    assert(dict->size < dict->capacity);
    DictItem* item = dict_get_unsafe(dict, key);
    if (item != NULL) {
        item->value = value;
        return;
    }
    dict->items[dict->size].key = key;
    dict->items[dict->size].value = value;
    dict->size++;
}

// Forward declare CUDA stream type
typedef struct CUstream_st* cudaStream_t;

// CUDA type declarations and stubs for non-CUDA compilation
#ifndef __CUDACC__
typedef int cudaError_t;
typedef int cudaMemcpyKind;
#define cudaSuccess 0
#define cudaMemcpyHostToDevice 1
#define cudaMemcpyDeviceToHost 2
#define cudaHostAllocPortable 1
#define cudaStreamNonBlocking 1

extern cudaError_t cudaHostAlloc(void**, size_t, unsigned int);
extern cudaError_t cudaMalloc(void**, size_t);
extern cudaError_t cudaMemcpy(void*, const void*, size_t, cudaMemcpyKind);
extern cudaError_t cudaMemcpyAsync(void*, const void*, size_t, cudaMemcpyKind, cudaStream_t);
extern cudaError_t cudaMemset(void*, int, size_t);
extern cudaError_t cudaFree(void*);
extern cudaError_t cudaFreeHost(void*);
extern cudaError_t cudaSetDevice(int);
extern cudaError_t cudaDeviceSynchronize(void);
extern cudaError_t cudaStreamSynchronize(cudaStream_t);
extern cudaError_t cudaStreamCreateWithFlags(cudaStream_t*, unsigned int);
extern cudaError_t cudaStreamQuery(cudaStream_t);
extern const char* cudaGetErrorString(cudaError_t);
#endif

// Threading state (defined in vecenv.c)
typedef struct StaticThreading StaticThreading;

// Generic VecEnv — uses PufferEnvVTable for all env-specific dispatch
typedef struct StaticVec {
    PufferEnvVTable*  vtable;            // loaded from env plugin
    PufferEnvHandle*  handles;           // array of per-env opaque handles
    int size;
    int total_agents;
    int buffers;
    int agents_per_buffer;
    int* buffer_env_starts;
    int* buffer_env_counts;
    void*          observations;
    float*         actions;
    float*         rewards;
    float*         terminals;
    unsigned char* action_mask;          // NULL unless env uses action masks
    void*          gpu_observations;
    float*         gpu_actions;
    float*         gpu_rewards;
    float*         gpu_terminals;
    unsigned char* gpu_action_mask;      // NULL unless env uses action masks
    cudaStream_t*  streams;
    StaticThreading* threading;
    int obs_size;
    int num_atns;
    int action_mask_size;                // 0 unless env uses action masks
    size_t obs_elem_size;               // sizeof per-element (float=4, uint8=1, bf16=2)
    PufferEnvDtype obs_dtype;           // runtime dtype tag
    int gpu;
    int* agent_perm;  // NULL = identity. Only valid when vtable->uses_perm() returns 1.
} StaticVec;

// Callback types
typedef void (*net_callback_fn)(void* ctx, int buf, int t);
typedef void (*thread_init_fn)(void* ctx, int buf);
typedef void (*step_fn)(void* env);

enum EvalProfileIdx {
    EVAL_GPU = 0,
    EVAL_ENV_STEP,
    NUM_EVAL_PROF,
};

// Create vectorized env set. vtable must outlive the returned StaticVec.
StaticVec* create_static_vec(int total_agents, int num_buffers, int gpu,
    Dict* vec_kwargs, Dict* env_kwargs, PufferEnvVTable* vtable);

void static_vec_reset(StaticVec* vec);
void static_vec_close(StaticVec* vec);
void static_vec_log(StaticVec* vec, Dict* out);
void static_vec_eval_log(StaticVec* vec, Dict* out);
void create_static_threads(StaticVec* vec, int num_threads, int horizon,
    void* ctx, net_callback_fn net_callback, thread_init_fn thread_init);
void static_vec_omp_step(StaticVec* vec);
void static_vec_seq_step(StaticVec* vec);
void static_vec_render(StaticVec* vec, int env_id);
void static_vec_read_profile(StaticVec* vec, float out[NUM_EVAL_PROF]);

void static_vec_step(StaticVec* vec);
void gpu_vec_step(StaticVec* vec);
void cpu_vec_step(StaticVec* vec);

// Optional permutation: re-maps per-slot pointers via vtable->setup_perm.
// Only valid when vtable->uses_perm() returns 1; otherwise no-op.
void static_vec_set_perm(StaticVec* vec, const int* perm);

// Optional per-env tagging + boundary tracking for selfplay-pool curricula.
// Only valid when vtable->uses_tags() returns 1; otherwise no-op.
void static_vec_set_env_tags(StaticVec* vec, const int* tags);
int static_vec_count_aligned(StaticVec* vec, int tag_value, int reset_flags);

#ifdef __cplusplus
}
#endif
