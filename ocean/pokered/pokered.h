#ifndef POKERED_H
#define POKERED_H

#include "raylib.h"
typedef float obs_t;
#include "pufferenv.h"

#include "gambatte/gambatte_wrapper.h"
#include "includes/ram_map.h"
#include "includes/battle.h"
#include "includes/events.h"
#include "pokered_stream.h"

#define SCREEN_WIDTH 160
#define SCALED_WIDTH 80
#define SCALED_HEIGHT 72
#define SCALED_PIXELS (SCALED_WIDTH * SCALED_HEIGHT)

#define PARTY_SIZE 6
#define PARTY_FIELDS 4
#define PARTY_OBS (PARTY_SIZE * PARTY_FIELDS)
#define GENERAL_SCALAR_OBS (1 + 4 + 1)
#define VISITED_WINDOW 15
#define VISITED_OBS (VISITED_WINDOW * VISITED_WINDOW)

#define TOTAL_OBSERVATIONS (SCALED_PIXELS + GENERAL_SCALAR_OBS + VISITED_OBS + PARTY_OBS)
#define VISITED_OBS_OFFSET (SCALED_PIXELS + GENERAL_SCALAR_OBS)
#define PARTY_OBS_OFFSET (VISITED_OBS_OFFSET + VISITED_OBS)
#define BLOCK_PIXELS 16
#define BLOCKS_WIDE (GB_SCREEN_WIDTH / BLOCK_PIXELS)
#define BLOCKS_TALL (GB_SCREEN_HEIGHT / BLOCK_PIXELS)
#define PLAYER_BLOCK_COL 4
#define PLAYER_BLOCK_ROW 4

#define GAME_MODE_GENERAL 0
#define GAME_MODE_BATTLE  1

#define MAX_MAPS 256
#define MAX_X 128
#define MAX_Y 128
#define VISITED_COORDS_SIZE (MAX_MAPS * MAX_X * MAX_Y)

#define POKEDEX_OWNED_SIZE ((PKRED_POKEDEX_NUM_POKEMON + 7) / 8)
#define POKEDEX_SEEN_SIZE  POKEDEX_OWNED_SIZE

#define VIRIDIAN_CITY_MAP 0x01

#define ACT_SIZES {8}   // A/B/SELECT/START/RIGHT/LEFT/UP/DOWN (GBAction)
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
    float pokedex_owned;
    float pokedex_seen;
    float explore_signal;
    float catching_signal;
    float seeing_signal;
    float events_signal;
    float leveling_signal;
    float healing_signal;
    float n;
};

typedef struct {
    uint32_t idx;
    uint8_t x;
    uint8_t y;
    uint8_t map_n;
    uint8_t badges;
    uint8_t party_count;
    uint8_t levels[6];
    uint8_t pokedex_owned_count;
    uint8_t pokedex_seen_count;
    float hp_fraction;
} CoreState;

typedef struct {
    CoreState core;
    BattleState battle;
    CoreState prev_core;
} GameState;

typedef struct {
    float total_explore_signal;
    float total_catching_signal;
    float total_seeing_signal;
    float total_events_signal;
    float total_leveling_signal;
    float total_healing_signal;
} EpisodeStats;

struct Env {
    Log log;
    Agent agents[1];
    int num_agents;
    int tag;
    int boundary_reached; 
    unsigned int rng;

    Emulator emu;
    GameState gstate;
    EpisodeStats stats;
    PokeredStream stream;

    uint8_t *visited_coords;
    uint8_t *prev_events;

    int32_t frame_count;
    int32_t step_count;
    int32_t max_episode_length;
    float prev_event_sum;
    uint32_t unique_coords_count;
    float score;
    int prev_action;
    uint8_t game_mode;
    bool party_wiped;

    bool full_reset;
    bool disable_wild_until_badge;
    bool verbose;

