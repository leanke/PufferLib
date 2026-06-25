// vecenv.c — StaticVec implementation using PufferEnvVTable (runtime dispatch).
// Included (via #include "vecenv.c") from bindings.cu (nvcc) and bindings_cpu.cpp (g++).
// Never compiled standalone: it relies on CUDA types being available from the including TU.

#ifndef VECENV_C_INCLUDED
#define VECENV_C_INCLUDED

#include "vecenv.h"
#include "pufferenv.h"

#include <omp.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

struct StaticThreading {
    atomic_int* buffer_states;
    atomic_int  shutdown;
    int         num_threads;
    int         num_buffers;
    pthread_t*  threads;
    float*      accum;  // [num_buffers * NUM_EVAL_PROF] per-buffer timing in ms
};

typedef struct {
    StaticVec*       vec;
    int              buf;
    int              horizon;
    void*            ctx;
    net_callback_fn  net_callback;
    thread_init_fn   thread_init;
} StaticOMPArg;

#define OMP_WAITING 5
#define OMP_RUNNING 6

// ---- Default vectorized step (OMP parallel over env handles) ----

static inline void _vec_env_step_range(StaticVec* vec, int env_start, int env_count, int num_workers) {
    PufferEnvVTable* vt = vec->vtable;
    if (vt->vec_step_range) {
        vt->vec_step_range(vec->handles, env_start, env_count, num_workers);
    } else {
        #pragma omp parallel for schedule(static) num_threads(num_workers)
        for (int i = env_start; i < env_start + env_count; i++) {
            vt->env_step(vec->handles[i]);
        }
    }
}

static inline void _static_vec_env_step(StaticVec* vec) {
    PufferEnvVTable* vt = vec->vtable;
    memset(vec->rewards,   0, (size_t)vec->total_agents * sizeof(float));
    memset(vec->terminals, 0, (size_t)vec->total_agents * sizeof(float));
    if (vt->vec_step) {
        vt->vec_step(vec->handles, vec->size);
    } else {
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < vec->size; i++) {
            vt->env_step(vec->handles[i]);
        }
    }
}

// ---- OMP thread manager ----

static void* static_omp_threadmanager(void* arg) {
    StaticOMPArg*    worker_arg = (StaticOMPArg*)arg;
    StaticVec*       vec        = worker_arg->vec;
    StaticThreading* threading  = vec->threading;
    int buf        = worker_arg->buf;
    int horizon    = worker_arg->horizon;
    void* ctx      = worker_arg->ctx;
    net_callback_fn  net_callback = worker_arg->net_callback;
    thread_init_fn   thread_init  = worker_arg->thread_init;

    if (thread_init != NULL) thread_init(ctx, buf);

    int agents_per_buffer = vec->agents_per_buffer;
    int agent_start       = buf * agents_per_buffer;
    int env_start         = vec->buffer_env_starts[buf];
    int env_count         = vec->buffer_env_counts[buf];
    atomic_int* buffer_states = threading->buffer_states;
    int num_workers = threading->num_threads / vec->buffers;
    if (num_workers < 1) num_workers = 1;

    size_t obs_stride = (size_t)vec->obs_size * vec->obs_elem_size;

    printf("Num workers: %d\n", num_workers);
    while (true) {
        while (atomic_load(&buffer_states[buf]) != OMP_RUNNING) {
            if (atomic_load(&threading->shutdown)) return NULL;
        }
        cudaStream_t stream = vec->streams[buf];
        float* my_accum = &threading->accum[buf * NUM_EVAL_PROF];
        struct timespec t0, t1;

        for (int t = 0; t < horizon; t++) {
            clock_gettime(CLOCK_MONOTONIC, &t0);
            net_callback(ctx, buf, t);

            cudaMemcpyAsync(
                vec->actions + (size_t)agent_start * vec->num_atns,
                vec->gpu_actions + (size_t)agent_start * vec->num_atns,
                (size_t)agents_per_buffer * vec->num_atns * sizeof(float),
                cudaMemcpyDeviceToHost, stream);
            cudaStreamSynchronize(stream);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            my_accum[EVAL_GPU] += (t1.tv_sec - t0.tv_sec) * 1000.0f
                                  + (t1.tv_nsec - t0.tv_nsec) / 1e6f;

            memset(vec->rewards   + agent_start, 0, (size_t)agents_per_buffer * sizeof(float));
            memset(vec->terminals + agent_start, 0, (size_t)agents_per_buffer * sizeof(float));
            clock_gettime(CLOCK_MONOTONIC, &t0);
            _vec_env_step_range(vec, env_start, env_count, num_workers);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            my_accum[EVAL_ENV_STEP] += (t1.tv_sec - t0.tv_sec) * 1000.0f
                                       + (t1.tv_nsec - t0.tv_nsec) / 1e6f;

            cudaMemcpyAsync(
                (char*)vec->gpu_observations + (size_t)agent_start * obs_stride,
                (char*)vec->observations     + (size_t)agent_start * obs_stride,
                (size_t)agents_per_buffer * obs_stride,
                cudaMemcpyHostToDevice, stream);
            cudaMemcpyAsync(
                vec->gpu_rewards   + agent_start,
                vec->rewards       + agent_start,
                (size_t)agents_per_buffer * sizeof(float),
                cudaMemcpyHostToDevice, stream);
            cudaMemcpyAsync(
                vec->gpu_terminals + agent_start,
                vec->terminals     + agent_start,
                (size_t)agents_per_buffer * sizeof(float),
                cudaMemcpyHostToDevice, stream);
            if (vec->action_mask_size > 0) {
                cudaMemcpyAsync(
                    vec->gpu_action_mask + (size_t)agent_start * vec->action_mask_size,
                    vec->action_mask     + (size_t)agent_start * vec->action_mask_size,
                    (size_t)agents_per_buffer * vec->action_mask_size * sizeof(unsigned char),
                    cudaMemcpyHostToDevice, stream);
            }
        }
        cudaStreamSynchronize(stream);
        atomic_store(&buffer_states[buf], OMP_WAITING);
    }
}

