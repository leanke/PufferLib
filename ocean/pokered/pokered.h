#ifndef POKERED_H
#define POKERED_H

#include <math.h>

#include "raylib.h"
typedef float obs_t;
#include "pufferenv.h"

#include "includes/ram_map.h"
#include "includes/events.h"
#include "pokered_stream.h"

#include "pokered_layout.h"
#include "pokered_backend.h"

#define MAX_MAPS 256
#define MAX_X 128
#define MAX_Y 128
#define VISITED_COORDS_SIZE (MAX_MAPS * MAX_X * MAX_Y)
#define VISITED_BYTES (VISITED_COORDS_SIZE / 8)

#define ACT_SIZES {PKRED_ACTION_COUNT}
#define OBS_SIZE TOTAL_OBSERVATIONS
#define NUM_ATNS 1

struct Log {
    float episode_length;
    float episode_return;
    float money;
    float level_sum;
    float pkmn1_lvl;
    float pkmn2_lvl;
    float pkmn3_lvl;
    float pkmn4_lvl;
    float pkmn5_lvl;
    float pkmn6_lvl;
    float party_count;
    float badges;
    float event_sum;
    float unique_coords;
    float map_exhaustion;
    float pokedex_owned;
    float pokedex_seen;
    float explore_signal;
    float catching_signal;
    float seeing_signal;
    float events_signal;
    float leveling_signal;
    float healing_signal;
    float hm_learned_signal;
    float pokecenter_signal;
    float pokecenter_visit_signal;
    float run_signal;
    float death_signal;
    float milestone_pool_size;
    float reset_from_milestone;
    float n;
};

typedef struct {
    float total_explore_signal;
    float total_catching_signal;
    float total_seeing_signal;
    float total_events_signal;
    float total_leveling_signal;
    float total_healing_signal;
    float total_hm_learned_signal;
    float total_pokecenter_signal;
    float total_pokecenter_visit_signal;
    float total_run_signal;
    float total_death_signal;
} EpisodeStats;

struct Env {
    Log log;
    Agent agents[1];
    int num_agents;
    int tag;
    int boundary_reached;
    unsigned int rng;

    const PkBackend *be;
    void *impl;
    PkSnapshot cur;
    PkSnapshot prev;
    EpisodeStats stats;
    PokeredStream stream;

    uint8_t *visited_coords;
    uint16_t *map_visited_counts;
    uint8_t *visited_cells;
    uint8_t *prev_events;

    int32_t step_count;
    long total_agent_steps;
    int32_t max_episode_length;
    float prev_event_sum;
    uint32_t unique_coords_count;
    float score;
    int prev_action;
    bool party_wiped;

    bool full_reset;
    float full_reset_prob;
    int full_reset_min_blackouts;
    int blackout_count;
    bool stuck_reset_enabled;
    int32_t stuck_reset_steps;
    int32_t steps_since_new_tile;
    bool verbose;
    bool reset_from_milestone;
    uint8_t hm_rewarded_mask;

    bool screen_obs_enabled;
    bool map_exhaustion_obs_enabled;
    float map_exhaustion_norm;
    bool battle_status_obs_enabled;
    bool progress_badges_bits_enabled;
    bool hm_bag_obs_enabled;
    bool blackout_map_obs_enabled;
    bool bag_obs_enabled;
    bool key_item_obs_enabled;
    bool party_status_pp_obs_enabled;
    bool battle_moveset_obs_enabled;
    bool party_type_obs_enabled;
    bool progress_money_obs_enabled;
    int exploration_cell_size;
    long weight_exploration_anneal_start;
    long weight_exploration_anneal_end;
    bool exploration_death_scaling_enabled;

    float weight_exploration;
    float weight_catching;
    float weight_seeing;
    float weight_events;
    float weight_leveling;
    float weight_healing;
    float weight_hm_learned;
    float weight_pokecenter;
    float weight_pokecenter_visit;
    float weight_run;
    int pokecenter_visit_steps;
    int pokecenter_visit_step;
    float weight_time;
    float weight_death;

    Texture2D render_texture;
    uint8_t *render_pixels;
    bool show_obs_view;
};
typedef Env Pokered;

