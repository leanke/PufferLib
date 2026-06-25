# PufferLib Plugin ABI

Environments compile to standalone `.so` plugins and are loaded at runtime via
`dlopen`. PufferLib itself is built once; no recompile is needed when switching
or adding environments.

---

## Quick start

```bash
# 1. Build PufferLib core once (CUDA, BF16 precision by default)
./build_pufferlib.sh

# 2. Build an environment plugin
./build_env.sh cartpole

# 3. Train
python train.py env_name=cartpole
```

**CPU / float32 variants:**

```bash
./build_pufferlib.sh --float    # FP32 precision (no BF16)
./build_pufferlib.sh --cpu      # No CUDA (for --slowly torch mode)
```

**Environment search path** — `find_env_plugin()` looks in this order:

1. `search_dirs` argument (if passed by caller)
2. Colon-separated dirs in `$PUFFERLIB_ENV_PATH`
3. `~/.local/share/pufferlib/envs/`
4. `ocean/<env_name>/` inside the repo

---

## Writing a new environment plugin

An environment plugin is a shared library that exposes one symbol:

```c
PufferEnvVTable* pufferenv_get_vtable(void);
```

Include only `pufferenv.h` from PufferLib — no `vecenv.h`, no CUDA headers.

### Minimal template

```c
#include "pufferenv.h"
#include "myenv.h"          // your environment header

// ---- Metadata ---------------------------------------------------------------

static int get_obs_size(void)         { return 8; }
static int get_num_atns(void)         { return 1; }
static const int ACT_SIZES[] = {4};
static const int* get_act_sizes(void) { return ACT_SIZES; }
static int get_num_act_sizes(void)    { return 1; }
static PufferEnvDtype get_obs_dtype(void)  { return PUFFERENV_DTYPE_FLOAT32; }
static size_t get_obs_elem_size(void)      { return sizeof(float); }
static int get_action_mask_size(void) { return 0; }
static int uses_perm(void)            { return 0; }
static int uses_tags(void)            { return 0; }

// ---- Vec lifecycle ----------------------------------------------------------

static PufferEnvHandle* vec_init_impl(
        int* num_envs_out, int* buf_starts, int* buf_counts,
        const PufferDict* vec_kwargs, const PufferDict* env_kwargs) {
    int num_envs    = (int)pufferenv_get(vec_kwargs, "total_agents");
    int num_buffers = (int)pufferenv_get(vec_kwargs, "num_buffers");

    MyEnv* envs = (MyEnv*)calloc(num_envs, sizeof(MyEnv));
    PufferEnvHandle* handles = (PufferEnvHandle*)malloc(
        (size_t)num_envs * sizeof(PufferEnvHandle));

    for (int i = 0; i < num_envs; i++) {
        handles[i] = &envs[i];
        envs[i].some_param = (float)pufferenv_get(env_kwargs, "some_param");
        my_env_init(&envs[i]);
    }

    // Partition envs across CUDA buffers
    int per_buf = num_envs / num_buffers;
    for (int b = 0; b < num_buffers; b++) {
        buf_starts[b] = b * per_buf;
        buf_counts[b] = (b == num_buffers - 1)
            ? num_envs - b * per_buf : per_buf;
    }
    *num_envs_out = num_envs;
    return handles;
}

static void vec_close_impl(PufferEnvHandle* handles, int num_envs) {
    if (num_envs > 0 && handles[0]) free(handles[0]);  // free env block
    free(handles);
}

// ---- Per-env lifecycle ------------------------------------------------------

static void env_init_impl(PufferEnvHandle h, const PufferDict* kw) { (void)h; (void)kw; }
static int  env_num_agents_impl(PufferEnvHandle h) { return ((MyEnv*)h)->num_agents; }
static void env_step_impl(PufferEnvHandle h)       { my_step((MyEnv*)h); }
static void env_reset_impl(PufferEnvHandle h)      { my_reset((MyEnv*)h); }
static void env_render_impl(PufferEnvHandle h)     { my_render((MyEnv*)h); }
static void env_close_impl(PufferEnvHandle h)      { my_close((MyEnv*)h); }

// Inject PufferLib's pinned buffer pointers into env
static void set_slot_impl(PufferEnvHandle h, const PufferEnvSlot* slot) {
    MyEnv* env = (MyEnv*)h;
    env->observations = (float*)slot->observations;
    env->actions      = slot->actions;
    env->rewards      = slot->rewards;
    env->terminals    = slot->terminals;
    // env->action_mask = slot->action_mask;  // if action_mask_size > 0
}

// ---- Logging ----------------------------------------------------------------

static void* get_log_ptr_impl(PufferEnvHandle h) { return &((MyEnv*)h)->log; }
static int get_log_num_fields_impl(void) { return (int)(sizeof(Log) / sizeof(float)); }

static void log_export_impl(const void* agg, PufferDict* out) {
    const Log* log = (const Log*)agg;
    pufferenv_dict_set(out, "score",          log->score);
    pufferenv_dict_set(out, "episode_length", log->episode_length);
    // n is always last: pufferenv_dict_set(out, "n", log->n);
}

// ---- Vtable -----------------------------------------------------------------

static PufferEnvVTable VTABLE = {
    .abi_version        = PUFFERENV_ABI_VERSION,
    .get_obs_size       = get_obs_size,
    .get_num_atns       = get_num_atns,
    .get_act_sizes      = get_act_sizes,
    .get_num_act_sizes  = get_num_act_sizes,
    .get_obs_dtype      = get_obs_dtype,
    .get_obs_elem_size  = get_obs_elem_size,
    .get_action_mask_size = get_action_mask_size,
    .uses_perm          = uses_perm,
    .uses_tags          = uses_tags,
    .vec_init           = vec_init_impl,
    .vec_close          = vec_close_impl,
    .env_init           = env_init_impl,
    .env_num_agents     = env_num_agents_impl,
    .env_step           = env_step_impl,
    .env_reset          = env_reset_impl,
    .env_render         = env_render_impl,
    .env_close          = env_close_impl,
    .set_slot           = set_slot_impl,
    .vec_step           = NULL,
    .vec_step_range     = NULL,
    .setup_perm         = NULL,
    .get_log_ptr        = get_log_ptr_impl,
    .get_log_num_fields = get_log_num_fields_impl,
    .log_export         = log_export_impl,
    .set_tag            = NULL,
    .get_tag            = NULL,
    .get_boundary_reached = NULL,
    .set_boundary_reached = NULL,
    .shared             = NULL,
    .shared_close       = NULL,
    .get                = NULL,
    .put                = NULL,
};

PufferEnvVTable* pufferenv_get_vtable(void) { return &VTABLE; }
```

