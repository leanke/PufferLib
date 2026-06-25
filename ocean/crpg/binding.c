#include "pufferenv.h"
#include "crpg.h"

// ---- Metadata ---------------------------------------------------------------

static int get_obs_size(void)              { return CRPG_OBS_SIZE; }
static int get_num_atns(void)             { return 1; }
static const int ACT_SIZES[] = {ACTION_COUNT};
static const int* get_act_sizes(void)     { return ACT_SIZES; }
static int get_num_act_sizes(void)        { return 1; }
static PufferEnvDtype get_obs_dtype(void) { return PUFFERENV_DTYPE_UINT8; }
static size_t get_obs_elem_size(void)     { return sizeof(uint8_t); }
static int get_action_mask_size(void)     { return 0; }
static int uses_perm(void)                { return 0; }
static int uses_tags(void)                { return 0; }

// ---- Vec lifecycle ----------------------------------------------------------

static PufferEnvHandle* vec_init_impl(
        int* num_envs_out, int* buf_starts, int* buf_counts,
        const PufferDict* vec_kwargs, const PufferDict* env_kwargs) {
    int num_envs    = (int)pufferenv_get(vec_kwargs, "total_agents");
    int num_buffers = (int)pufferenv_get(vec_kwargs, "num_buffers");
    int max_steps   = (int)pufferenv_get(env_kwargs, "max_steps");

    CrpgEnv* envs = (CrpgEnv*)calloc(num_envs, sizeof(CrpgEnv));
    PufferEnvHandle* handles = (PufferEnvHandle*)malloc(
        (size_t)num_envs * sizeof(PufferEnvHandle));

    for (int i = 0; i < num_envs; i++) {
        handles[i]           = &envs[i];
        envs[i].num_agents   = 1;
        envs[i].max_steps    = max_steps;
        envs[i].seed         = (uint64_t)(i + 1);
        sim_init(&envs[i].sim, envs[i].seed, /*headless=*/1, /*num_players=*/1);
    }

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
    CrpgEnv* envs = (CrpgEnv*)handles[0];
    for (int i = 0; i < num_envs; i++)
        sim_destroy(&envs[i].sim);
    free(envs);
    free(handles);
}

// ---- Per-env lifecycle ------------------------------------------------------

static void env_init_impl(PufferEnvHandle h, const PufferDict* kwargs) {
    (void)h; (void)kwargs;
}

static int env_num_agents_impl(PufferEnvHandle h) {
    return ((CrpgEnv*)h)->num_agents;
}

static void env_reset_impl(PufferEnvHandle h) {
    CrpgEnv* env = (CrpgEnv*)h;
    env->seed++;
    sim_reset(&env->sim, env->seed);
    env->tick           = 0;
    env->episode_return = 0.0f;
    encode_obs(env);
    if (env->rewards)   env->rewards[0]   = 0.0f;
    if (env->terminals) env->terminals[0] = 0.0f;
}

static void env_step_impl(PufferEnvHandle h) {
    CrpgEnv* env = (CrpgEnv*)h;

    ActionID action = (ActionID)((int)env->actions[0]);
    if (action < 0 || action >= ACTION_COUNT) action = ACTION_NONE;

    env->sim.last_reward = 0.0f;
    sim_tick(&env->sim, action);

    env->rewards[0]      = env->sim.last_reward;
    env->episode_return += env->sim.last_reward;
    env->tick++;

    int done = env->sim.done || (env->tick >= env->max_steps);
    env->terminals[0] = (float)done;
    encode_obs(env);

    if (done) {
        env->log.episode_return += env->episode_return;
        env->log.episode_length += (float)env->tick;
        env->log.deaths         += (float)env->sim.done;
        env->log.n              += 1.0f;
        env_reset_impl(env);
        env->terminals[0] = 1.0f;  // keep terminal signal for this timestep
    }
}

static void env_render_impl(PufferEnvHandle h) { (void)h; }

static void env_close_impl(PufferEnvHandle h) {
    // sim_destroy called in vec_close_impl; this is a no-op per-env
    (void)h;
}

static void set_slot_impl(PufferEnvHandle h, const PufferEnvSlot* slot) {
    CrpgEnv* env      = (CrpgEnv*)h;
    env->observations  = (uint8_t*)slot->observations;
    env->actions       = slot->actions;
    env->rewards       = slot->rewards;
    env->terminals     = slot->terminals;
}

// ---- Logging ----------------------------------------------------------------

static void* get_log_ptr_impl(PufferEnvHandle h) {
    return &((CrpgEnv*)h)->log;
}

static int get_log_num_fields_impl(void) {
    return (int)(sizeof(CrpgLog) / sizeof(float));
}

static void log_export_impl(const void* agg, PufferDict* out) {
    const CrpgLog* log = (const CrpgLog*)agg;
    pufferenv_dict_set(out, "episode_return", log->episode_return);
    pufferenv_dict_set(out, "episode_length", log->episode_length);
    pufferenv_dict_set(out, "deaths",         log->deaths);
    pufferenv_dict_set(out, "n",              log->n);
}

// ---- Vtable -----------------------------------------------------------------

static PufferEnvVTable VTABLE = {
    .abi_version          = PUFFERENV_ABI_VERSION,
    .get_obs_size         = get_obs_size,
    .get_num_atns         = get_num_atns,
    .get_act_sizes        = get_act_sizes,
    .get_num_act_sizes    = get_num_act_sizes,
    .get_obs_dtype        = get_obs_dtype,
    .get_obs_elem_size    = get_obs_elem_size,
    .get_action_mask_size = get_action_mask_size,
    .uses_perm            = uses_perm,
    .uses_tags            = uses_tags,
    .vec_init             = vec_init_impl,
    .vec_close            = vec_close_impl,
    .env_init             = env_init_impl,
    .env_num_agents       = env_num_agents_impl,
    .env_step             = env_step_impl,
    .env_reset            = env_reset_impl,
    .env_render           = env_render_impl,
    .env_close            = env_close_impl,
    .set_slot             = set_slot_impl,
    .vec_step             = NULL,
    .vec_step_range       = NULL,
    .setup_perm           = NULL,
    .get_log_ptr          = get_log_ptr_impl,
    .get_log_num_fields   = get_log_num_fields_impl,
    .log_export           = log_export_impl,
    .set_tag              = NULL,
    .get_tag              = NULL,
    .get_boundary_reached = NULL,
    .set_boundary_reached = NULL,
    .shared               = NULL,
    .shared_close         = NULL,
    .get                  = NULL,
    .put                  = NULL,
};

PufferEnvVTable* pufferenv_get_vtable(void) { return &VTABLE; }