static inline uint32_t coord_index(uint8_t map, uint8_t x, uint8_t y) {
    uint32_t cx = x < MAX_X ? x : MAX_X - 1;
    uint32_t cy = y < MAX_Y ? y : MAX_Y - 1;
    return (uint32_t)map * (MAX_X * MAX_Y) + cx * MAX_Y + cy;
}
static inline bool vbit_get(const uint8_t *bits, uint32_t idx) {
    return (bits[idx >> 3] >> (idx & 7)) & 1;
}

static inline bool vbit_test_set(uint8_t *bits, uint32_t idx) {
    uint8_t mask = (uint8_t)(1u << (idx & 7));
    if (bits[idx >> 3] & mask)
        return false;
    bits[idx >> 3] |= mask;
    return true;
}
static inline bool is_directional_action(int action) {
    return action >= PKRED_ACTION_RIGHT && action <= PKRED_ACTION_DOWN;
}

#include "pokered_observations.h"
#include "pokered_rewards.h"

static void add_log(Env *env) {
    const PkSnapshot *core = &env->cur;

    env->log.episode_length = env->step_count;
    env->log.episode_return = env->score;
    env->log.death_signal = env->stats.total_death_signal;
    env->log.money = core->money;

    env->log.level_sum = calc_level_sum(core);
    for (int i = 0; i < 6; i++)
        (&env->log.pkmn1_lvl)[i] = core->party[i].level;
    env->log.party_count = core->party_count;

    env->log.badges = core->badges;
    env->log.event_sum = env->prev_event_sum;
    env->log.unique_coords = env->unique_coords_count;
    env->log.map_exhaustion = env->map_exhaustion_obs_enabled
        ? fminf(1.0f, (float)env->map_visited_counts[core->map_n] / env->map_exhaustion_norm)
        : 0.0f;
    env->log.pokedex_owned = core->pokedex_owned_count;
    env->log.pokedex_seen = core->pokedex_seen_count;

    env->log.explore_signal = env->stats.total_explore_signal;
    env->log.catching_signal = env->stats.total_catching_signal;
    env->log.seeing_signal = env->stats.total_seeing_signal;
    env->log.events_signal = env->stats.total_events_signal;
    env->log.leveling_signal = env->stats.total_leveling_signal;
    env->log.healing_signal = env->stats.total_healing_signal;
    env->log.hm_learned_signal = env->stats.total_hm_learned_signal;
    env->log.pokecenter_signal = env->stats.total_pokecenter_signal;
    env->log.pokecenter_visit_signal = env->stats.total_pokecenter_visit_signal;
    env->log.run_signal = env->stats.total_run_signal;
    env->log.milestone_pool_size = env->be->milestone_pool_size ? (float)env->be->milestone_pool_size() : 0.0f;
    env->log.reset_from_milestone = env->reset_from_milestone ? 1.0f : 0.0f;
    env->log.n++;
}

