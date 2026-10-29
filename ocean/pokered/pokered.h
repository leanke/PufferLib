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
#include "pokered_stream.h"
#include "backend/backend.h"
#include "backend/pksnapshot.h"
#include "pokered_items.h"
#include "data/species_names.h"

#define ACT_SIZES {PKRED_ACTION_COUNT}
#define NUM_ATNS 1

#define OBS_SIZE TOTAL_OBSERVATIONS

#define PK_REWARD_SIGNALS(X)                                       \
    X(explore, signal_exploration, "weight_exploration", 0, 1)  \
    X(catching, signal_catching, "weight_catching", 0, 1)          \
    X(seeing, signal_seeing, "weight_seeing", 0, 1)                \
    X(leveling, signal_leveling, "weight_leveling", 0, 1)          \
    X(events, signal_events, "weight_events", 0, 1)                \
    X(battling, signal_battling, "weight_battling", 0, 1)          \
    X(death, signal_none, "weight_death", 1, 0)                    \
    X(fleeing, signal_fleeing, "weight_fleeing", 1, 1)             \
    X(ghost_stall, signal_ghost_stall, "weight_ghost_stall", 1, 1) \
    X(healing, signal_healing, "weight_healing", 0, 1)             \
    X(item_used, signal_item_used, "weight_item_used", 0, 1)       \
    X(hm, signal_hm, "weight_hm", 0, 1)                            \
    X(key_items, signal_key_items, "weight_key_items", 0, 1)       \
    X(badges, signal_badges, "weight_badges", 0, 1)

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
    float progress;
    float progress_gained;
    float unique_coords;
    float pokedex_owned;
    float pokedex_seen;
    float money;
    float money_gained;
    float coins;
    float coins_gained;
    RewardTotals reward;
    float battle_steps;
    float ghost_battle_steps;
    float steps_since_reward;
    float episode_extensions;
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
    int32_t battle_steps, ghost_battle_steps, last_reward_step;
    float score;
    int progress_start;
    uint32_t money_start;
    uint16_t coins_start;
    uint8_t maps_entered[32];
    int prev_action;
    int requested_action;
    RewardTotals totals;
    PokeredStream stream;

    uint8_t *visited_coords;
    uint8_t *visited_cells;
    uint32_t unique_coords_count;
    uint8_t dex_owned_best, dex_seen_best;
    uint8_t *prev_events;
    uint8_t *paid_events;
    bool event_reward_once;
    bool flee_exempt_ghost;
    bool blackout_clear_visited;
    bool obs_progress_gating;
    uint8_t key_progress;
    bool start_select_enabled;
    bool hm_actions;

    PkEpisode episode;

    float weight[PK_SIG_COUNT];
    int hm_use_cap;
    float healing_health_left;
    float healing_pp_left;
    int hm_use_count[5];
    uint8_t key_item_mask;
    PkItemUse item_use;
    uint64_t tm_pending;
    int tm_pending_steps;
    int exploration_cell_size;


    bool verbose;

    Texture2D render_texture;
    uint8_t *render_pixels;
    bool show_obs_view;
    bool render_panel;
    RewardTotals step_totals;
};
typedef Env Pokered;

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
    env->maps_entered[env->cur.map_n >> 3] |= (uint8_t)(1u << (env->cur.map_n & 7));
}

static void env_rebase(Env *env, bool clear_cut) {
    pk_events_rebase(&env->tracker);
    if (clear_cut) {
        PkRam ram = env_ram(env);
        pk_ram_clear_cut(&ram, &env->tracker);
    }
}


static void env_advance_buttons(Env *env, unsigned buttons) {
    PkRam ram = env_ram(env);
    pk_ram_apply_scenario(&ram, &env->scenario, &env->cur);
    env->be->step(env->impl, buttons);
    pk_ram_track_cut(&ram, &env->tracker, &env->cur);
    pk_events_stepped(&env->tracker);
}

static void env_advance(Env *env, int action) { env_advance_buttons(env, pk_action_buttons(action)); }

#include "pokered_progress.h"
#include "pokered_obs.h"
#include "pokered_rew.h"
#include "pokered_hm.h"

