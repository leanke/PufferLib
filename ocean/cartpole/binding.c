#include "pufferenv.h"
#include "cartpole.h"

// ---- Metadata ---------------------------------------------------------------

static int get_obs_size(void)         { return 4; }
static int get_num_atns(void)         { return 1; }
static const int ACT_SIZES[] = {2};
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
    // Cartpole has 1 agent per env, so num_envs == total_agents
    int num_envs = (int)pufferenv_get(vec_kwargs, "total_agents");
    int num_buffers = (int)pufferenv_get(vec_kwargs, "num_buffers");

    Cartpole* envs = (Cartpole*)calloc(num_envs, sizeof(Cartpole));
    PufferEnvHandle* handles = (PufferEnvHandle*)malloc((size_t)num_envs * sizeof(PufferEnvHandle));
    for (int i = 0; i < num_envs; i++) {
        handles[i] = &envs[i];
        envs[i].num_agents = 1;
        envs[i].cart_mass   = (float)pufferenv_get(env_kwargs, "cart_mass");
        envs[i].pole_mass   = (float)pufferenv_get(env_kwargs, "pole_mass");
        envs[i].pole_length = (float)pufferenv_get(env_kwargs, "pole_length");
        envs[i].gravity     = (float)pufferenv_get(env_kwargs, "gravity");
        envs[i].force_mag   = (float)pufferenv_get(env_kwargs, "force_mag");
        envs[i].tau         = (float)pufferenv_get(env_kwargs, "dt");
        envs[i].continuous  = (int)pufferenv_get(env_kwargs, "continuous");
        init(&envs[i]);
    }

    int per_buf = num_envs / num_buffers;
    for (int b = 0; b < num_buffers; b++) {
        buf_starts[b] = b * per_buf;
        buf_counts[b] = (b == num_buffers - 1) ? num_envs - b * per_buf : per_buf;
    }

    *num_envs_out = num_envs;
    return handles;
}

static void vec_close_impl(PufferEnvHandle* handles, int num_envs) {
    if (num_envs > 0 && handles[0]) free(handles[0]);
    free(handles);
}

// ---- Per-env lifecycle ------------------------------------------------------

static void env_init_impl(PufferEnvHandle h, const PufferDict* kwargs) {
    (void)h; (void)kwargs;
}

static int env_num_agents_impl(PufferEnvHandle h) {
    return ((Cartpole*)h)->num_agents;
}

static void env_step_impl(PufferEnvHandle h) {
    c_step((Cartpole*)h);
}

static void env_reset_impl(PufferEnvHandle h) {
    c_reset((Cartpole*)h);
}

static void env_render_impl(PufferEnvHandle h) {
    c_render((Cartpole*)h);
}

static void env_close_impl(PufferEnvHandle h) {
    c_close((Cartpole*)h);
}

static void set_slot_impl(PufferEnvHandle h, const PufferEnvSlot* slot) {
    Cartpole* env = (Cartpole*)h;
    env->observations = (float*)slot->observations;
    env->actions      = slot->actions;
    env->rewards      = slot->rewards;
    env->terminals    = slot->terminals;
}

// ---- Logging ----------------------------------------------------------------

static void* get_log_ptr_impl(PufferEnvHandle h) {
    return &((Cartpole*)h)->log;
}

static int get_log_num_fields_impl(void) {
    return (int)(sizeof(Log) / sizeof(float));
}

static void log_export_impl(const void* agg, PufferDict* out) {
    const Log* log = (const Log*)agg;
    pufferenv_dict_set(out, "score",                    log->score);
    pufferenv_dict_set(out, "perf",                     log->perf);
    pufferenv_dict_set(out, "episode_length",            log->episode_length);
    pufferenv_dict_set(out, "x_threshold_termination",  log->x_threshold_termination);
    pufferenv_dict_set(out, "pole_angle_termination",   log->pole_angle_termination);
    pufferenv_dict_set(out, "max_steps_termination",    log->max_steps_termination);
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
    .set_tag             = NULL,
    .get_tag             = NULL,
    .get_boundary_reached = NULL,
    .set_boundary_reached = NULL,
    .shared             = NULL,
    .shared_close       = NULL,
    .get                = NULL,
    .put                = NULL,
};

PufferEnvVTable* pufferenv_get_vtable(void) { return &VTABLE; }