void static_vec_omp_step(StaticVec* vec) {
    StaticThreading* threading = vec->threading;
    for (int buf = 0; buf < vec->buffers; buf++)
        atomic_store(&threading->buffer_states[buf], OMP_RUNNING);
    for (int buf = 0; buf < vec->buffers; buf++)
        while (atomic_load(&threading->buffer_states[buf]) != OMP_WAITING) {}
}

void static_vec_seq_step(StaticVec* vec) {
    StaticThreading* threading = vec->threading;
    for (int buf = 0; buf < vec->buffers; buf++) {
        atomic_store(&threading->buffer_states[buf], OMP_RUNNING);
        while (atomic_load(&threading->buffer_states[buf]) != OMP_WAITING) {}
    }
}

// ---- create_static_vec ----

StaticVec* create_static_vec(int total_agents, int num_buffers, int gpu,
        Dict* vec_kwargs, Dict* env_kwargs, PufferEnvVTable* vtable) {
    StaticVec* vec = (StaticVec*)calloc(1, sizeof(StaticVec));
    vec->vtable        = vtable;
    vec->total_agents  = total_agents;
    vec->buffers       = num_buffers;
    vec->agents_per_buffer = total_agents / num_buffers;
    vec->obs_size      = vtable->get_obs_size();
    vec->num_atns      = vtable->get_num_atns();
    vec->obs_elem_size = vtable->get_obs_elem_size();
    vec->obs_dtype     = vtable->get_obs_dtype();
    vec->action_mask_size = vtable->get_action_mask_size();
    vec->gpu           = gpu;

    vec->buffer_env_starts = (int*)calloc(num_buffers, sizeof(int));
    vec->buffer_env_counts = (int*)calloc(num_buffers, sizeof(int));

    int num_envs = 0;
    vec->handles = vtable->vec_init(&num_envs, vec->buffer_env_starts,
                                    vec->buffer_env_counts, vec_kwargs, env_kwargs);
    vec->size = num_envs;

    size_t obs_stride = (size_t)vec->obs_size * vec->obs_elem_size;
    if (gpu) {
        cudaHostAlloc(&vec->observations, (size_t)total_agents * obs_stride,            cudaHostAllocPortable);
        cudaHostAlloc((void**)&vec->actions,   (size_t)total_agents * vec->num_atns * sizeof(float), cudaHostAllocPortable);
        cudaHostAlloc((void**)&vec->rewards,   (size_t)total_agents * sizeof(float),   cudaHostAllocPortable);
        cudaHostAlloc((void**)&vec->terminals, (size_t)total_agents * sizeof(float),   cudaHostAllocPortable);

        cudaMalloc(&vec->gpu_observations, (size_t)total_agents * obs_stride);
        cudaMalloc((void**)&vec->gpu_actions,   (size_t)total_agents * vec->num_atns * sizeof(float));
        cudaMalloc((void**)&vec->gpu_rewards,   (size_t)total_agents * sizeof(float));
        cudaMalloc((void**)&vec->gpu_terminals, (size_t)total_agents * sizeof(float));

        cudaMemset(vec->gpu_observations, 0, (size_t)total_agents * obs_stride);
        cudaMemset(vec->gpu_actions,      0, (size_t)total_agents * vec->num_atns * sizeof(float));
        cudaMemset(vec->gpu_rewards,      0, (size_t)total_agents * sizeof(float));
        cudaMemset(vec->gpu_terminals,    0, (size_t)total_agents * sizeof(float));
    } else {
        vec->observations = calloc((size_t)total_agents * vec->obs_size, vec->obs_elem_size);
        vec->actions      = (float*)calloc((size_t)total_agents * vec->num_atns, sizeof(float));
        vec->rewards      = (float*)calloc((size_t)total_agents, sizeof(float));
        vec->terminals    = (float*)calloc((size_t)total_agents, sizeof(float));
        vec->gpu_observations = vec->observations;
        vec->gpu_actions      = vec->actions;
        vec->gpu_rewards      = vec->rewards;
        vec->gpu_terminals    = vec->terminals;
    }

    if (vec->action_mask_size > 0) {
        size_t mask_bytes = (size_t)total_agents * vec->action_mask_size * sizeof(unsigned char);
        if (gpu) {
            cudaHostAlloc((void**)&vec->action_mask, mask_bytes, cudaHostAllocPortable);
            cudaMalloc((void**)&vec->gpu_action_mask, mask_bytes);
            cudaMemset(vec->gpu_action_mask, 0, mask_bytes);
        } else {
            vec->action_mask     = (unsigned char*)calloc((size_t)total_agents * vec->action_mask_size, sizeof(unsigned char));
            vec->gpu_action_mask = vec->action_mask;
        }
    }

    vec->streams = (cudaStream_t*)calloc(num_buffers, sizeof(cudaStream_t));

    // Assign buffer slots to each env handle
    for (int buf = 0; buf < num_buffers; buf++) {
        int buf_start = buf * vec->agents_per_buffer;
        int buf_agent = 0;
        int env_start = vec->buffer_env_starts[buf];
        int env_count = vec->buffer_env_counts[buf];

        for (int e = 0; e < env_count; e++) {
            PufferEnvHandle h = vec->handles[env_start + e];
            int slot = buf_start + buf_agent;
            PufferEnvSlot s = {
                .observations = (char*)vec->observations + (size_t)slot * obs_stride,
                .actions      = vec->actions  + (size_t)slot * vec->num_atns,
                .rewards      = vec->rewards  + slot,
                .terminals    = vec->terminals + slot,
                .action_mask  = (vec->action_mask && vec->action_mask_size > 0)
                                ? vec->action_mask + (size_t)slot * vec->action_mask_size
                                : NULL,
            };
            vtable->set_slot(h, &s);
            if (vtable->uses_perm()) {
                vtable->setup_perm(h, NULL, slot, vtable->env_num_agents(h),
                    vec->observations, obs_stride,
                    vec->actions, vec->rewards, vec->terminals, vec->action_mask);
            }
            buf_agent += vtable->env_num_agents(h);
        }
    }

    return vec;
}

