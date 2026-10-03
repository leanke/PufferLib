#ifndef POKERED_H
#define POKERED_H

// Pokemon Red environment.
//
// One Env serves two game backends (Gambatte emulator / redcore) behind
// pokered_backend.h. This file owns the episode lifecycle; the pieces it
// delegates to:
//   pokered_layout.h        observation layout + action enum (shared with pokered.cu)
//   pokered_visited.h       bit-packed visited sets
//   pokered_observations.h  PkSnapshot -> observation vector
//   pokered_rewards.h       the six reward signals
//   pokered_render.h        raylib window / keyboard play

#include <math.h>

#include "raylib.h"
typedef float obs_t;
#include "pufferenv.h"

#include "includes/ram_map.h"
#include "includes/events.h"
#include "pokered_stream.h"

#include "pokered_layout.h"
#include "pokered_backend.h"
#include "pokered_visited.h"

#define ACT_SIZES {PKRED_ACTION_COUNT}
#define OBS_SIZE TOTAL_OBSERVATIONS
#define NUM_ATNS 1

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// Weighted reward totals for the current episode (what each term contributed).
typedef struct {
    float explore, catching, seeing, leveling, events, battling, death;
} RewardTotals;

struct Log {
    float episode_length;
    float episode_return;
    float level_sum;
    float pkmn_lvl[PARTY_SIZE];
    float party_count;
    float badges;
    float event_sum;
    float unique_coords;
    float map_exhaustion;
    float pokedex_owned;
    float pokedex_seen;
    RewardTotals reward;
    float milestone_pool_size;
    float reset_from_milestone;
    float n;
};

struct Env {
    Log log;
    Agent agents[1];
    int num_agents;
    int tag;                         // set by the vec layer
    int boundary_reached;            // set by the vec layer
    unsigned int rng;

    // Game backend and the last two snapshots of its state.
    const PkBackend *be;
    void *impl;
    PkSnapshot cur;
    PkSnapshot prev;

    // Episode bookkeeping.
    int32_t step_count;
    float score;
    int prev_action;
    RewardTotals totals;
    PokeredStream stream;

    // Exploration tracking.
    uint8_t *visited_coords;         // every tile stepped on (also an observation)
    uint8_t *visited_cells;          // coarse cells already paid out
    uint16_t *map_visited_counts;    // new tiles per map (map_exhaustion log)
    uint32_t unique_coords_count;
    uint8_t *prev_events;            // events seen last step, for milestone capture

    // Death / reset handling.
    bool party_wiped;                // latch so one wipe-out is penalised once
    int blackout_count;
    bool reset_from_milestone;
    int32_t max_episode_length;
    bool full_reset;                 // episode end reloads the start state

    // Reward weights.
    float weight_exploration;
    float weight_catching;
    float weight_seeing;
    float weight_leveling;
    float weight_events;
    float weight_battling;
    float weight_death;
    int exploration_cell_size;
    bool exploration_death_scaling_enabled;

    // Observation toggle (off = the screen segment is zero-filled).
    bool screen_obs_enabled;
    float map_exhaustion_norm;       // log-only: visits at which map_exhaustion reads 1.0

    bool verbose;

    Texture2D render_texture;
    uint8_t *render_pixels;
    bool show_obs_view;
};
typedef Env Pokered;

static inline bool is_directional_action(int action) {
    return action >= PKRED_ACTION_RIGHT && action <= PKRED_ACTION_DOWN;
}

#include "pokered_observations.h"
#include "pokered_rewards.h"

static void clear_visited(Env *env) {
    env->unique_coords_count = 0;
    memset(env->visited_coords, 0, VISITED_BYTES);
    memset(env->visited_cells, 0, VISITED_BYTES);
    memset(env->map_visited_counts, 0, MAX_MAPS * sizeof(*env->map_visited_counts));
}