    float weight_exploration;
    float weight_catching;
    float weight_seeing;
    float weight_events;
    float weight_leveling;
    float weight_healing;
    float weight_time;   // per-step cost; 0 disables (default)

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
static inline bool is_directional_action(int action) {
    return action >= GB_ACTION_RIGHT && action <= GB_ACTION_DOWN;
}
static inline uint8_t detect_game_mode(Emulator *emu) {
    if (read_mem(emu, PKRED_ADDR_IS_IN_BATTLE) != 0)
        return GAME_MODE_BATTLE;
    return GAME_MODE_GENERAL;
}

#include "pokered_observations.h"
#include "pokered_rewards.h"

static void add_log(Env *env) {
    CoreState *core = &env->gstate.core;

    env->log.episode_length = env->step_count;
    env->log.episode_return = env->score;
    env->log.money = read_bcd(&env->emu, PKRED_ADDR_PLAYER_MONEY);

    env->log.level_sum = calc_level_sum(core);
    for (int i = 0; i < 6; i++)
        (&env->log.pkmn1_lvl)[i] = core->levels[i];
    env->log.party_count = core->party_count;

    env->log.badges = core->badges;
    env->log.event_sum = env->prev_event_sum;
    env->log.unique_coords = env->unique_coords_count;
    env->log.pokedex_owned = core->pokedex_owned_count;
    env->log.pokedex_seen = core->pokedex_seen_count;

    env->log.explore_signal = env->stats.total_explore_signal;
    env->log.catching_signal = env->stats.total_catching_signal;
    env->log.seeing_signal = env->stats.total_seeing_signal;
    env->log.events_signal = env->stats.total_events_signal;
    env->log.leveling_signal = env->stats.total_leveling_signal;
    env->log.healing_signal = env->stats.total_healing_signal;
    env->log.n++;
}

void puf_init(Env* env, Dict* kwargs) {
    unsigned int env_id = env->rng;  // trainer sets this before calling puf_init

    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->agents[0].action_mask = NULL;

    env->emu.frame_skip = (int32_t)dict_get(kwargs, "frameskip");
    env->emu.press_frames = (int32_t)dict_get(kwargs, "press_frames");
    env->max_episode_length = (int32_t)dict_get(kwargs, "max_episode_length");
    env->emu.render_enabled = dict_get(kwargs, "headless") == 0.0;
    env->full_reset = dict_get(kwargs, "full_reset") != 0.0;
    env->disable_wild_until_badge = dict_get(kwargs, "disable_wild_until_badge") != 0.0;
    env->verbose = dict_get(kwargs, "verbose") != 0.0;

    env->weight_exploration = (float)dict_get(kwargs, "weight_exploration");
    env->weight_catching = (float)dict_get(kwargs, "weight_catching");
    env->weight_seeing = (float)dict_get(kwargs, "weight_seeing");
    env->weight_events = (float)dict_get(kwargs, "weight_events");
    env->weight_leveling = (float)dict_get(kwargs, "weight_leveling");
    env->weight_healing = (float)dict_get(kwargs, "weight_healing");
    env->weight_time = (float)dict_get(kwargs, "weight_time");

    DictItem* sp = dict_find(kwargs, "state_path");
    if (sp && sp->str && sp->str[0]) {
        strncpy(env->emu.state_path, sp->str, sizeof(env->emu.state_path) - 1);
    }

    const char* rom_path = dict_get_str(kwargs, "rom_path");
    strncpy(env->emu.rom_path, rom_path, sizeof(env->emu.rom_path) - 1);
    DictItem* nt = dict_find(kwargs, "vec_num_threads");
    int pool_size = nt ? (int)nt->value : 1;
    gb_pool_init(pool_size > 0 ? pool_size : 1, rom_path);
    env->emu.pool_state_buf = (uint8_t*)malloc(g_gb_pool.state_size);
    env->emu.pool_initial_state_buf = (uint8_t*)malloc(g_gb_pool.state_size);
    env->emu.video_buffer = (color_t*)calloc(GB_VIDEO_PITCH * GB_SCREEN_HEIGHT, sizeof(color_t));
    env->emu.gb = NULL;
    gb_pool_load_initial_state(&env->emu, env->emu.state_path);

    env->visited_coords = (uint8_t*)calloc(VISITED_COORDS_SIZE, sizeof(uint8_t));
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

static void puf_reset_body(Env* env) {
    if (env->full_reset) {
        gambatte_load_state_raw(env->emu.gb, env->emu.pool_initial_state_buf);
    }
    update_core_state(env);
    env->gstate.prev_core = env->gstate.core;
    update_battle_state(&env->gstate.battle, &env->emu);
    update_observations(env);

    env->unique_coords_count = 0;
    memset(env->visited_coords, 0, VISITED_COORDS_SIZE * sizeof(*env->visited_coords));

    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count = env->frame_count = 0;
    env->score = 0.0f;
    env->prev_action = -1;
    env->prev_event_sum = calc_event_weighted_sum(&env->emu, NULL, false);
    env->game_mode = detect_game_mode(&env->emu);
    memset(&env->stats, 0, sizeof(EpisodeStats));
    for (size_t i = 0; i < EVENT_COUNT; ++i) {
        uint8_t value = read_mem(&env->emu, EVENT_LIST[i].address);
        env->prev_events[i] = (value >> EVENT_LIST[i].bit) & 1;
    }

    for (int i = 0; i < 4; i++)
        gambatte_run_frame(env->emu.gb, env->emu.video_buffer);
}
void puf_reset(Env* env) {
    gb_pool_acquire_for(&env->emu);
    puf_reset_body(env);
    gb_pool_release_for(&env->emu);
}

static void puf_step_body(Env* env) {
    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    env->step_count++;

    if (env->disable_wild_until_badge) {
        uint8_t flags = read_mem(&env->emu, PKRED_ADDR_WD72E);
        if (env->gstate.core.badges == 0) {
            write_mem(&env->emu, PKRED_ADDR_WD72E, flags | (1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
        } else {
            write_mem(&env->emu, PKRED_ADDR_WD72E, flags & ~(1 << PKRED_WD72E_DISABLE_BATTLES_BIT));
        }
    }

    // Viridian City's "old man" NPC runs a scripted forced-battle cutscene
    if (env->gstate.core.map_n == VIRIDIAN_CITY_MAP &&
        read_mem(&env->emu, PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT) == 1) {
        write_mem(&env->emu, PKRED_ADDR_VIRIDIAN_CITY_CUR_SCRIPT, 0);
        write_mem(&env->emu, PKRED_ADDR_BATTLE_TYPE, 0);
    }

    int skip = env->emu.frame_skip > 0 ? env->emu.frame_skip : 24;
    int press = env->emu.press_frames > 0 ? env->emu.press_frames : 8;
    env->prev_action = (int)env->agents[0].actions[0];
    uint32_t action_key = action_to_key(env->prev_action);
    STEP_ACTION_FRAMES(env->emu.gb, action_key, env->emu.video_buffer, press, skip);
    env->frame_count += skip;

    float reward = calculate_rewards(env);
    env->game_mode = detect_game_mode(&env->emu);
    update_observations(env);
    env->agents[0].rewards[0] = reward;
    env->score += reward;

    stream_collect(&env->stream, env->gstate.core.x, env->gstate.core.y, env->gstate.core.map_n);
    if (env->stream.interval > 0 && env->step_count % env->stream.interval == 0) {
        stream_flush(&env->stream);
    }


    bool party_alive = env->gstate.core.party_count == 0 ||
        party_hp_fraction(&env->emu) > 0.0f;
    if (party_alive) {
        env->party_wiped = false;
    } else if (!env->party_wiped) {
        env->party_wiped = true;
        env->agents[0].terminals[0] = 1;
        add_log(env);
        puf_reset_body(env);
    }

}
void puf_step(Env* env) {
    gb_pool_acquire_for(&env->emu);
    puf_step_body(env);
    gb_pool_release_for(&env->emu);
}

void puf_render(Env* env) {
    if (!IsWindowReady()) {
        InitWindow(SCREEN_WIDTH * 4, GB_SCREEN_HEIGHT * 4, "PufferLib Pokemon Red");
        SetTargetFPS(60);
        Image img = GenImageColor(GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT, BLACK);
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        env->render_texture = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }

    if (IsKeyDown(KEY_Z)) {
        env->agents[0].actions[0] = GB_ACTION_A;
    } else if (IsKeyDown(KEY_X)) {
        env->agents[0].actions[0] = GB_ACTION_B;
    } else if (IsKeyDown(KEY_ENTER)) {
        env->agents[0].actions[0] = GB_ACTION_START;
    } else if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
        env->agents[0].actions[0] = GB_ACTION_SELECT;
    } else if (IsKeyDown(KEY_RIGHT)) {
        env->agents[0].actions[0] = GB_ACTION_RIGHT;
    } else if (IsKeyDown(KEY_LEFT)) {
        env->agents[0].actions[0] = GB_ACTION_LEFT;
    } else if (IsKeyDown(KEY_UP)) {
        env->agents[0].actions[0] = GB_ACTION_UP;
    } else if (IsKeyDown(KEY_DOWN)) {
        env->agents[0].actions[0] = GB_ACTION_DOWN;
    } else {
        env->agents[0].actions[0] = -1;
    }

    if (IsKeyPressed(KEY_S)) {
        const char* save_path = "ocean/pokered/states/quicksave.state";
        if (c_save_state_file(&env->emu, save_path)) {
            printf("pokered: state saved to %s\n", save_path);
        } else {
            fprintf(stderr, "pokered: failed to save state to %s\n", save_path);
        }
    }

    if (IsKeyPressed(KEY_O)) {
        env->show_obs_view = !env->show_obs_view;
    }

    if (!env->render_pixels) {
        env->render_pixels = (uint8_t*)calloc(GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT * 4, 1);
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
                        uint8_t* out = &env->render_pixels[(y * GB_SCREEN_WIDTH + x) * 4];
                        out[0] = gray;  // R
                        out[1] = gray;  // G
                        out[2] = gray;  // B
                        out[3] = 255;   // A
                    }
                }
            }
        }

