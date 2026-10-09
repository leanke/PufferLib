#ifndef POKERED_H
#define POKERED_H

#include <math.h>

#include "raylib.h"
#ifdef POKERED_OBS_U8
typedef unsigned char obs_t;
#else
typedef float obs_t;
#endif
#include "pufferenv.h"

#include "data/ram_map.h"
#include "data/events.h"
#include "data/utils.h"
#include "backend/backend.h"
#include "backend/pksnapshot.h"

#define ACT_SIZES {PKRED_ACTION_COUNT}
#define OBS_SIZE TOTAL_OBSERVATIONS
#define NUM_ATNS 1

#define PK_REWARD_SIGNALS(X)                                       \
    X(explore, signal_exploration, "weight_exploration", 0, 1)    \
    X(catching, signal_catching, "weight_catching", 0, 1)          \
    X(seeing, signal_seeing, "weight_seeing", 0, 1)                \
    X(leveling, signal_leveling, "weight_leveling", 0, 1)          \
    X(events, signal_events, "weight_events", 0, 1)                \
    X(battling, signal_battling, "weight_battling", 0, 1)          \
    X(death, signal_none, "weight_death", 1, 0)                    \
    X(fleeing, signal_fleeing, "weight_fleeing", 1, 1)             \
    X(healing, signal_healing, "weight_healing", 0, 1)             \
    X(hm_taught, signal_hm_taught, "weight_hm_taught", 0, 1)       \
    X(hm_used, signal_hm_used, "weight_hm_used", 0, 1)

#define PK_SIG_FIELD(name, fn, key, penalty, summed) float name;
#define PK_SIG_ENUM(name, fn, key, penalty, summed) PK_SIG_##name,
typedef struct {
    PK_REWARD_SIGNALS(PK_SIG_FIELD)
} RewardTotals;
enum { PK_REWARD_SIGNALS(PK_SIG_ENUM) PK_SIG_COUNT };

struct Log {
    float episode_length;
    float episode_return;
    float perf;
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
    int tag;
    int boundary_reached;
    unsigned int rng;

    const PkBackend *be;
    void *impl;
    PkSnapshot cur;
    PkSnapshot prev;
    PkEventTracker tracker;
    PkScenario scenario;

    int32_t step_count;
    float score;
    int prev_action;
    RewardTotals totals;
    PokeredStream stream;

    uint8_t *visited_coords;
    uint8_t *visited_cells;
    uint16_t *map_visited_counts;
    uint32_t unique_coords_count;
    uint8_t *prev_events;
    uint8_t *paid_events;
    bool event_reward_once;
    bool start_select_enabled;

    PkEpisode episode;

    float weight[PK_SIG_COUNT];
    bool hm_used_once;
    float healing_health_left;
    float healing_pp_left;
    uint8_t hm_used_mask;
    int exploration_cell_size;
    bool exploration_death_scaling_enabled;

    bool screen_obs_enabled;
    bool obs_tiles;
    float map_exhaustion_norm;

    bool verbose;

    float *screen_buf;
    Texture2D render_texture;
    uint8_t *render_pixels;
    bool show_obs_view;
};
typedef Env Pokered;

static int g_pokered_obs_tiles = -1;

static inline bool is_directional_action(int action) {
    return action >= PKRED_ACTION_RIGHT && action <= PKRED_ACTION_DOWN;
}

static PkRam env_ram(Env *env) {
    PkRam ram = {env->impl, env->be->peek, env->be->poke};
    return ram;
}

static void env_snapshot(Env *env) {
    env->be->snapshot(env->impl, &env->cur);
    pk_events_apply(&env->tracker, &env->cur);
}

static void env_rebase(Env *env, bool clear_cut) {
    pk_events_rebase(&env->tracker);
    if (clear_cut) {
        PkRam ram = env_ram(env);
        pk_ram_clear_cut(&ram, &env->tracker);
    }
}

static bool env_state_load(Env *env, const void *buf) {
    if (!env->be->state_load(env->impl, buf))
        return false;
    env_rebase(env, true);
    return true;
}