static void clear_visited(Env *env) {
    env->unique_coords_count = 0;
    memset(env->visited_coords, 0, VISITED_BYTES);
    memset(env->visited_cells, 0, VISITED_BYTES);
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
    int progress = pk_progress(s, env->maps_entered);
    log->progress += progress;
    log->progress_gained += progress - env->progress_start;
    log->unique_coords += env->unique_coords_count;
    log->pokedex_owned += s->pokedex_owned_count;
    log->pokedex_seen += s->pokedex_seen_count;
    log->money += s->money;
    log->money_gained += (float)s->money - (float)env->money_start;
    log->coins += s->coins;
    log->coins_gained += (float)s->coins - (float)env->coins_start;
    log->battle_steps += env->battle_steps;
    log->ghost_battle_steps += env->ghost_battle_steps;
    log->steps_since_reward += env->step_count - env->last_reward_step;
    log->episode_extensions += env->episode.extensions;
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
    env->hm_use_cap = kw_int(kw, "hm_use_cap");
    env->healing_health_left = kw_float(kw, "healing_health_left");
    env->healing_pp_left = kw_float(kw, "healing_pp_left");
    if (env->healing_health_left < 0.0f || env->healing_health_left > 1.0f || env->healing_pp_left < 0.0f ||
        env->healing_pp_left > 1.0f) {
        fprintf(stderr, "pokered: healing_health_left and healing_pp_left must be in [0, 1]\n");
        exit(1);
    }
    env->event_reward_once = kw_bool(kw, "event_reward_once");
    env->flee_exempt_ghost = kw_bool(kw, "flee_exempt_ghost");
    env->blackout_clear_visited = kw_bool(kw, "blackout_clear_visited");
    env->obs_progress_gating = kw_bool(kw, "obs_progress_gating");
    env->start_select_enabled = kw_bool(kw, "start_select_enabled");
    env->hm_actions = kw_bool(kw, "hm_actions");
    env->exploration_cell_size = kw_int(kw, "exploration_cell_size");
    if (env->exploration_cell_size <= 0)
        env->exploration_cell_size = 1;
}

static void read_episode_config(Env *env, Dict *kw) {
    PkEpisode *ep = &env->episode;
    ep->base_length = (int32_t)dict_get(kw, "max_episode_length");
    ep->max_length = ep->base_length;
    ep->full_reset = kw_bool(kw, "full_reset");
    env->verbose = kw_bool(kw, "verbose");
    DictItem *panel = dict_find(kw, "render_panel");
    env->render_panel = panel && panel->value != 0.0;
    ep->milestone_key_items = kw_bool(kw, "milestone_key_items");
    ep->milestone_extend_episode = kw_bool(kw, "milestone_extend_episode");
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

    bc->frameskip = kw_int(kw, "frameskip");
    bc->press_frames = kw_int(kw, "press_frames");
    bc->headless = kw_bool(kw, "headless");

    copy_str(bc->rom_path, sizeof(bc->rom_path), dict_get_str(kw, "rom_path"));
    copy_str(bc->state_path, sizeof(bc->state_path), kw_str(kw, "state_path", ""));
    bc->pkstate_cache_enabled = kw_bool(kw, "pkstate_cache_enabled");
}

static void read_scenario_config(Env *env, Dict *kw) {
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
    read_scenario_config(env, kwargs);

    PkBackendConfig bc;
    read_backend_config(env, kwargs, &bc);
    PkOptions opts = {kwargs, dict_lookup};
    env->be = select_backend(kwargs);
    env->impl = env->be->create(&bc, &opts);

    env->visited_coords = (uint8_t *)calloc(VISITED_BYTES, 1);
    env->visited_cells = (uint8_t *)calloc(VISITED_BYTES, 1);
    env->prev_events = (uint8_t *)calloc(PK_EVENT_FLAG_BYTES, 1);
    env->paid_events = (uint8_t *)calloc(PK_EVENT_FLAG_BYTES, 1);
    pk_events_table_init();
    env->prev_action = -1;
    env->requested_action = -1;

    StreamConfig sc;
    sc.enabled = kw_bool(kwargs, "stream_enabled");
    sc.async = kw_bool(kwargs, "stream_async");
    sc.interval = kw_int(kwargs, "stream_interval");
    sc.window = kw_int(kwargs, "stream_window");
    sc.agents = kw_int(kwargs, "stream_agents");
    sc.trainer_class = kw_bool(kwargs, "stream_trainer_class");
    sc.sprites = kw_bool(kwargs, "stream_sprites");
    sc.color_progress = kw_bool(kwargs, "stream_color_progress");
    sc.connections = kw_int(kwargs, "stream_connections");
    sc.queue_mb = kw_int(kwargs, "stream_queue_mb");
    sc.user = kw_str(kwargs, "stream_user", "User");
    sc.color = kw_str(kwargs, "stream_color", "#0000FF");
    sc.url = kw_str(kwargs, "stream_url", "");
    stream_init(&env->stream, &sc);
}