void puf_init(Env* env, Dict* kwargs) {
    unsigned int env_id = env->rng;

    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->agents[0].action_mask = NULL;

    PkBackendConfig bc;
    memset(&bc, 0, sizeof(bc));
    bc.env_id = env_id;
    bc.frameskip = (int)dict_get(kwargs, "frameskip");
    bc.press_frames = (int)dict_get(kwargs, "press_frames");
    bc.headless = dict_get(kwargs, "headless") != 0.0;
    env->max_episode_length = (int32_t)dict_get(kwargs, "max_episode_length");
    env->full_reset = dict_get(kwargs, "full_reset") != 0.0;
    env->full_reset_prob = (float)dict_get(kwargs, "full_reset_prob");
    env->full_reset_min_blackouts = (int)dict_get(kwargs, "full_reset_min_blackouts");
    env->stuck_reset_enabled = dict_get(kwargs, "stuck_reset_enabled") != 0.0;
    env->stuck_reset_steps = (int32_t)dict_get(kwargs, "stuck_reset_steps");
    bc.disable_wild_until_badge = dict_get(kwargs, "disable_wild_until_badge") != 0.0;
    bc.route22_rival_beaten = dict_get(kwargs, "route22_rival_beaten") != 0.0;
    bc.route22_rival_2nd_beaten = dict_get(kwargs, "route22_rival_2nd_beaten") != 0.0;
    bc.nickname_prompt_enabled = dict_get(kwargs, "nickname_prompt_enabled") != 0.0;
    env->verbose = dict_get(kwargs, "verbose") != 0.0;
    bc.verbose = env->verbose;

    env->weight_exploration = (float)dict_get(kwargs, "weight_exploration");
    env->weight_catching = (float)dict_get(kwargs, "weight_catching");
    env->weight_seeing = (float)dict_get(kwargs, "weight_seeing");
    env->weight_events = (float)dict_get(kwargs, "weight_events");
    env->weight_leveling = (float)dict_get(kwargs, "weight_leveling");
    env->weight_healing = (float)dict_get(kwargs, "weight_healing");
    env->weight_hm_learned = (float)dict_get(kwargs, "weight_hm_learned");
    env->weight_pokecenter = (float)dict_get(kwargs, "weight_pokecenter");
    env->weight_pokecenter_visit = (float)dict_get(kwargs, "weight_pokecenter_visit");
    env->weight_run = (float)dict_get(kwargs, "weight_run");
    env->pokecenter_visit_steps = (int)dict_get(kwargs, "pokecenter_visit_steps");
    if (env->pokecenter_visit_steps <= 0)
        env->pokecenter_visit_steps = 1;
    env->weight_time = (float)dict_get(kwargs, "weight_time");
    env->weight_death = (float)dict_get(kwargs, "weight_death");
    bc.milestone_sample_prob = (float)dict_get(kwargs, "milestone_sample_prob");
    bc.milestones_enabled = dict_get(kwargs, "milestones_enabled") != 0.0;
    bc.event_milestones_enabled = dict_get(kwargs, "event_milestones_enabled") != 0.0;
    bc.town_milestones_enabled = dict_get(kwargs, "town_milestones_enabled") != 0.0;
    env->screen_obs_enabled = dict_get(kwargs, "screen_obs_enabled") != 0.0;
    env->map_exhaustion_obs_enabled = dict_get(kwargs, "map_exhaustion_obs_enabled") != 0.0;
    env->map_exhaustion_norm = (float)dict_get(kwargs, "map_exhaustion_norm");
    env->battle_status_obs_enabled = dict_get(kwargs, "battle_status_obs_enabled") != 0.0;
    env->progress_badges_bits_enabled = dict_get(kwargs, "progress_badges_bits_enabled") != 0.0;
    env->hm_bag_obs_enabled = dict_get(kwargs, "hm_bag_obs_enabled") != 0.0;
    env->blackout_map_obs_enabled = dict_get(kwargs, "blackout_map_obs_enabled") != 0.0;
    env->bag_obs_enabled = dict_get(kwargs, "bag_obs_enabled") != 0.0;
    env->key_item_obs_enabled = dict_get(kwargs, "key_item_obs_enabled") != 0.0;
    env->party_status_pp_obs_enabled = dict_get(kwargs, "party_status_pp_obs_enabled") != 0.0;
    env->battle_moveset_obs_enabled = dict_get(kwargs, "battle_moveset_obs_enabled") != 0.0;
    env->party_type_obs_enabled = dict_get(kwargs, "party_type_obs_enabled") != 0.0;
    env->progress_money_obs_enabled = dict_get(kwargs, "progress_money_obs_enabled") != 0.0;
    if (env->map_exhaustion_norm <= 0.0f)
        env->map_exhaustion_norm = 1.0f;
    env->exploration_cell_size = (int)dict_get(kwargs, "exploration_cell_size");
    if (env->exploration_cell_size <= 0)
        env->exploration_cell_size = 1;
    env->weight_exploration_anneal_start = (long)dict_get(kwargs, "weight_exploration_anneal_start");
    env->weight_exploration_anneal_end = (long)dict_get(kwargs, "weight_exploration_anneal_end");
    env->exploration_death_scaling_enabled = dict_get(kwargs, "exploration_death_scaling_enabled") != 0.0;

    DictItem* sp = dict_find(kwargs, "state_path");
    if (sp && sp->str && sp->str[0]) {
        strncpy(bc.state_path, sp->str, sizeof(bc.state_path) - 1);
    }
    strncpy(bc.rom_path, dict_get_str(kwargs, "rom_path"), sizeof(bc.rom_path) - 1);
    bc.audio_enabled = dict_get(kwargs, "audio_enabled") != 0.0;
    bc.frame_render_skip_enabled = dict_get(kwargs, "frame_render_skip_enabled") != 0.0;
    DictItem* nt = dict_find(kwargs, "vec_num_threads");
    bc.vec_num_threads = nt ? (int)nt->value : 1;

    bc.screen_obs_enabled = env->screen_obs_enabled;
    DictItem* fs = dict_find(kwargs, "fixed_starter_species");
    bc.fixed_starter_species = fs ? (int)fs->value : 0;
    DictItem* ad = dict_find(kwargs, "assets_dir");
    strncpy(bc.assets_dir, ad && ad->str && ad->str[0] ? ad->str : "vendor/redcore/assets", sizeof(bc.assets_dir) - 1);

    DictItem* bk = dict_find(kwargs, "backend");
    const char* backend = bk && bk->str && bk->str[0] ? bk->str : "emulator";
    if (strcmp(backend, "emulator") == 0) {
        env->be = pk_backend_emulator();
    } else if (strcmp(backend, "redcore") == 0) {
        env->be = pk_backend_redcore();
        if (!env->be) {
            fprintf(stderr, "pokered: built with --no-redcore; env.backend=redcore is unavailable\n");
            exit(1);
        }
    } else {
        fprintf(stderr, "pokered: unknown env.backend '%s' (expected emulator or redcore)\n", backend);
        exit(1);
    }
    env->impl = env->be->create(&bc);

    env->visited_coords = (uint8_t*)calloc(VISITED_BYTES, 1);
    env->visited_cells = (uint8_t*)calloc(VISITED_BYTES, 1);
    env->map_visited_counts = (uint16_t*)calloc(MAX_MAPS, sizeof(uint16_t));
    env->prev_events = (uint8_t*)calloc(EVENT_COUNT, sizeof(uint8_t));
    env->prev_action = -1;

    bool stream_enabled = dict_get(kwargs, "stream_enabled") != 0.0;
    int stream_interval = (int)dict_get(kwargs, "stream_interval");
    const char* stream_user = "User";
    DictItem* su = dict_find(kwargs, "stream_user");
    if (su && su->str && su->str[0]) stream_user = su->str;
    const char* stream_color = "#0000FF";
    DictItem* sc = dict_find(kwargs, "stream_color");
    if (sc && sc->str && sc->str[0]) stream_color = sc->str;
    stream_init(&env->stream, stream_enabled, stream_user, stream_color, env_id, stream_interval);
}