static void env_try_milestone_start(Env *env) {
    const uint8_t *blob = pk_episode_pick_milestone(&env->episode, &env->rng);
    if (blob && env_state_load(env, blob))
        env->episode.from_milestone = true;
}

static void env_capture_milestones(Env *env) {
    pk_episode_capture(&env->episode, &env->cur, &env->prev, env->be, env->impl, &env->rng, env->verbose);
}

static void env_advance(Env *env, int action) {
    PkRam ram = env_ram(env);
    pk_ram_apply_scenario(&ram, &env->scenario, &env->cur);
    env->be->step(env->impl, pk_action_buttons(action));
    pk_ram_track_cut(&ram, &env->tracker, &env->cur);
    pk_events_stepped(&env->tracker);
}

#include "pokered_obs.h"
#include "pokered_rew.h"

static void clear_visited(Env *env) {
    env->unique_coords_count = 0;
    memset(env->visited_coords, 0, VISITED_BYTES);
    memset(env->visited_cells, 0, VISITED_BYTES);
    memset(env->map_visited_counts, 0, MAX_MAPS * sizeof(*env->map_visited_counts));
}

static void add_log(Env *env) {
    const PkSnapshot *s = &env->cur;
    Log *log = &env->log;

    log->episode_length += env->step_count;
    log->episode_return += env->score;
    log->perf += env->score;
#define PK_SIG_ADD(name, fn, key, penalty, summed) log->reward.name += env->totals.name;
    PK_REWARD_SIGNALS(PK_SIG_ADD)
#undef PK_SIG_ADD
    log->level_sum += party_level_sum(s);
    for (int i = 0; i < PARTY_SIZE; i++)
        log->pkmn_lvl[i] += s->party[i].level;
    log->party_count += s->party_count;
    log->badges += __builtin_popcount(s->badges);
    log->event_sum += completed_event_count(s);
    log->unique_coords += env->unique_coords_count;
    log->map_exhaustion +=
        fminf(1.0f, (float)env->map_visited_counts[s->map_n] / env->map_exhaustion_norm);
    log->pokedex_owned += s->pokedex_owned_count;
    log->pokedex_seen += s->pokedex_seen_count;
    log->milestone_pool_size += env->episode.milestones_enabled ? (float)pk_pool_slots_filled(env->episode.pool) : 0.0f;
    log->reset_from_milestone += env->episode.from_milestone ? 1.0f : 0.0f;
    log->n++;
}

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

static float read_signal_weight(Dict *kw, const char *key, int sig, bool penalty) {
    static bool warned[PK_SIG_COUNT];
    float w = kw_float(kw, key);
    if (!penalty)
        return w;
    if (w > 0.0f && !warned[sig]) {
        warned[sig] = true;
        fprintf(stderr, "pokered: %s = %g is a penalty; using %g (write it negative in the ini)\n", key, w, -w);
    }
    return -fabsf(w);
}