static bool action_available(Env *env, int action) {
    if (action == PKRED_ACTION_START || action == PKRED_ACTION_SELECT)
        return env->start_select_enabled;
    int hm = action - PKRED_ACTION_HM_CUT;
    if (hm >= 0 && hm < PK_NUM_HMS)
        return env->hm_actions && hm_action_ready(&env->cur, hm);
    return true;
}

static void update_action_mask(Env *env) {
    unsigned char *mask = env->agents[0].action_mask;
    if (!mask)
        return;
    for (int a = PKRED_ACTION_START; a < PKRED_ACTION_COUNT; a++)
        mask[a] = action_available(env, a);
}

static void puf_reset_body(Env *env, bool do_full_reset) {
    bool full = env->episode.full_reset && do_full_reset;
    if (full) {
        clear_visited(env);
        memset(env->maps_entered, 0, sizeof(env->maps_entered));
    }
    env->be->reset(env->impl, full);
    env_rebase(env, full);
    if (full) {
        PkRam ram = env_ram(env);
        pk_ram_prepare_start(&ram, &env->scenario, &env->rng);
        env->episode.blackouts = 0;
    }

    env_snapshot(env);
    env->prev = env->cur;
    env->dex_owned_best = env->cur.pokedex_owned_count;
    env->dex_seen_best = env->cur.pokedex_seen_count;
    pk_episode_begin(&env->episode, &env->cur);
    update_observations(env);

    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count = 0;
    env->battle_steps = env->ghost_battle_steps = env->last_reward_step = 0;
    env->score = 0.0f;
    env->progress_start = pk_progress(&env->cur, env->maps_entered);
    env->money_start = env->cur.money;
    env->coins_start = env->cur.coins;
    env->prev_action = -1;
    env->requested_action = -1;
    memset(env->hm_use_count, 0, sizeof(env->hm_use_count));
    env->key_item_mask = 0;
    env->key_progress = key_progress_mask(&env->cur);
    memset(&env->item_use, 0, sizeof(env->item_use));
    env->tm_pending = 0;
    env->tm_pending_steps = 0;
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
    float reward = env->agents[0].rewards[0];
    puf_reset_body(env, true);
    env->agents[0].rewards[0] = reward;
    env->agents[0].terminals[0] = 1;
}

static void handle_blackout(Env *env) {
    float w = env->weight[PK_SIG_death];
    env->agents[0].rewards[0] += w;
    env->score += w;
    env->totals.death += w;
    env->episode.blackouts++;
    if (env->blackout_clear_visited)
        clear_visited(env);
}

static void puf_step_body(Env *env) {
    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count++;
    RewardTotals totals_before = env->totals;

    int action = (int)env->agents[0].actions[0];
    env->requested_action = action;
    if (!env->start_select_enabled && (action == PKRED_ACTION_START || action == PKRED_ACTION_SELECT))
        action = -1;
    int hm = action - PKRED_ACTION_HM_CUT;
    bool hm_valid = hm >= 0 && hm < PK_NUM_HMS;
    if (hm_valid && !(env->hm_actions && hm_action_ready(&env->cur, hm)))
        action = -1, hm_valid = false;
    env->prev_action = action;
    if (hm_valid)
        hm_run(env, hm);
    else
        env_advance(env, action);
    env_snapshot(env);
    pk_episode_update_length(&env->episode, &env->cur);

    float reward = calculate_rewards(env);
    if (is_battle_active(&env->cur)) {
        env->battle_steps++;
        env->ghost_battle_steps += (env->cur.step_events & PK_EV_GHOST_BATTLE) != 0;
    }
    if (reward > 0.0f)
        env->last_reward_step = env->step_count;
    update_observations(env);
    update_action_mask(env);
    env->agents[0].rewards[0] = reward;
    env->score += reward;

    stream_collect(&env->stream, env->cur.x, env->cur.y, env->cur.map_n);
    if (env->stream.interval > 0 && env->step_count % env->stream.interval == 0) {
        env->stream.badges = __builtin_popcount(env->cur.badges);
        stream_flush(&env->stream);
    }

    if (env->cur.step_events & PK_EV_BLACKOUT)
        handle_blackout(env);
#define PK_SIG_STEP(name, fn, key, penalty, summed) env->step_totals.name = env->totals.name - totals_before.name;
    PK_REWARD_SIGNALS(PK_SIG_STEP)
#undef PK_SIG_STEP

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
        obs_view_tiles(env, obs);
        obs_view_visited(env, obs);
        UpdateTexture(env->render_texture, env->render_pixels);
    } else if (env->be->frame_rgba(env->impl, env->render_pixels)) {
        UpdateTexture(env->render_texture, env->render_pixels);
    }
    DrawTexturePro(env->render_texture, (Rectangle){0, 0, (float)PK_FRAME_W, (float)PK_FRAME_H}, dst,
        (Vector2){0, 0}, 0.0f, WHITE);
    if (obs_view)
        draw_tile_ids(env, dst);
}