void static_vec_set_perm(StaticVec* vec, const int* perm) {
    if (!vec->vtable->uses_perm()) {
        fprintf(stderr, "static_vec_set_perm: env did not opt in via uses_perm; ignoring.\n");
        return;
    }
    int N = vec->total_agents;
    if (vec->agent_perm == NULL)
        vec->agent_perm = (int*)malloc((size_t)N * sizeof(int));
    memcpy(vec->agent_perm, perm, (size_t)N * sizeof(int));

    size_t obs_stride = (size_t)vec->obs_size * vec->obs_elem_size;
    for (int buf = 0; buf < vec->buffers; buf++) {
        int buf_start = buf * vec->agents_per_buffer;
        int buf_agent = 0;
        int env_start = vec->buffer_env_starts[buf];
        int env_count = vec->buffer_env_counts[buf];
        for (int e = 0; e < env_count; e++) {
            PufferEnvHandle h = vec->handles[env_start + e];
            int slot_base = buf_start + buf_agent;
            int n = vec->vtable->env_num_agents(h);
            vec->vtable->setup_perm(h, vec->agent_perm, slot_base, n,
                vec->observations, obs_stride,
                vec->actions, vec->rewards, vec->terminals, vec->action_mask);
            buf_agent += n;
        }
    }
}