### Observation dtypes

| `PufferEnvDtype`           | C type      | `obs_elem_size`     |
|----------------------------|-------------|---------------------|
| `PUFFERENV_DTYPE_FLOAT32`  | `float*`    | `sizeof(float)`     |
| `PUFFERENV_DTYPE_UINT8`    | `uint8_t*`  | `sizeof(uint8_t)`   |
| `PUFFERENV_DTYPE_FLOAT16`  | `uint16_t*` | `sizeof(uint16_t)`  |

Cast `slot->observations` to the appropriate pointer type in `set_slot_impl`.

### Log struct convention

```c
typedef struct {
    float score;
    float episode_length;
    // ... any float fields ...
    float n;   // MUST be last: episode count (denominator for averaging)
} Log;
```

`get_log_num_fields()` must return `sizeof(Log) / sizeof(float)`.

---

## Optional vtable fields

### Custom vectorized step (`vec_step`, `vec_step_range`)

Set either to override the default `#pragma omp parallel for` over `env_step`:

```c
// Called for the full env set each step
static void my_vec_step(PufferEnvHandle* handles, int num_envs) { ... }

// Called for a range [env_start, env_start+env_count) in threaded mode
static void my_vec_step_range(PufferEnvHandle* handles,
                               int env_start, int env_count,
                               int num_workers) { ... }
```

Set `VTABLE.vec_step = my_vec_step` or `VTABLE.vec_step_range = my_vec_step_range`.

### Selfplay tags (`uses_tags() == 1`)

Required when using selfplay pool curricula. Implement:

```c
static void set_tag_impl(PufferEnvHandle h, int tag)           { ... }
static int  get_tag_impl(PufferEnvHandle h)                    { ... }
static int  get_boundary_reached_impl(PufferEnvHandle h)       { ... }
static void set_boundary_reached_impl(PufferEnvHandle h, int v){ ... }
```

### Agent permutation (`uses_perm() == 1`)