static void clear_visited(Env* env) {
    env->unique_coords_count = 0;
    memset(env->visited_coords, 0, VISITED_BYTES);
    memset(env->visited_cells, 0, VISITED_BYTES);
}

static void puf_reset_body(Env* env, bool do_full_reset) {
    bool full = env->full_reset && do_full_reset;
    env->reset_from_milestone = full ? false : env->reset_from_milestone;
    if (full)
        clear_visited(env);
    env->be->reset(env->impl, full, &env->rng, &env->reset_from_milestone);
    if (full)
        env->blackout_count = 0;

    memset(env->map_visited_counts, 0, MAX_MAPS * sizeof(*env->map_visited_counts));
    env->hm_rewarded_mask = 0;
    env->pokecenter_visit_step = -1;
    env->steps_since_new_tile = 0;
    env->be->snapshot(env->impl, &env->cur);
    env->prev = env->cur;
    update_observations(env);

    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count = 0;
    env->score = 0.0f;
    env->prev_action = -1;
    env->party_wiped = false;
    env->prev_event_sum = event_weighted_sum(env->cur.events);
    memset(&env->stats, 0, sizeof(EpisodeStats));
    for (size_t i = 0; i < EVENT_COUNT; ++i)
        env->prev_events[i] = env->cur.events[i];

    env->be->warmup(env->impl);
}
void puf_reset(Env* env) {
    env->be->acquire(env->impl);
    puf_reset_body(env, true);
    env->be->release(env->impl);
}