static void add_log(Env *env) {
    const PkSnapshot *s = &env->cur;
    Log *log = &env->log;

    log->episode_length = env->step_count;
    log->episode_return = env->score;
    log->reward = env->totals;
    log->level_sum = party_level_sum(s);
    for (int i = 0; i < PARTY_SIZE; i++)
        log->pkmn_lvl[i] = s->party[i].level;
    log->party_count = s->party_count;
    log->badges = s->badges;
    log->event_sum = completed_event_count(s);
    log->unique_coords = env->unique_coords_count;
    log->map_exhaustion =
        fminf(1.0f, (float)env->map_visited_counts[s->map_n] / env->map_exhaustion_norm);
    log->pokedex_owned = s->pokedex_owned_count;
    log->pokedex_seen = s->pokedex_seen_count;
    log->milestone_pool_size = env->be->milestone_pool_size ? (float)env->be->milestone_pool_size() : 0.0f;
    log->reset_from_milestone = env->reset_from_milestone ? 1.0f : 0.0f;
    log->n++;
}

// ---------------------------------------------------------------------------
// Init: read config
// ---------------------------------------------------------------------------

static bool kw_bool(Dict *kw, const char *key) { return dict_get(kw, key) != 0.0; }
static int kw_int(Dict *kw, const char *key) { return (int)dict_get(kw, key); }
static float kw_float(Dict *kw, const char *key) { return (float)dict_get(kw, key); }

static const char *kw_str(Dict *kw, const char *key, const char *fallback) {
    DictItem *item = dict_find(kw, key);
    return item && item->str && item->str[0] ? item->str : fallback;
}