static void read_reward_config(Env *env, Dict *kw) {
#define PK_SIG_READ(name, fn, key, penalty, summed) \
    env->weight[PK_SIG_##name] = read_signal_weight(kw, key, PK_SIG_##name, penalty);
    PK_REWARD_SIGNALS(PK_SIG_READ)
#undef PK_SIG_READ
    env->hm_used_once = kw_bool(kw, "hm_used_once");
    env->healing_health_left = kw_float(kw, "healing_health_left");
    env->healing_pp_left = kw_float(kw, "healing_pp_left");
    if (env->healing_health_left < 0.0f || env->healing_health_left > 1.0f || env->healing_pp_left < 0.0f ||
        env->healing_pp_left > 1.0f) {
        fprintf(stderr, "pokered: healing_health_left and healing_pp_left must be in [0, 1]\n");
        exit(1);
    }
    env->event_reward_once = kw_bool(kw, "event_reward_once");
    env->start_select_enabled = kw_bool(kw, "start_select_enabled");
    env->exploration_cell_size = kw_int(kw, "exploration_cell_size");
    if (env->exploration_cell_size <= 0)
        env->exploration_cell_size = 1;
    env->exploration_death_scaling_enabled = kw_bool(kw, "exploration_death_scaling_enabled");
}

static void read_observation_config(Env *env, Dict *kw) {
    env->screen_obs_enabled = kw_bool(kw, "screen_obs_enabled");
    const char *mode = kw_str(kw, "obs_mode", "screen");
    if (strcmp(mode, "screen") && strcmp(mode, "tiles")) {
        fprintf(stderr, "pokered: obs_mode must be \"screen\" or \"tiles\", got \"%s\"\n", mode);
        exit(1);
    }
    env->obs_tiles = !strcmp(mode, "tiles");
    if (g_pokered_obs_tiles >= 0 && g_pokered_obs_tiles != (int)env->obs_tiles) {
        fprintf(stderr, "pokered: every env in a process must use the same obs_mode\n");
        exit(1);
    }
    g_pokered_obs_tiles = env->obs_tiles;
    env->map_exhaustion_norm = kw_float(kw, "map_exhaustion_norm");
    if (env->map_exhaustion_norm <= 0.0f)
        env->map_exhaustion_norm = 1.0f;
}

static void read_episode_config(Env *env, Dict *kw) {
    PkEpisode *ep = &env->episode;
    ep->max_length = (int32_t)dict_get(kw, "max_episode_length");
    ep->full_reset = kw_bool(kw, "full_reset");
    env->verbose = kw_bool(kw, "verbose");
    ep->milestones_enabled = kw_bool(kw, "milestones_enabled");
    ep->milestone_reset_prob = kw_float(kw, "milestone_reset_prob");
    ep->milestone_states_per_slot = kw_int(kw, "milestone_states_per_slot");
    ep->milestone_progress_bias = kw_float(kw, "milestone_progress_bias");
}

static bool dict_lookup(void *ctx, const char *key, double *num, const char **str) {
    DictItem *item = dict_find((Dict *)ctx, key);
    if (!item)
        return false;
    if (num) *num = item->value;
    if (str) *str = item->str;
    return true;
}

static void read_backend_config(Env *env, Dict *kw, PkBackendConfig *bc) {
    memset(bc, 0, sizeof(*bc));
    bc->env_id = env->rng;
    bc->verbose = env->verbose;
    bc->screen_obs_enabled = env->screen_obs_enabled && !env->obs_tiles;

    bc->frameskip = kw_int(kw, "frameskip");
    bc->press_frames = kw_int(kw, "press_frames");
    bc->headless = kw_bool(kw, "headless");

    copy_str(bc->rom_path, sizeof(bc->rom_path), dict_get_str(kw, "rom_path"));
    copy_str(bc->state_path, sizeof(bc->state_path), kw_str(kw, "state_path", ""));
    bc->pkstate_cache_enabled = kw_bool(kw, "pkstate_cache_enabled");
}

static void read_scenario_config(Env *env, Dict *kw) {
    env->scenario.disable_wild_until_badge = kw_bool(kw, "disable_wild_until_badge");
    env->scenario.route22_rival_beaten = kw_bool(kw, "route22_rival_beaten");
    env->scenario.route22_rival_2nd_beaten = kw_bool(kw, "route22_rival_2nd_beaten");
    int text_speed = kw_int(kw, "text_speed"), animation = kw_int(kw, "battle_animation"),
        style = kw_int(kw, "battle_style");
    if (text_speed < 0 || text_speed > 3 || animation < 0 || animation > 2 || style < 0 || style > 2) {
        fprintf(stderr, "pokered: text_speed (0-3), battle_animation (0-2) or battle_style (0-2) out of range\n");
        exit(1);
    }
    env->scenario.text_speed = (uint8_t)text_speed;
    env->scenario.battle_animation = (uint8_t)animation;
    env->scenario.battle_style = (uint8_t)style;
    env->scenario.rng_randomize_on_reset = kw_bool(kw, "rng_randomize_on_reset");
}

static const PkBackend *select_backend(Dict *kw) {
    const char *name = kw_str(kw, "backend", "emulator");
    const PkBackend *be = pk_backend_find(name);
    if (!be) {
        fprintf(stderr, "pokered: env.backend '%s' is not available; this build has:", name);
        for (int i = 0; i < pk_backend_count(); i++)
            fprintf(stderr, " %s", pk_backend_at(i)->name);
        fprintf(stderr, "\n(a backend skipped with build.sh --no-<name> is not linked in)\n");
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
    read_scenario_config(env, kwargs);

    PkBackendConfig bc;
    read_backend_config(env, kwargs, &bc);
    PkOptions opts = {kwargs, dict_lookup};
    env->be = select_backend(kwargs);
    env->impl = env->be->create(&bc, &opts);
    pk_episode_init(&env->episode, env->be);

    env->visited_coords = (uint8_t *)calloc(VISITED_BYTES, 1);
    env->visited_cells = (uint8_t *)calloc(VISITED_BYTES, 1);
    env->map_visited_counts = (uint16_t *)calloc(MAX_MAPS, sizeof(uint16_t));
    env->prev_events = (uint8_t *)calloc(PK_EVENT_FLAG_BYTES, 1);
    env->paid_events = (uint8_t *)calloc(PK_EVENT_FLAG_BYTES, 1);
    pk_events_table_init();
    env->prev_action = -1;

    stream_init(&env->stream, kw_bool(kwargs, "stream_enabled"),
                kw_str(kwargs, "stream_user", "User"), kw_str(kwargs, "stream_color", "#0000FF"),
                env->rng, kw_int(kwargs, "stream_interval"));
}

static void update_action_mask(Env *env) {
    unsigned char *mask = env->agents[0].action_mask;
    if (!mask)
        return;
    mask[PKRED_ACTION_START] = env->start_select_enabled;
    mask[PKRED_ACTION_SELECT] = env->start_select_enabled;
}

static void puf_reset_body(Env *env, bool do_full_reset) {
    bool full = env->episode.full_reset && do_full_reset;
    if (full) {
        env->episode.from_milestone = false;
        clear_visited(env);
    }
    env->be->reset(env->impl, full);
    env_rebase(env, full);
    if (full) {
        env_try_milestone_start(env);
        PkRam ram = env_ram(env);
        pk_ram_prepare_start(&ram, &env->scenario, &env->rng);
        env->episode.blackouts = 0;
    }

    memset(env->map_visited_counts, 0, MAX_MAPS * sizeof(*env->map_visited_counts));
    env_snapshot(env);
    env->prev = env->cur;
    update_observations(env);

    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count = 0;
    env->score = 0.0f;
    env->prev_action = -1;
    env->hm_used_mask = 0;
    memset(&env->totals, 0, sizeof(env->totals));
    memcpy(env->prev_events, env->cur.event_flags, PK_EVENT_FLAG_BYTES);
    memcpy(env->paid_events, env->cur.event_flags, PK_EVENT_FLAG_BYTES);

    env->be->warmup(env->impl);
    update_action_mask(env);
}

void puf_reset(Env *env) {
    env->be->acquire(env->impl);
    puf_reset_body(env, true);
    env->be->release(env->impl);
}

static void end_episode(Env *env) {
    add_log(env);
    puf_reset_body(env, true);
    env->agents[0].terminals[0] = 1;
}

static void handle_blackout(Env *env) {
    float w = env->weight[PK_SIG_death];
    env->agents[0].rewards[0] += w;
    env->score += w;
    env->totals.death += w;
    env->episode.blackouts++;
    clear_visited(env);
}

static void puf_step_body(Env *env) {
    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count++;

    env->prev_action = (int)env->agents[0].actions[0];
    if (!env->start_select_enabled && env->prev_action >= PKRED_ACTION_START)
        env->prev_action = -1;
    env_advance(env, env->prev_action);
    env_snapshot(env);
    env_capture_milestones(env);

    float reward = calculate_rewards(env);
    update_observations(env);
    update_action_mask(env);
    env->agents[0].rewards[0] = reward;
    env->score += reward;

    stream_collect(&env->stream, env->cur.x, env->cur.y, env->cur.map_n);
    if (env->stream.interval > 0 && env->step_count % env->stream.interval == 0)
        stream_flush(&env->stream);

    if (env->cur.step_events & PK_EV_BLACKOUT)
        handle_blackout(env);

    if (env->episode.max_length > 0 && env->step_count >= env->episode.max_length)
        end_episode(env);
}

void puf_step(Env *env) {
    env->be->acquire(env->impl);
    puf_step_body(env);
    env->be->release(env->impl);
}

static void render_init(Env* env, int width, int height, unsigned flags) {
    SetTraceLogLevel(LOG_WARNING);
    if (flags)
        SetConfigFlags(flags);
    InitWindow(width, height, "PufferLib Pokemon Red");
    SetTargetFPS(60);
    Image img = GenImageColor(PK_FRAME_W, PK_FRAME_H, BLACK);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    env->render_texture = LoadTextureFromImage(img);
    UnloadImage(img);
}

static void obs_view_screen(Env* env, const obs_t* obs) {
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
}

static Color tile_view_color(int tile) {
    if (tile == PKRED_TILE_BLANK)
        return WHITE;
    float hue = fmodf((float)tile * 137.508f, 360.0f);
    return ColorFromHSV(hue, 0.35f + 0.25f * (float)((tile >> 4) & 1), 0.95f - 0.25f * (float)((tile >> 5) & 1));
}

static void obs_view_tiles(Env* env, const obs_t* obs) {
    for (int r = 0; r < PK_TILE_MAP_H; r++) {
        for (int c = 0; c < PK_TILE_MAP_W; c++) {
            int cell = r * PK_TILE_MAP_W + c;
            Color col = tile_view_color((int)obs[TILE_OBS_OFFSET + cell]);
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint8_t* out = &env->render_pixels[((r * 8 + y) * PK_FRAME_W + c * 8 + x) * 4];
                    bool grid = x == 7 || y == 7;
                    out[0] = grid ? (uint8_t)(col.r * 3 / 4) : col.r;
                    out[1] = grid ? (uint8_t)(col.g * 3 / 4) : col.g;
                    out[2] = grid ? (uint8_t)(col.b * 3 / 4) : col.b;
                    out[3] = 255;
                }
            }
        }
    }
}