        int half = VISITED_WINDOW / 2;
        for (int by = 0; by < BLOCKS_TALL; by++) {
            int wy = half + by - PLAYER_BLOCK_ROW;
            for (int bx = 0; bx < BLOCKS_WIDE; bx++) {
                bool is_player = bx == PLAYER_BLOCK_COL && by == PLAYER_BLOCK_ROW;
                int wx = half + bx - PLAYER_BLOCK_COL;
                bool visited = !is_player && wx >= 0 && wx < VISITED_WINDOW &&
                    wy >= 0 && wy < VISITED_WINDOW &&
                    obs[VISITED_OBS_OFFSET + wy * VISITED_WINDOW + wx] != 0.0f;
                if (!visited && !is_player)
                    continue;
                uint8_t tint_r = is_player ? 220 : 0;
                uint8_t tint_g = is_player ? 40 : 200;
                for (int py = 0; py < BLOCK_PIXELS; py++) {
                    for (int px = 0; px < BLOCK_PIXELS; px++) {
                        int x = bx * BLOCK_PIXELS + px;
                        int y = by * BLOCK_PIXELS + py;
                        uint8_t* out = &env->render_pixels[(y * GB_SCREEN_WIDTH + x) * 4];
                        out[0] = (uint8_t)((out[0] + tint_r) / 2);
                        out[1] = (uint8_t)((out[1] + tint_g) / 2);
                        out[2] = (uint8_t)(out[2] / 2);
                    }
                }
            }
        }
        UpdateTexture(env->render_texture, env->render_pixels);
    } else if (env->emu.video_buffer) {
        for (int y = 0; y < GB_SCREEN_HEIGHT; y++) {
            const color_t* row = &env->emu.video_buffer[y * GB_VIDEO_PITCH];
            uint8_t* out = &env->render_pixels[y * GB_SCREEN_WIDTH * 4];
            for (int x = 0; x < GB_SCREEN_WIDTH; x++) {
                color_t px = row[x];
                out[x * 4 + 0] = (px >> 16) & 0xFF;  // R
                out[x * 4 + 1] = (px >> 8) & 0xFF;   // G
                out[x * 4 + 2] = px & 0xFF;          // B
                out[x * 4 + 3] = 255;                // A
            }
        }
        UpdateTexture(env->render_texture, env->render_pixels);
    }

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(env->render_texture,
        (Rectangle){0, 0, (float)GB_SCREEN_WIDTH, (float)GB_SCREEN_HEIGHT},
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
    if (env->emu.gb) {
        gambatte_destroy(env->emu.gb);
        env->emu.gb = NULL;
    }
    if (env->emu.uses_shared_rom) {
        release_shared_rom();
        env->emu.uses_shared_rom = false;
    }
    if (env->emu.video_buffer) {
        free(env->emu.video_buffer);
        env->emu.video_buffer = NULL;
    }

    if (env->emu.pool_state_buf) {
        free(env->emu.pool_state_buf);
        env->emu.pool_state_buf = NULL;
    }
    if (env->emu.pool_initial_state_buf) {
        free(env->emu.pool_initial_state_buf);
        env->emu.pool_initial_state_buf = NULL;
    }
    stream_close(&env->stream);
    free(env->visited_coords);
    free(env->prev_events);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "level_sum", log->level_sum);
    dict_set(out, "unique_coords", log->unique_coords);
    // Pokemon levels and signals
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

    dict_set(out, "money", log->money);
    dict_set(out, "party_count", log->party_count);
    dict_set(out, "badges", log->badges);
    dict_set(out, "event_sum", log->event_sum);
    dict_set(out, "pokedex_owned", log->pokedex_owned);
    dict_set(out, "pokedex_seen", log->pokedex_seen);

    dict_set(out, "n", log->n);
}

#endif // POKERED_H