#define PK_PANEL_H 296
#define PK_PANEL_INFO_H 240

static const char *const PK_ACTION_NAMES[PKRED_ACTION_COUNT] = {
    "A", "B", "RIGHT", "LEFT", "UP", "DOWN", "START", "SELECT", "CUT", "FLY", "SURF", "STR", "FLASH"};

static const char *status_label(uint8_t status) {
    if (status & 0x07) return "SLP";
    if (status & 0x08) return "PSN";
    if (status & 0x10) return "BRN";
    if (status & 0x20) return "FRZ";
    if (status & 0x40) return "PAR";
    return "";
}

static void draw_info_panel(Env *env, Rectangle box) {
    const PkSnapshot *s = &env->cur;
    const Color ink = RAYWHITE, dim = GRAY;
    DrawRectangleRec(box, (Color){24, 24, 32, 255});
    DrawLine((int)box.x, (int)box.y, (int)(box.x + box.width), (int)box.y, dim);

    int x = (int)box.x + 10, y = (int)box.y + 8;
    DrawText(TextFormat("PARTY %d/6", s->party_count), x, y, 16, dim);
    y += 22;
    for (int i = 0; i < PARTY_SIZE; i++, y += 32) {
        if (i >= s->party_count) {
            DrawText("-", x, y, 18, dim);
            continue;
        }
        const PkMon *m = &s->party[i];
        float frac = m->max_hp ? (float)m->hp / (float)m->max_hp : 0.0f;
        Color bar = frac > 0.5f ? GREEN : frac > 0.2f ? YELLOW : RED;
        DrawText(PKRED_SPECIES_NAMES[m->species], x, y, 18, ink);
        DrawText(TextFormat("L%d", m->level), x + 128, y, 18, ink);
        DrawRectangle(x + 172, y + 4, 80, 10, DARKGRAY);
        DrawRectangle(x + 172, y + 4, (int)(80 * frac), 10, bar);
        DrawText(TextFormat("%d/%d", m->hp, m->max_hp), x + 260, y, 16, ink);
        DrawText(status_label(m->status), x + 128, y + 16, 12, ORANGE);
    }

    x = (int)(box.x + box.width * 0.58f);
    y = (int)box.y + 8;
    float r = env->agents[0].rewards[0];
    Color rc = r > 0 ? GREEN : r < 0 ? RED : ink;
    DrawText(TextFormat("STEP %d", env->step_count), x, y, 16, dim);
    y += 22;
    DrawText(TextFormat("reward %+.4f", r), x, y, 18, rc);
    y += 22;
    DrawText(TextFormat("return %.3f", env->score), x, y, 16, ink);
    y += 24;
#define PK_SIG_DRAW(name, fn, key, penalty, summed)                                          \
    {                                                                                        \
        float v = env->step_totals.name;                                                     \
        DrawText(#name, x, y, 12, v != 0.0f ? ink : dim);                                    \
        DrawText(TextFormat("%+.4f", v), x + 80, y, 12, v > 0 ? GREEN : v < 0 ? RED : dim);  \
        DrawText(TextFormat("%.3f", env->totals.name), x + 160, y, 12, dim);                 \
        y += 14;                                                                             \
    }
    PK_REWARD_SIGNALS(PK_SIG_DRAW)
#undef PK_SIG_DRAW

    x = (int)box.x + 10;
    y = (int)box.y + PK_PANEL_INFO_H;
    int req = env->requested_action, done = env->prev_action;
    bool blocked = req >= 0 && req < PKRED_ACTION_COUNT && req != done;
    DrawText(TextFormat("ACTION %s", done >= 0 ? PK_ACTION_NAMES[done] : "NOOP"), x, y, 16, dim);
    if (blocked)
        DrawText(TextFormat("(%s blocked)", PK_ACTION_NAMES[req]), x + 140, y + 2, 12, RED);
    float cw = (box.width - 20) / PKRED_ACTION_COUNT;
    for (int a = 0; a < PKRED_ACTION_COUNT; a++) {
        Rectangle cell = {box.x + 10 + a * cw + 1, (float)y + 20, cw - 2, 26};
        bool avail = action_available(env, a);
        bool bad = blocked && a == req;
        DrawRectangleRec(cell, a == done ? GOLD : (Color){40, 40, 52, 255});
        DrawRectangleLinesEx(cell, bad ? 2 : 1, bad ? RED : dim);
        Color text = a == done ? BLACK : avail ? ink : (Color){80, 80, 90, 255};
        int w = MeasureText(PK_ACTION_NAMES[a], 10);
        DrawText(PK_ACTION_NAMES[a], (int)(cell.x + (cell.width - w) / 2), (int)cell.y + 8, 10, text);
    }
}

void puf_render(Env* env) {
    if (!IsWindowReady()) {
        render_init(env, SCREEN_WIDTH * 4, PK_FRAME_H * 4 + (env->render_panel ? PK_PANEL_H : 0), 0);
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
    } else if (IsKeyPressed(KEY_ONE)) {
        env->agents[0].actions[0] = PKRED_ACTION_HM_CUT;
    } else if (IsKeyPressed(KEY_TWO)) {
        env->agents[0].actions[0] = PKRED_ACTION_HM_FLY;
    } else if (IsKeyPressed(KEY_THREE)) {
        env->agents[0].actions[0] = PKRED_ACTION_HM_SURF;
    } else if (IsKeyPressed(KEY_FOUR)) {
        env->agents[0].actions[0] = PKRED_ACTION_HM_STRENGTH;
    } else if (IsKeyPressed(KEY_FIVE)) {
        env->agents[0].actions[0] = PKRED_ACTION_HM_FLASH;
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
    float game_h = (float)GetScreenHeight() - (env->render_panel ? PK_PANEL_H : 0);
    render_draw(env, (Rectangle){0, 0, (float)GetScreenWidth(), game_h});
    if (env->render_panel)
        draw_info_panel(env, (Rectangle){0, game_h, (float)GetScreenWidth(), PK_PANEL_H});
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
    free(env->prev_events);
    free(env->paid_events);
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
    dict_set(out, "money", log->money);
    dict_set(out, "money_gained", log->money_gained);
    dict_set(out, "coins", log->coins);
    dict_set(out, "coins_gained", log->coins_gained);
    dict_set(out, "badges", log->badges);
    dict_set(out, "event_sum", log->event_sum);
    dict_set(out, "progress", log->progress);
    dict_set(out, "progress_gained", log->progress_gained);

    dict_set(out, "pkmn1_lvl", log->pkmn_lvl[0]);
    dict_set(out, "pkmn2_lvl", log->pkmn_lvl[1]);
    dict_set(out, "pkmn3_lvl", log->pkmn_lvl[2]);
    dict_set(out, "pkmn4_lvl", log->pkmn_lvl[3]);
    dict_set(out, "pkmn5_lvl", log->pkmn_lvl[4]);
    dict_set(out, "pkmn6_lvl", log->pkmn_lvl[5]);

#define PK_SIG_LOG(name, fn, key, penalty, summed) dict_set(out, #name "_signal", log->reward.name);
    PK_REWARD_SIGNALS(PK_SIG_LOG)
#undef PK_SIG_LOG

    dict_set(out, "battle_steps", log->battle_steps);
    dict_set(out, "ghost_battle_steps", log->ghost_battle_steps);
    dict_set(out, "steps_since_reward", log->steps_since_reward);
    dict_set(out, "episode_extensions", log->episode_extensions);
    dict_set(out, "n", log->n);
}

#endif