static void obs_view_sprites(Env* env, const obs_t* obs) {
    for (int cell = 0; cell < PK_TILE_MAP_CELLS; cell++) {
        int sprite = (int)obs[SPRITE_OBS_OFFSET + cell];
        if (!sprite)
            continue;
        Color tint = sprite == TILE_SPRITE_PLAYER ? (Color){230, 30, 30, 255} : (Color){200, 0, 200, 255};
        int r = cell / PK_TILE_MAP_W, c = cell % PK_TILE_MAP_W;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint8_t* out = &env->render_pixels[((r * 8 + y) * PK_FRAME_W + c * 8 + x) * 4];
                out[0] = (uint8_t)((out[0] + 2 * tint.r) / 3);
                out[1] = (uint8_t)((out[1] + 2 * tint.g) / 3);
                out[2] = (uint8_t)((out[2] + 2 * tint.b) / 3);
            }
        }
    }
}

static void obs_view_visited(Env* env, const obs_t* obs) {
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
}

static void draw_tile_ids(Env* env, Rectangle dst) {
    const obs_t* obs = env->agents[0].observations;
    float cw = dst.width / PK_TILE_MAP_W, ch = dst.height / PK_TILE_MAP_H;
    int font = (int)(ch * 0.4f);
    if (font < 8)
        return;
    for (int r = 0; r < PK_TILE_MAP_H; r++) {
        for (int c = 0; c < PK_TILE_MAP_W; c++) {
            char label[4];
            snprintf(label, sizeof(label), "%02X", (unsigned)obs[TILE_OBS_OFFSET + r * PK_TILE_MAP_W + c]);
            const uint8_t* px = &env->render_pixels[((r * 8 + 3) * PK_FRAME_W + c * 8 + 3) * 4];
            Color ink = px[0] * 77 + px[1] * 150 + px[2] * 29 > 128 * 256 ? BLACK : WHITE;
            int w = MeasureText(label, font);
            DrawText(label, (int)(dst.x + c * cw + (cw - w) / 2), (int)(dst.y + r * ch + (ch - font) / 2), font, ink);
        }
    }
}