static void puf_step_body(Env* env) {
    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count++;
    env->total_agent_steps++;

    env->prev_action = (int)env->agents[0].actions[0];
    env->be->step(env->impl, env->prev_action, &env->cur);

    uint32_t coords_before_step = env->unique_coords_count;
    float reward = calculate_rewards(env);
    if (env->unique_coords_count > coords_before_step) {
        env->steps_since_new_tile = 0;
    } else {
        env->steps_since_new_tile++;
    }
    update_observations(env);
    env->agents[0].rewards[0] = reward;
    env->score += reward;

    stream_collect(&env->stream, env->cur.x, env->cur.y, env->cur.map_n);
    if (env->stream.interval > 0 && env->step_count % env->stream.interval == 0) {
        stream_flush(&env->stream);
    }

    bool party_alive = env->cur.party_count == 0 || env->cur.hp_fraction > 0.0f;
    bool died_this_step = false;
    if (party_alive) {
        env->party_wiped = false;
    } else if (!env->party_wiped) {
        env->party_wiped = true;
        died_this_step = true;
    }

    if (died_this_step) {
        env->agents[0].rewards[0] -= env->weight_death;
        env->score -= env->weight_death;
        clear_visited(env);
        memset(env->map_visited_counts, 0, MAX_MAPS * sizeof(*env->map_visited_counts));
        env->stats.total_death_signal += env->weight_death;
        env->blackout_count++;
        bool roll_full_reset = env->full_reset &&
            env->blackout_count >= env->full_reset_min_blackouts &&
            (env->full_reset_prob >= 1.0f ||
             (float)rand_r(&env->rng) / (float)RAND_MAX < env->full_reset_prob);
        if (roll_full_reset) {
            env->agents[0].terminals[0] = 1;
            add_log(env);
            puf_reset_body(env, true);
        } else if (env->be->blackout) {

            env->be->blackout(env->impl);
            env->party_wiped = false;
            env->be->snapshot(env->impl, &env->cur);
            env->prev = env->cur;
            update_observations(env);
        }
    }

    if (!env->agents[0].terminals[0] && env->stuck_reset_enabled &&
        env->stuck_reset_steps > 0 && env->steps_since_new_tile >= env->stuck_reset_steps) {

        env->agents[0].terminals[0] = 1;
        add_log(env);
        puf_reset_body(env, true);
    }

    if (!env->agents[0].terminals[0] &&
        env->max_episode_length > 0 && env->step_count >= env->max_episode_length) {
        env->agents[0].terminals[0] = 1;
        add_log(env);
        puf_reset_body(env, false);
    }
}
void puf_step(Env* env) {
    env->be->acquire(env->impl);
    puf_step_body(env);
    env->be->release(env->impl);
}