void static_vec_set_env_tags(StaticVec* vec, const int* tags) {
    if (!vec->vtable->uses_tags()) {
        fprintf(stderr, "static_vec_set_env_tags: env did not opt in via uses_tags; ignoring.\n");
        return;
    }
    for (int i = 0; i < vec->size; i++) {
        vec->vtable->set_tag(vec->handles[i], tags[i]);
        vec->vtable->set_boundary_reached(vec->handles[i], 0);
    }
}

int static_vec_count_aligned(StaticVec* vec, int tag_value, int reset_flags) {
    if (!vec->vtable->uses_tags()) return 0;
    int count = 0;
    for (int i = 0; i < vec->size; i++) {
        if (vec->vtable->get_tag(vec->handles[i]) == tag_value &&
            vec->vtable->get_boundary_reached(vec->handles[i])) {
            count++;
        }
    }
    if (reset_flags) {
        for (int i = 0; i < vec->size; i++) {
            if (vec->vtable->get_tag(vec->handles[i]) == tag_value)
                vec->vtable->set_boundary_reached(vec->handles[i], 0);
        }
    }
    return count;
}

void static_vec_reset(StaticVec* vec) {
    PufferEnvVTable* vt = vec->vtable;
    for (int i = 0; i < vec->size; i++) {
        vt->env_reset(vec->handles[i]);
    }
    size_t obs_stride = (size_t)vec->obs_size * vec->obs_elem_size;
    if (vec->gpu) {
        cudaMemcpy(vec->gpu_observations, vec->observations,
            (size_t)vec->total_agents * obs_stride, cudaMemcpyHostToDevice);
        cudaMemset(vec->gpu_rewards,   0, (size_t)vec->total_agents * sizeof(float));
        cudaMemset(vec->gpu_terminals, 0, (size_t)vec->total_agents * sizeof(float));
        if (vec->action_mask_size > 0) {
            cudaMemcpy(vec->gpu_action_mask, vec->action_mask,
                (size_t)vec->total_agents * vec->action_mask_size * sizeof(unsigned char),
                cudaMemcpyHostToDevice);
        }
        cudaDeviceSynchronize();
    } else {
        memset(vec->rewards,   0, (size_t)vec->total_agents * sizeof(float));
        memset(vec->terminals, 0, (size_t)vec->total_agents * sizeof(float));
    }
}

void create_static_threads(StaticVec* vec, int num_threads, int horizon,
        void* ctx, net_callback_fn net_callback, thread_init_fn thread_init) {
    vec->threading = (StaticThreading*)calloc(1, sizeof(StaticThreading));
    vec->threading->num_threads = num_threads;
    vec->threading->num_buffers = vec->buffers;
    vec->threading->buffer_states = (atomic_int*)calloc(vec->buffers, sizeof(atomic_int));
    vec->threading->threads = (pthread_t*)calloc(vec->buffers, sizeof(pthread_t));
    vec->threading->accum   = (float*)calloc(vec->buffers * NUM_EVAL_PROF, sizeof(float));

    StaticOMPArg* args = (StaticOMPArg*)calloc(vec->buffers, sizeof(StaticOMPArg));
    for (int i = 0; i < vec->buffers; i++) {
        args[i].vec          = vec;
        args[i].buf          = i;
        args[i].horizon      = horizon;
        args[i].ctx          = ctx;
        args[i].net_callback = net_callback;
        args[i].thread_init  = thread_init;
        pthread_create(&vec->threading->threads[i], NULL, static_omp_threadmanager, &args[i]);
    }
}

void static_vec_close(StaticVec* vec) {
    PufferEnvVTable* vt = vec->vtable;

    if (vec->threading != NULL) {
        atomic_store(&vec->threading->shutdown, 1);
        for (int i = 0; i < vec->buffers; i++)
            pthread_join(vec->threading->threads[i], NULL);
    }

    for (int i = 0; i < vec->size; i++)
        vt->env_close(vec->handles[i]);

    // vec_close frees both env data and handles array
    vt->vec_close(vec->handles, vec->size);

    if (vec->threading != NULL) {
        free(vec->threading->buffer_states);
        free(vec->threading->threads);
        free(vec->threading->accum);
        free(vec->threading);
    }
    free(vec->buffer_env_starts);
    free(vec->buffer_env_counts);

    if (vec->gpu) {
        cudaDeviceSynchronize();
        cudaFree(vec->gpu_observations);
        cudaFree(vec->gpu_actions);
        cudaFree(vec->gpu_rewards);
        cudaFree(vec->gpu_terminals);
        cudaFreeHost(vec->observations);
        cudaFreeHost(vec->actions);
        cudaFreeHost(vec->rewards);
        cudaFreeHost(vec->terminals);
        if (vec->action_mask_size > 0) {
            cudaFree(vec->gpu_action_mask);
            cudaFreeHost(vec->action_mask);
        }
    } else {
        free(vec->observations);
        free(vec->actions);
        free(vec->rewards);
        free(vec->terminals);
        if (vec->action_mask_size > 0)
            free(vec->action_mask);
    }

    free(vec->streams);
    if (vec->agent_perm != NULL) free(vec->agent_perm);
    free(vec);
}