Required for multi-agent envs with non-trivial slot assignment (e.g., chess).
Implement `setup_perm` to remap per-slot pointers when the global permutation
changes:

```c
static void setup_perm_impl(PufferEnvHandle h,
    const int* global_perm, int slot_base, int num_slots,
    void* obs_base, size_t obs_elem_stride,
    float* act_base, float* rew_base,
    float* term_base, unsigned char* mask_base) { ... }
```

---

## Migrating an existing `binding.c`

Use the automated migration tool for simple (non-perm, non-tag) environments:

```bash
python tools/migrate_binding.py ocean/myenv/binding.c
# Writes ocean/myenv/binding_new.c — review, then replace binding.c
```

The tool reads `#define OBS_SIZE`, `#define NUM_ATNS`, `#define ACT_SIZES`,
`#define OBS_TENSOR_T`, `void my_init(...)`, `void my_log(...)` from the old
file and generates the vtable equivalent.

Complex envs (`MY_USES_PERM`, `MY_USES_TAGS`, `MY_VEC_STEP`, `MY_ACTION_MASK`)
require manual review of the generated output.

---

## Building a plugin outside the repo

Only `src/pufferenv.h` is needed from PufferLib:

```bash
clang -O2 -fPIC -shared \
    -I/path/to/pufferlib/src \
    -I/path/to/my_env \
    -fopenmp \
    ocean/myenv/binding.c \
    -lm -lpthread \
    -o myenv_env.so
```

No CUDA, no pybind11, no `vecenv.h`.

---

## Python API

```python
from pufferlib._loader import find_env_plugin, load_env_plugin

# Find and load plugin, get vtable pointer
path = find_env_plugin("cartpole")
lib, vtable_ptr = load_env_plugin(path)

# lib must stay alive (kept by _loaded_libs inside _loader.py)
# vtable_ptr is an integer passed to C++ as long long
```

`find_env_plugin` raises `FileNotFoundError` with the build command if the
plugin is not found.

---

## Vtable field reference

| Field                  | Required | Description |
|------------------------|----------|-------------|
| `abi_version`          | yes      | Must equal `PUFFERENV_ABI_VERSION` |
| `get_obs_size`         | yes      | Number of observation elements per agent |
| `get_num_atns`         | yes      | Number of action elements per agent |
| `get_act_sizes`        | yes      | Array of branch sizes for discrete actions |
| `get_num_act_sizes`    | yes      | Length of `get_act_sizes()` array |
| `get_obs_dtype`        | yes      | Observation element type |
| `get_obs_elem_size`    | yes      | `sizeof` of one observation element |
| `get_action_mask_size` | yes      | 0 if unused, otherwise branch count |
| `uses_perm`            | yes      | 1 if env uses agent permutation |
| `uses_tags`            | yes      | 1 if env uses selfplay tags |
| `vec_init`             | yes      | Allocate all env instances |
| `vec_close`            | yes      | Free all env instances + handles |
| `env_init`             | yes      | Per-env init (called after vec_init per handle) |
| `env_num_agents`       | yes      | Returns agent count for one env instance |
| `env_step`             | yes      | Advance one env by one step |
| `env_reset`            | yes      | Reset one env |
| `env_render`           | yes      | Render one env (no-op is fine) |
| `env_close`            | yes      | Per-env cleanup (called before vec_close) |
| `set_slot`             | yes      | Inject pinned buffer pointers into one env |
| `get_log_ptr`          | yes      | Returns `&env->log` |
| `get_log_num_fields`   | yes      | `sizeof(Log) / sizeof(float)` |
| `log_export`           | yes      | Write averaged log to `PufferDict` |
| `vec_step`             | no       | Custom full-batch step (replaces OMP loop) |
| `vec_step_range`       | no       | Custom range step (for threaded rolling) |
| `setup_perm`           | no       | Required when `uses_perm() == 1` |
| `set_tag`              | no       | Required when `uses_tags() == 1` |
| `get_tag`              | no       | Required when `uses_tags() == 1` |
| `get_boundary_reached` | no       | Required when `uses_tags() == 1` |
| `set_boundary_reached` | no       | Required when `uses_tags() == 1` |
| `shared`               | no       | Shared state init across envs |
| `shared_close`         | no       | Shared state cleanup |
| `get` / `put`          | no       | Shared state accessors |