void puf_render(Env* env) {
    if (!IsWindowReady()) {
        SetTraceLogLevel(LOG_WARNING);
        InitWindow(SCREEN_WIDTH * 4, PK_FRAME_H * 4, "PufferLib Pokemon Red");
        SetTargetFPS(60);
        Image img = GenImageColor(PK_FRAME_W, PK_FRAME_H, BLACK);
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        env->render_texture = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }

    if (IsKeyDown(KEY_Z)) {
        env->agents[0].actions[0] = PKRED_ACTION_A;
    } else if (IsKeyDown(KEY_X)) {
        env->agents[0].actions[0] = PKRED_ACTION_B;
    } else if (IsKeyDown(KEY_RIGHT)) {
        env->agents[0].actions[0] = PKRED_ACTION_RIGHT;
    } else if (IsKeyDown(KEY_LEFT)) {
        env->agents[0].actions[0] = PKRED_ACTION_LEFT;
    } else if (IsKeyDown(KEY_UP)) {
        env->agents[0].actions[0] = PKRED_ACTION_UP;
    } else if (IsKeyDown(KEY_DOWN)) {
        env->agents[0].actions[0] = PKRED_ACTION_DOWN;
    } else {
        env->agents[0].actions[0] = -1;
    }

    if (IsKeyPressed(KEY_S)) {
        const char* save_path = "ocean/pokered/states/quicksave.state";
        if (env->be->quicksave && env->be->quicksave(env->impl, save_path)) {
            printf("pokered: state saved to %s\n", save_path);
        } else {
            fprintf(stderr, "pokered: failed to save state to %s\n", save_path);
        }
    }

    if (IsKeyPressed(KEY_O)) {
        env->show_obs_view = !env->show_obs_view;
    }

    if (!env->render_pixels) {
        env->render_pixels = (uint8_t*)calloc(PK_FRAME_W * PK_FRAME_H * 4, 1);
    }

    if (env->show_obs_view && env->agents[0].observations) {
        const obs_t* obs = env->agents[0].observations;
        for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
            for (int sx = 0; sx < SCALED_WIDTH; sx++) {
                uint8_t gray = (uint8_t)obs[sy * SCALED_WIDTH + sx];
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int x = sx * 2 + dx;
                        int y = sy * 2 + dy;
                        uint8_t* out = &env->render_pixels[(y * PK_FRAME_W + x) * 4];
                        out[0] = gray;
                        out[1] = gray;
                        out[2] = gray;
                        out[3] = 255;
                    }
                }
            }
        }

        for (int by = 0; by < BLOCKS_TALL; by++) {
            for (int bx = 0; bx < BLOCKS_WIDE; bx++) {
                bool is_player = bx == PLAYER_BLOCK_COL && by == PLAYER_BLOCK_ROW;
                bool visited = !is_player &&
                    obs[VISITED_MASK_OFFSET + by * BLOCKS_WIDE + bx] != 0.0f;
                if (!visited && !is_player)
                    continue;
                uint8_t tint_r = is_player ? 220 : 0;
                uint8_t tint_g = is_player ? 40 : 200;
                for (int py = 0; py < BLOCK_PIXELS; py++) {
                    for (int px = 0; px < BLOCK_PIXELS; px++) {
                        int x = bx * BLOCK_PIXELS + px;
                        int y = by * BLOCK_PIXELS + py;
                        uint8_t* out = &env->render_pixels[(y * PK_FRAME_W + x) * 4];
                        out[0] = (uint8_t)((out[0] + tint_r) / 2);
                        out[1] = (uint8_t)((out[1] + tint_g) / 2);
                        out[2] = (uint8_t)(out[2] / 2);
                    }
                }
            }
        }
        UpdateTexture(env->render_texture, env->render_pixels);
    } else if (env->be->frame_rgba && env->be->frame_rgba(env->impl, env->render_pixels)) {
        UpdateTexture(env->render_texture, env->render_pixels);
    }

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(env->render_texture,
        (Rectangle){0, 0, (float)PK_FRAME_W, (float)PK_FRAME_H},
        (Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()},
        (Vector2){0, 0}, 0.0f, WHITE);
    EndDrawing();
    puf_web_vsync();
}

void puf_close(Env* env) {
    if (IsWindowReady()) {
        CloseWindow();
    }
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

void puf_log(Log* log, Dict* out) {
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "level_sum", log->level_sum);
    dict_set(out, "unique_coords", log->unique_coords);

    dict_set(out, "pkmn1_lvl", log->pkmn1_lvl);
    dict_set(out, "explore_signal", log->explore_signal);
    dict_set(out, "pkmn2_lvl", log->pkmn2_lvl);
    dict_set(out, "catching_signal", log->catching_signal);
    dict_set(out, "pkmn3_lvl", log->pkmn3_lvl);
    dict_set(out, "seeing_signal", log->seeing_signal);
    dict_set(out, "pkmn4_lvl", log->pkmn4_lvl);
    dict_set(out, "events_signal", log->events_signal);
    dict_set(out, "pkmn5_lvl", log->pkmn5_lvl);
    dict_set(out, "leveling_signal", log->leveling_signal);
    dict_set(out, "pkmn6_lvl", log->pkmn6_lvl);
    dict_set(out, "healing_signal", log->healing_signal);
    dict_set(out, "pokedex_owned", log->pokedex_owned);
    dict_set(out, "hm_learned_signal", log->hm_learned_signal);
    dict_set(out, "pokedex_seen", log->pokedex_seen);
    dict_set(out, "pokecenter_signal", log->pokecenter_signal);
    dict_set(out, "party_count", log->party_count);
    dict_set(out, "pokecenter_visit_signal", log->pokecenter_visit_signal);
    dict_set(out, "run_signal", log->run_signal);
    dict_set(out, "milestone_pool_size", log->milestone_pool_size);
    dict_set(out, "death_signal", log->death_signal);

    dict_set(out, "reset_from_milestone", log->reset_from_milestone);
    dict_set(out, "money", log->money);
    dict_set(out, "badges", log->badges);
    dict_set(out, "event_sum", log->event_sum);

    dict_set(out, "n", log->n);
}

#endif