// ---- Log aggregation ----
// Log struct convention: flat array of float; last field is float n (sample count).

static float static_vec_aggregate_logs(StaticVec* vec, float* aggregate) {
    PufferEnvVTable* vt = vec->vtable;
    int num_fields = vt->get_log_num_fields();
    memset(aggregate, 0, (size_t)num_fields * sizeof(float));

    for (int i = 0; i < vec->size; i++) {
        float* log = (float*)vt->get_log_ptr(vec->handles[i]);
        float n = log[num_fields - 1];
        if (n == 0) continue;
        for (int j = 0; j < num_fields; j++)
            aggregate[j] += log[j];
    }

    float total_n = aggregate[num_fields - 1];
    if (total_n == 0.0f) return 0;

    for (int j = 0; j < num_fields; j++)
        aggregate[j] /= total_n;

    return total_n;
}

void static_vec_log(StaticVec* vec, Dict* out) {
    PufferEnvVTable* vt = vec->vtable;
    int num_fields = vt->get_log_num_fields();
    float* aggregate = (float*)calloc((size_t)num_fields, sizeof(float));

    float total_n = static_vec_aggregate_logs(vec, aggregate);
    if (total_n == 0) {
        free(aggregate);
        return;
    }

    // Zero each env's log
    for (int i = 0; i < vec->size; i++)
        memset(vt->get_log_ptr(vec->handles[i]), 0, (size_t)num_fields * sizeof(float));

    vt->log_export(aggregate, out);
    dict_set(out, "n", total_n);
    free(aggregate);
}

void static_vec_eval_log(StaticVec* vec, Dict* out) {
    PufferEnvVTable* vt = vec->vtable;
    int num_fields = vt->get_log_num_fields();
    float* aggregate = (float*)calloc((size_t)num_fields, sizeof(float));

    float total_n = static_vec_aggregate_logs(vec, aggregate);
    if (total_n > 0) {
        vt->log_export(aggregate, out);
        dict_set(out, "n", total_n);
    }
    free(aggregate);
}

void static_vec_read_profile(StaticVec* vec, float out[NUM_EVAL_PROF]) {
    StaticThreading* threading = vec->threading;
    memset(out, 0, NUM_EVAL_PROF * sizeof(float));
    for (int buf = 0; buf < threading->num_buffers; buf++) {
        float* src = &threading->accum[buf * NUM_EVAL_PROF];
        for (int i = 0; i < NUM_EVAL_PROF; i++) out[i] += src[i];
        memset(src, 0, NUM_EVAL_PROF * sizeof(float));
    }
    for (int i = 0; i < NUM_EVAL_PROF; i++) out[i] /= threading->num_buffers;
}

void static_vec_render(StaticVec* vec, int env_id) {
    vec->vtable->env_render(vec->handles[env_id]);
}

void gpu_vec_step(StaticVec* vec) {
    assert(vec->buffers == 1);
    cudaMemcpy(vec->actions, vec->gpu_actions,
        (size_t)vec->total_agents * vec->num_atns * sizeof(float),
        cudaMemcpyDeviceToHost);
    _static_vec_env_step(vec);
    size_t obs_stride = (size_t)vec->obs_size * vec->obs_elem_size;
    cudaMemcpy(vec->gpu_observations, vec->observations,
        (size_t)vec->total_agents * obs_stride, cudaMemcpyHostToDevice);
    cudaMemcpy(vec->gpu_rewards, vec->rewards,
        (size_t)vec->total_agents * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(vec->gpu_terminals, vec->terminals,
        (size_t)vec->total_agents * sizeof(float), cudaMemcpyHostToDevice);
}

void cpu_vec_step(StaticVec* vec) {
    assert(vec->buffers == 1);
    _static_vec_env_step(vec);
}

void static_vec_step(StaticVec* vec) {
    if (vec->gpu) gpu_vec_step(vec);
    else cpu_vec_step(vec);
}

#endif // VECENV_C_INCLUDED