static void render_draw(Env* env, Rectangle dst) {
    if (!env->render_pixels) {
        env->render_pixels = (uint8_t*)calloc(PK_FRAME_W * PK_FRAME_H * 4, 1);
    }
    bool obs_view = env->show_obs_view && env->agents[0].observations;
    if (obs_view) {
        const obs_t* obs = env->agents[0].observations;
        if (env->obs_tiles)
            obs_view_tiles(env, obs);
        else
            obs_view_screen(env, obs);
        obs_view_visited(env, obs);
        if (env->obs_tiles)
            obs_view_sprites(env, obs);
        UpdateTexture(env->render_texture, env->render_pixels);
    } else if (env->be->frame_rgba(env->impl, env->render_pixels)) {
        UpdateTexture(env->render_texture, env->render_pixels);
    }
    DrawTexturePro(env->render_texture, (Rectangle){0, 0, (float)PK_FRAME_W, (float)PK_FRAME_H}, dst,
        (Vector2){0, 0}, 0.0f, WHITE);
    if (obs_view && env->obs_tiles)
        draw_tile_ids(env, dst);
}

void puf_render(Env* env) {
    if (!IsWindowReady()) {
        render_init(env, SCREEN_WIDTH * 4, PK_FRAME_H * 4, 0);
    }
    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }

    if (IsKeyPressed(KEY_Z) || IsKeyPressedRepeat(KEY_Z)) {
        env->agents[0].actions[0] = PKRED_ACTION_A;
    } else if (IsKeyPressed(KEY_X) || IsKeyPressedRepeat(KEY_X)) {
        env->agents[0].actions[0] = PKRED_ACTION_B;
    } else if (IsKeyPressed(KEY_ENTER) || IsKeyPressedRepeat(KEY_ENTER)) {
        env->agents[0].actions[0] = PKRED_ACTION_START;
    } else if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        env->agents[0].actions[0] = PKRED_ACTION_SELECT;
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
        const char* pk_path = "ocean/pokered/states/quicksave.pkstate";
        bool saved = false;
        if (env->be->quicksave && env->be->quicksave(env->impl, save_path)) {
            printf("pokered: state saved to %s\n", save_path);
            saved = true;
        }
        PkState pk;
        if (env->be->export_state(env->impl, &pk) &&
            pk_state_write(pk_path, &pk)) {
            printf("pokered: state saved to %s\n", pk_path);
            saved = true;
        }
        if (!saved)
            fprintf(stderr, "pokered: backend '%s' cannot save its state\n", env->be->name);
    }

    if (IsKeyPressed(KEY_O)) {
        env->show_obs_view = !env->show_obs_view;
    }

    BeginDrawing();
    ClearBackground(BLACK);
    render_draw(env, (Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()});
    EndDrawing();
    puf_web_vsync();
}

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
    free(env->paid_events);
    free(env->episode.buf);
    free(env->screen_buf);
}

void puf_log(Log *log, Dict *out) {
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "perf", log->perf);
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

#define PK_SIG_LOG(name, fn, key, penalty, summed) dict_set(out, #name "_signal", log->reward.name);
    PK_REWARD_SIGNALS(PK_SIG_LOG)
#undef PK_SIG_LOG

    dict_set(out, "milestone_pool_size", log->milestone_pool_size);
    dict_set(out, "reset_from_milestone", log->reset_from_milestone);
    dict_set(out, "n", log->n);
}

#endif