static void copy_str(char *dst, size_t cap, const char *src) {
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static void read_reward_config(Env *env, Dict *kw) {
    env->weight_exploration = kw_float(kw, "weight_exploration");
    env->weight_catching = kw_float(kw, "weight_catching");
    env->weight_seeing = kw_float(kw, "weight_seeing");
    env->weight_leveling = kw_float(kw, "weight_leveling");
    env->weight_events = kw_float(kw, "weight_events");
    env->weight_battling = kw_float(kw, "weight_battling");
    env->weight_death = kw_float(kw, "weight_death");
    env->exploration_cell_size = kw_int(kw, "exploration_cell_size");
    if (env->exploration_cell_size <= 0)
        env->exploration_cell_size = 1;
    env->exploration_death_scaling_enabled = kw_bool(kw, "exploration_death_scaling_enabled");
}

static void read_observation_config(Env *env, Dict *kw) {
    env->screen_obs_enabled = kw_bool(kw, "screen_obs_enabled");
    env->map_exhaustion_norm = kw_float(kw, "map_exhaustion_norm");
    if (env->map_exhaustion_norm <= 0.0f)
        env->map_exhaustion_norm = 1.0f;
}

static void read_episode_config(Env *env, Dict *kw) {
    env->max_episode_length = (int32_t)dict_get(kw, "max_episode_length");
    env->full_reset = kw_bool(kw, "full_reset");
    env->verbose = kw_bool(kw, "verbose");
}

static void read_backend_config(Env *env, Dict *kw, PkBackendConfig *bc) {
    memset(bc, 0, sizeof(*bc));
    bc->env_id = env->rng;
    bc->verbose = env->verbose;
    bc->screen_obs_enabled = env->screen_obs_enabled;

    bc->frameskip = kw_int(kw, "frameskip");
    bc->press_frames = kw_int(kw, "press_frames");
    bc->headless = kw_bool(kw, "headless");
    bc->audio_enabled = kw_bool(kw, "audio_enabled");
    bc->frame_render_skip_enabled = kw_bool(kw, "frame_render_skip_enabled");
    DictItem *nt = dict_find(kw, "vec_num_threads");
    bc->vec_num_threads = nt ? (int)nt->value : 1;

    copy_str(bc->rom_path, sizeof(bc->rom_path), dict_get_str(kw, "rom_path"));
    copy_str(bc->state_path, sizeof(bc->state_path), kw_str(kw, "state_path", ""));
    copy_str(bc->assets_dir, sizeof(bc->assets_dir), kw_str(kw, "assets_dir", "vendor/redcore/assets"));
    DictItem *fs = dict_find(kw, "fixed_starter_species");
    bc->fixed_starter_species = fs ? (int)fs->value : 0;

    bc->disable_wild_until_badge = kw_bool(kw, "disable_wild_until_badge");
    bc->route22_rival_beaten = kw_bool(kw, "route22_rival_beaten");
    bc->route22_rival_2nd_beaten = kw_bool(kw, "route22_rival_2nd_beaten");
    bc->nickname_prompt_enabled = kw_bool(kw, "nickname_prompt_enabled");
    bc->npc_text_enabled = kw_bool(kw, "npc_text_enabled");
    bc->npc_movement_enabled = kw_bool(kw, "npc_movement_enabled");

    bc->milestone_sample_prob = kw_float(kw, "milestone_sample_prob");
    bc->milestones_enabled = kw_bool(kw, "milestones_enabled");
    bc->event_milestones_enabled = kw_bool(kw, "event_milestones_enabled");
    bc->town_milestones_enabled = kw_bool(kw, "town_milestones_enabled");
}

static const PkBackend *select_backend(Dict *kw) {
    const char *name = kw_str(kw, "backend", "emulator");
    const PkBackend *be = NULL;
    if (strcmp(name, "emulator") == 0) {
        be = pk_backend_emulator();
    } else if (strcmp(name, "redcore") == 0) {
        be = pk_backend_redcore();
        if (!be) {
            fprintf(stderr, "pokered: built with --no-redcore; env.backend=redcore is unavailable\n");
            exit(1);
        }
    } else {
        fprintf(stderr, "pokered: unknown env.backend '%s' (expected emulator or redcore)\n", name);
        exit(1);
    }
    return be;
}

void puf_init(Env *env, Dict *kwargs) {
    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->agents[0].action_mask = NULL;

    read_episode_config(env, kwargs);
    read_reward_config(env, kwargs);
    read_observation_config(env, kwargs);

    PkBackendConfig bc;
    read_backend_config(env, kwargs, &bc);
    env->be = select_backend(kwargs);
    env->impl = env->be->create(&bc);

    env->visited_coords = (uint8_t *)calloc(VISITED_BYTES, 1);
    env->visited_cells = (uint8_t *)calloc(VISITED_BYTES, 1);
    env->map_visited_counts = (uint16_t *)calloc(MAX_MAPS, sizeof(uint16_t));
    env->prev_events = (uint8_t *)calloc(EVENT_COUNT, sizeof(uint8_t));
    env->prev_action = -1;

    stream_init(&env->stream, kw_bool(kwargs, "stream_enabled"),
                kw_str(kwargs, "stream_user", "User"), kw_str(kwargs, "stream_color", "#0000FF"),
                env->rng, kw_int(kwargs, "stream_interval"));
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

static void puf_reset_body(Env *env, bool do_full_reset) {
    bool full = env->full_reset && do_full_reset;
    if (full) {
        env->reset_from_milestone = false;
        clear_visited(env);
    }
    env->be->reset(env->impl, full, &env->rng, &env->reset_from_milestone);
    if (full)
        env->blackout_count = 0;

    memset(env->map_visited_counts, 0, MAX_MAPS * sizeof(*env->map_visited_counts));
    env->be->snapshot(env->impl, &env->cur);
    env->prev = env->cur;
    update_observations(env);

    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count = 0;
    env->score = 0.0f;
    env->prev_action = -1;
    env->party_wiped = false;
    memset(&env->totals, 0, sizeof(env->totals));
    memcpy(env->prev_events, env->cur.events, EVENT_COUNT);

    env->be->warmup(env->impl);
}

void puf_reset(Env *env) {
    env->be->acquire(env->impl);
    puf_reset_body(env, true);
    env->be->release(env->impl);
}

// ---------------------------------------------------------------------------
// Step
// ---------------------------------------------------------------------------

// Episodes end only on the step limit. The new episode's first observation is
// already in place when terminals[0] is set.
static void end_episode(Env *env) {
    add_log(env);
    puf_reset_body(env, true);
    env->agents[0].terminals[0] = 1;
}

// True on the step the whole party goes down (once per wipe-out). Backends that
// resolve the blackout inside the step report it through the snapshot's blackout
// counter; otherwise the wipe shows up as a 0-HP party for at least one step.
static bool party_just_wiped(Env *env, uint16_t blackouts_before) {
    if (env->cur.blackouts != blackouts_before) {
        env->party_wiped = false;
        return true;
    }
    bool alive = env->cur.party_count == 0 || env->cur.hp_fraction > 0.0f;
    if (alive) {
        env->party_wiped = false;
        return false;
    }
    bool first = !env->party_wiped;
    env->party_wiped = true;
    return first;
}

// Death penalty, then let the backend heal/teleport the player like a real
// blackout. Does not end the episode.
static void handle_blackout(Env *env) {
    env->agents[0].rewards[0] -= env->weight_death;
    env->score -= env->weight_death;
    env->totals.death += env->weight_death;
    env->blackout_count++;
    clear_visited(env);

    if (env->be->blackout) {
        env->be->blackout(env->impl);
        env->party_wiped = false;
        env->be->snapshot(env->impl, &env->cur);
        env->prev = env->cur;
        update_observations(env);
    }
}

static void puf_step_body(Env *env) {
    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count++;

    env->prev_action = (int)env->agents[0].actions[0];
    uint16_t blackouts_before = env->cur.blackouts;
    env->be->step(env->impl, env->prev_action, &env->cur);

    float reward = calculate_rewards(env);
    update_observations(env);
    env->agents[0].rewards[0] = reward;
    env->score += reward;

    stream_collect(&env->stream, env->cur.x, env->cur.y, env->cur.map_n);
    if (env->stream.interval > 0 && env->step_count % env->stream.interval == 0)
        stream_flush(&env->stream);

    if (party_just_wiped(env, blackouts_before))
        handle_blackout(env);

    if (env->max_episode_length > 0 && env->step_count >= env->max_episode_length)
        end_episode(env);
}

void puf_step(Env *env) {
    env->be->acquire(env->impl);
    puf_step_body(env);
    env->be->release(env->impl);
}

// ---------------------------------------------------------------------------
// Render / close / log
// ---------------------------------------------------------------------------

#include "pokered_render.h"

void puf_close(Env *env) {
    if (IsWindowReady())
        CloseWindow();
    if (env->render_pixels) {
        free(env->render_pixels);
        env->render_pixels = NULL;
    }
    if (env->be && env->impl) {
        env->be->destroy(env->impl);
        env->impl = NULL;
    }
    stream_close(&env->stream);
    free(env->visited_coords);
    free(env->visited_cells);
    free(env->map_visited_counts);
    free(env->prev_events);
}

void puf_log(Log *log, Dict *out) {
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "level_sum", log->level_sum);
    dict_set(out, "unique_coords", log->unique_coords);
    dict_set(out, "party_count", log->party_count);
    dict_set(out, "pokedex_owned", log->pokedex_owned);
    dict_set(out, "pokedex_seen", log->pokedex_seen);
    dict_set(out, "badges", log->badges);
    dict_set(out, "event_sum", log->event_sum);

    dict_set(out, "pkmn1_lvl", log->pkmn_lvl[0]);
    dict_set(out, "pkmn2_lvl", log->pkmn_lvl[1]);
    dict_set(out, "pkmn3_lvl", log->pkmn_lvl[2]);
    dict_set(out, "pkmn4_lvl", log->pkmn_lvl[3]);
    dict_set(out, "pkmn5_lvl", log->pkmn_lvl[4]);
    dict_set(out, "pkmn6_lvl", log->pkmn_lvl[5]);

    dict_set(out, "explore_signal", log->reward.explore);
    dict_set(out, "catching_signal", log->reward.catching);
    dict_set(out, "seeing_signal", log->reward.seeing);
    dict_set(out, "leveling_signal", log->reward.leveling);
    dict_set(out, "events_signal", log->reward.events);
    dict_set(out, "battling_signal", log->reward.battling);
    dict_set(out, "death_signal", log->reward.death);

    dict_set(out, "milestone_pool_size", log->milestone_pool_size);
    dict_set(out, "reset_from_milestone", log->reset_from_milestone);
    dict_set(out, "n", log->n);
}

#endif
