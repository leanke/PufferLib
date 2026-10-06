#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "../includes/ram_map.h"
#include "../pokered_layout.h"
#include "../pokered_backend.h"
#include "../pkstate.h"
#include "step_events.h"

extern "C" {
#include "gamestate.h"
#include "constants.h"
#include "events.h"
#include "items.h"
#include "map_objects.h"
#include "moves.h"
#include "overworld.h"
#include "pokemon_data.h"
#include "stats.h"
#include "tilesets.h"
#include "type_chart.h"
}
#include "redcore_events.h"
#include "redcore_start.h"
#include "redcore_battle_text.h"

#define REDCORE_POKEDEX_SLOTS 256
#define REDCORE_DEX_BYTES (REDCORE_POKEDEX_SLOTS / 8)

typedef enum RedcoreBattleMenu {
    RC_MENU_MAIN = 0,
    RC_MENU_FIGHT,
    RC_MENU_ITEM,
    RC_MENU_PARTY,
} RedcoreBattleMenu;

struct RcOptions {
    char assets_dir[256];
    int fixed_starter_species;
    bool nickname_prompt_enabled, npc_text_enabled, npc_movement_enabled;
    bool real_battle_ui_enabled, battle_text_enabled;
};

struct RcEnv {
    GameState gstate;
    PkBackendConfig cfg;
    RcOptions opt;
    PkEventTracker events;
    uint8_t fixed_starter_species;
    RcStart start;
    uint8_t dex_owned[REDCORE_DEX_BYTES];
    uint8_t dex_seen[REDCORE_DEX_BYTES];
    int *event_bit;

    uint8_t battle_menu;
    uint8_t cursor_main;
    uint8_t cursor_fight;
    uint8_t cursor_item;
    uint8_t cursor_party;
    uint8_t last_move_slot;
    uint8_t cursor_party_saved;
    uint8_t party_stage;
    uint8_t party_msg;
    uint8_t bag_scroll;
    uint8_t item_stage;
    uint8_t item_pending;
    uint8_t item_msg;
    uint8_t cursor_action;
    uint8_t player_selected_move;
    GameMode prev_mode;
    uint8_t text_return_menu;
    uint8_t struggle_turn;
    RcTextPhase text;

    uint8_t frame[160 * 144];
};

static inline bool rc_in_battle(const GameState *gs) {
    return gs->mode == GAME_MODE_BATTLE || gs->mode == GAME_MODE_BATTLE_SWITCH ||
           gs->mode == GAME_MODE_SAFARI_BATTLE;
}

static inline bool rc_is_directional(int action) {
    return action >= PKRED_ACTION_RIGHT && action <= PKRED_ACTION_DOWN;
}

static PartyMon rc_party_mon(const GameState *gs, int i) {
    PartyMon m = gs->party[i];
    if ((gs->mode == GAME_MODE_BATTLE || gs->mode == GAME_MODE_BATTLE_SWITCH) && i == gs->active_party_slot) {
        const BattleMon *b = &gs->battle.player;
        m.box.hp = (uint16_t)(b->hp < 0 ? 0 : b->hp);
    }
    return m;
}

static float rc_party_hp_fraction(const GameState *gs) {
    if (gs->party_count == 0 || gs->party_count > 6) return 1.0f;
    uint32_t hp = 0, max_hp = 0;
    for (int i = 0; i < gs->party_count; i++) {
        PartyMon m = rc_party_mon(gs, i);
        hp += m.box.hp;
        max_hp += m.max_hp;
    }
    return max_hp > 0 ? (float)hp / (float)max_hp : 1.0f;
}

static int rc_dex_count(const uint8_t *dex) {
    int n = 0;
    for (int i = 0; i < REDCORE_DEX_BYTES; i++) n += __builtin_popcount(dex[i]);
    return n;
}

static inline void rc_dex_mark(uint8_t *dex, uint8_t species) {
    if (species != 0) dex[species / 8] |= (uint8_t)(1 << (species % 8));
}

static void rc_update_dex(RcEnv *env) {
    const GameState *gs = &env->gstate;
    for (int i = 0; i < gs->party_count && i < REDCORE_MAX_PARTY; i++) {
        rc_dex_mark(env->dex_owned, gs->party[i].box.species);
        rc_dex_mark(env->dex_seen, gs->party[i].box.species);
    }
    for (int bank = 0; bank < REDCORE_NUM_BOX_BANKS; bank++)
        for (int box = 0; box < REDCORE_BOXES_PER_BANK; box++)
            for (int slot = 0; slot < REDCORE_BOX_CAPACITY; slot++) {
                uint8_t sp = gs->box_species[bank][box][slot];
                if (sp != 0) {
                    rc_dex_mark(env->dex_owned, sp);
                    rc_dex_mark(env->dex_seen, sp);
                }
            }
    if (rc_in_battle(gs)) rc_dex_mark(env->dex_seen, gs->battle.enemy.species);
}

#include "redcore_menu.h"
#include "redcore_item_names.h"
#include "redcore_screen.h"

static void rc_start_mon(PartyMon *m, const RcStartMon *c, bool is_starter, uint8_t starter_species,
                         const char *ot_name) {
    memset(m, 0, sizeof(*m));
    bool swap = is_starter && starter_species != c->species;
    uint8_t species = swap ? starter_species : c->species;
    const PokemonBaseStats *base = pokemon_base_stats(species);
    m->box.species = species;
    m->box.type1 = base->type1;
    m->box.type2 = base->type2;
    m->box.catch_rate = base->catch_rate;
    m->box.status = c->status;
    m->level = c->level;
    m->box.box_level = c->level;
    m->box.exp = c->exp;
    m->box.ot_id = c->ot_id;
    m->box.hp_exp = c->stat_exp[0];
    m->box.attack_exp = c->stat_exp[1];
    m->box.defense_exp = c->stat_exp[2];
    m->box.speed_exp = c->stat_exp[3];
    m->box.special_exp = c->stat_exp[4];
    m->box.dv_attack = c->dv_hi >> 4;
    m->box.dv_defense = c->dv_hi & 15;
    m->box.dv_speed = c->dv_lo >> 4;
    m->box.dv_special = c->dv_lo & 15;
    strncpy(m->ot_name, ot_name, REDCORE_NAME_LENGTH - 1);
    if (swap) {
        memcpy(m->box.moves, base->learnset, sizeof(m->box.moves));
        for (int i = 0; i < REDCORE_NUM_MOVES; i++)
            m->box.pp[i] = m->box.moves[i] != 0 ? MOVES[m->box.moves[i]].pp : 0;
        redcore_calc_all_stats(m, m->level);
        m->box.hp = m->max_hp;
        return;
    }
    memcpy(m->box.moves, c->moves, sizeof(m->box.moves));
    memcpy(m->box.pp, c->pp, sizeof(m->box.pp));
    m->box.hp = c->hp;
    m->max_hp = c->max_hp;
    m->attack = c->attack;
    m->defense = c->defense;
    m->speed = c->speed;
    m->special = c->special;
}

static void rc_set_event_by_name(GameState *gs, const char *name) {
    for (size_t j = 0; j < sizeof(REDCORE_EVENT_LIST) / sizeof(REDCORE_EVENT_LIST[0]); j++)
        if (strcmp(REDCORE_EVENT_LIST[j].name, name) == 0) {
            event_flag_set(&gs->flags, REDCORE_EVENT_LIST[j].bit);
            return;
        }
    fprintf(stderr, "redcore: start event '%s' has no redcore flag\n", name);
}

static void rc_seed_dex(RcEnv *env, const RcStart *st) {
    for (int id = 1; id < 191; id++) {
        int dex = SPECIES_ID_TO_DEX_NUMBER[id];
        if (dex < 1 || dex > 151) continue;
        int bit = dex - 1;
        if ((st->dex_owned[bit / 8] >> (bit % 8)) & 1) rc_dex_mark(env->dex_owned, (uint8_t)id);
        if ((st->dex_seen[bit / 8] >> (bit % 8)) & 1) rc_dex_mark(env->dex_seen, (uint8_t)id);
    }
}

static void redcore_reset_to_fixed_start(RcEnv *env, unsigned *rng) {
    GameState *gs = &env->gstate;
    const RcStart *st = &env->start;

    *rng = *rng * 1664525u + 1013904223u;
    gamestate_init(gs, *rng);
    gs->nickname_prompt_enabled = env->opt.nickname_prompt_enabled;
    gs->npc_text_enabled = env->opt.npc_text_enabled;
    gs->npc_movement_enabled = env->opt.npc_movement_enabled;
    gs->npc_frames_per_step = env->cfg.frameskip > 0 ? (uint8_t)env->cfg.frameskip : REDCORE_NPC_FRAMES_PER_STEP;

    strncpy(gs->player.name, st->player_name, REDCORE_NAME_LENGTH - 1);
    strncpy(gs->player.rival_name, st->rival_name, REDCORE_NAME_LENGTH - 1);
    gs->player.money = st->money;
    gs->player.badges = st->badges;

    for (int i = 0; i < st->party_count && i < REDCORE_MAX_PARTY; i++)
        rc_start_mon(&gs->party[i], &st->party[i], i == 0, env->fixed_starter_species, gs->player.name);
    gs->party_count = st->party_count;
    gs->active_party_slot = 0;

    for (int i = 0; i < st->bag_count; i++) bag_add_item(&gs->bag, st->bag[i][0], st->bag[i][1]);

    for (int i = 0; i < st->event_count; i++) rc_set_event_by_name(gs, st->events[i]);
    if (env->cfg.route22_rival_beaten) event_flag_set(&gs->flags, EVENT_BEAT_ROUTE22_RIVAL_1ST_BATTLE);
    if (env->cfg.route22_rival_2nd_beaten) event_flag_set(&gs->flags, EVENT_BEAT_ROUTE22_RIVAL_2ND_BATTLE);

    rc_seed_dex(env, st);

    memcpy(gs->toggle_hidden, st->toggles, sizeof(gs->toggle_hidden));
    if (env->cfg.route22_rival_beaten) overworld_hide_object(gs, TOGGLE_ROUTE_22_RIVAL_1);
    if (env->cfg.route22_rival_2nd_beaten) overworld_hide_object(gs, TOGGLE_ROUTE_22_RIVAL_2);

    gs->player.map_id = st->map;
    gs->player.x = st->x;
    gs->player.y = st->y;
    switch (st->facing_byte) {
        case 0: gs->player.direction = DIR_SOUTH; break;
        case 4: gs->player.direction = DIR_NORTH; break;
        case 8: gs->player.direction = DIR_WEST; break;
        default: gs->player.direction = DIR_EAST; break;
    }
    gs->mode = GAME_MODE_OVERWORLD;
}

static uint8_t rc_facing(uint8_t dir) {
    switch (dir) {
        case DIR_SOUTH: return 0;
        case DIR_NORTH: return 1;
        case DIR_WEST: return 2;
        default: return 3;
    }
}

static void fill_party_mon(PkMon *o, const PartyMon *m) {
    o->species = m->box.species;
    o->level = m->level;
    o->hp = m->box.hp;
    o->max_hp = m->max_hp;
    for (int k = 0; k < 4; k++)
        o->moves[k] = m->box.moves[k];
}

static void fill_battle_mon(PkMon *o, const BattleMon *b) {
    memset(o, 0, sizeof(*o));
    o->species = b->species;
    o->level = b->level;
    o->hp = (uint16_t)(b->hp < 0 ? 0 : b->hp);
    o->max_hp = (uint16_t)b->max_hp;
}

namespace {

void *rc_create(const PkBackendConfig *cfg, const PkOptions *opts) {
    RcEnv *env = (RcEnv *)calloc(1, sizeof(RcEnv));
    env->cfg = *cfg;
    snprintf(env->opt.assets_dir, sizeof(env->opt.assets_dir), "%s", pk_opt_str(opts, "assets_dir", "vendor/redcore/assets"));
    env->opt.fixed_starter_species = pk_opt_int(opts, "fixed_starter_species", 0);
    env->opt.nickname_prompt_enabled = pk_opt_bool(opts, "nickname_prompt_enabled", false);
    env->opt.npc_text_enabled = pk_opt_bool(opts, "npc_text_enabled", false);
    env->opt.npc_movement_enabled = pk_opt_bool(opts, "npc_movement_enabled", false);
    env->opt.real_battle_ui_enabled = pk_opt_bool(opts, "real_battle_ui_enabled", false);
    env->opt.battle_text_enabled = pk_opt_bool(opts, "battle_text_enabled", false);

    PkState state;
    char err[512];
    if (!pk_state_resolve(cfg->state_path, cfg->rom_path, cfg->pkstate_cache_enabled, &state, err, sizeof(err))) {
        fprintf(stderr, "pokered: redcore backend: %s\n", err);
        exit(1);
    }
    rc_start_decode(&state, &env->start);

    env->fixed_starter_species = (uint8_t)env->opt.fixed_starter_species;
    if (env->fixed_starter_species != BULBASAUR && env->fixed_starter_species != CHARMANDER &&
        env->fixed_starter_species != SQUIRTLE) {
        env->fixed_starter_species = SQUIRTLE;
    }

    int n = pk_event_count();
    env->event_bit = (int *)malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) {
        env->event_bit[i] = -1;
        for (size_t j = 0; j < sizeof(REDCORE_EVENT_LIST) / sizeof(REDCORE_EVENT_LIST[0]); j++)
            if (strcmp(REDCORE_EVENT_LIST[j].name, pk_event_name(i)) == 0) {
                env->event_bit[i] = REDCORE_EVENT_LIST[j].bit;
                break;
            }
    }

    if (cfg->screen_obs_enabled) redcore_load_atlases(env->opt.assets_dir);

    unsigned rng = cfg->env_id;
    redcore_reset_to_fixed_start(env, &rng);
    return env;
}

void rc_destroy(void *impl) {
    RcEnv *env = (RcEnv *)impl;
    free(env->event_bit);
    free(env);
}

void rc_noop(void *) {}

void rc_reset(void *impl, bool full_reset, unsigned *rng, bool *from_milestone) {
    RcEnv *env = (RcEnv *)impl;
    *from_milestone = false;
    pk_events_rebase(&env->events);
    if (full_reset) {
        memset(env->dex_owned, 0, sizeof(env->dex_owned));
        memset(env->dex_seen, 0, sizeof(env->dex_seen));
        redcore_reset_to_fixed_start(env, rng);
    }
    env->battle_menu = RC_MENU_MAIN;
    env->cursor_main = env->cursor_fight = env->cursor_item = env->cursor_party = 0;
    env->last_move_slot = env->cursor_party_saved = env->party_stage = env->cursor_action = env->party_msg = 0;
    env->bag_scroll = env->item_stage = env->item_pending = env->item_msg = 0;
    env->player_selected_move = 0;
    env->text.active = false;
    env->text_return_menu = RC_MENU_MAIN;
    env->struggle_turn = 0;
    env->prev_mode = env->gstate.mode;
}

void rc_cancel_wild_battle(RcEnv *env) {
    GameState *gs = &env->gstate;
    if (env->cfg.disable_wild_until_badge && gs->player.badges == 0 && gs->mode == GAME_MODE_BATTLE &&
        env->prev_mode != GAME_MODE_BATTLE && !gs->battle.is_trainer_battle &&
        gs->pending_story_battle == STORY_BATTLE_NONE) {
        gs->mode = GAME_MODE_OVERWORLD;
    }
}

void rc_step(void *impl, int action, const PkSnapshot *) {
    RcEnv *env = (RcEnv *)impl;
    env->prev_mode = env->gstate.mode;
    pk_events_stepped(&env->events);
    if (env->text.active) {
        rc_text_input(env, action);
        return;
    }
    redcore_apply_button(env, action);
    rc_cancel_wild_battle(env);
    redcore_menu_after_step(env);
}

void rc_snapshot_now(RcEnv *env, PkSnapshot *s) {
    const GameState *gs = &env->gstate;
    memset(s, 0, sizeof(*s));

    rc_update_dex(env);
    s->x = (uint8_t)gs->player.x;
    s->y = (uint8_t)gs->player.y;
    s->map_n = gs->player.map_id;
    s->facing = rc_facing(gs->player.direction);
    s->badges = gs->player.badges;
    s->party_count = gs->party_count;
    for (int i = 0; i < gs->party_count && i < 6; i++) {
        PartyMon m = rc_party_mon(gs, i);
        fill_party_mon(&s->party[i], &m);
    }
    s->pokedex_owned_count = (uint8_t)rc_dex_count(env->dex_owned);
    s->pokedex_seen_count = (uint8_t)rc_dex_count(env->dex_seen);
    s->hp_fraction = rc_party_hp_fraction(gs);
    s->blackouts = gs->blackouts;
    s->battles_won = gs->battles_won;
    s->battles_fled = gs->battles_fled;

    if (rc_in_battle(gs)) {

        s->in_battle = gs->battle.is_trainer_battle ? 2 : 1;

        bool has_player_mon = gs->mode == GAME_MODE_BATTLE || gs->mode == GAME_MODE_BATTLE_SWITCH;
        if (has_player_mon) fill_battle_mon(&s->battle_mon, &gs->battle.player);
        fill_battle_mon(&s->enemy_mon, &gs->battle.enemy);
    }

    s->bag_count = gs->bag.num_slots > 20 ? 20 : gs->bag.num_slots;
    for (int i = 0; i < s->bag_count; i++) {
        s->bag[i].item = gs->bag.slots[i].item_id;
        s->bag[i].count = gs->bag.slots[i].count;
    }

    for (int i = 0; i < pk_event_count(); i++)
        s->events[i] = env->event_bit[i] >= 0 && event_flag_get(&gs->flags, env->event_bit[i]) ? 1 : 0;
}

void rc_snapshot_state(RcEnv *env, PkSnapshot *s) {
    if (!env->text.active) {
        rc_snapshot_now(env, s);
        return;
    }
    const RcTextPhase *ph = &env->text;
    const RcTextFrame *tf = &ph->f[ph->i < ph->n ? ph->i : (ph->n ? ph->n - 1 : 0)];
    *s = ph->held;
    s->in_battle = ph->held.in_battle ? ph->held.in_battle : (env->gstate.battle.is_trainer_battle ? 2 : 1);
    if (tf->me.max_hp) {
        s->battle_mon.species = tf->me.species;
        s->battle_mon.level = tf->me.level;
        s->battle_mon.hp = (uint16_t)tf->me.hp;
        s->battle_mon.max_hp = tf->me.max_hp;
    }
    if (tf->foe.max_hp) {
        s->enemy_mon.species = tf->foe.species;
        s->enemy_mon.level = tf->foe.level;
        s->enemy_mon.hp = (uint16_t)tf->foe.hp;
        s->enemy_mon.max_hp = tf->foe.max_hp;
    }
}

void rc_snapshot(void *impl, PkSnapshot *s) {
    RcEnv *env = (RcEnv *)impl;
    rc_snapshot_state(env, s);
    pk_events_apply(&env->events, s);
}

void rc_screen(void *impl, float *obs) {
    RcEnv *env = (RcEnv *)impl;

    redcore_render_frame(env, env->frame);
    for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
        const uint8_t *r0 = &env->frame[(sy * 2) * RC_FRAME_W];
        const uint8_t *r1 = r0 + RC_FRAME_W;
        for (int sx = 0; sx < SCALED_WIDTH; sx++)
            obs[sy * SCALED_WIDTH + sx] =
                (float)((r0[sx * 2] + r0[sx * 2 + 1] + r1[sx * 2] + r1[sx * 2 + 1]) >> 2);
    }
}

bool rc_frame_rgba(void *impl, uint8_t *rgba) {
    RcEnv *env = (RcEnv *)impl;
    if (env->cfg.screen_obs_enabled) redcore_render_frame(env, env->frame);
    else memset(env->frame, 0, sizeof(env->frame));
    for (int i = 0; i < RC_FRAME_PIXELS; i++) {
        rgba[i * 4 + 0] = env->frame[i];
        rgba[i * 4 + 1] = env->frame[i];
        rgba[i * 4 + 2] = env->frame[i];
        rgba[i * 4 + 3] = 255;
    }
    return true;
}

void rc_blackout(void *impl) {
    RcEnv *env = (RcEnv *)impl;
    gamestate_check_and_handle_blackout(&env->gstate);
    env->battle_menu = RC_MENU_MAIN;
    pk_events_rebase(&env->events);
}

const PkBackend REDCORE_BACKEND = {
    "redcore",
    PK_CAP_FRAME_RGBA | PK_CAP_BLACKOUT,
    PK_BTN_A | PK_BTN_B | PK_BTN_RIGHT | PK_BTN_LEFT | PK_BTN_UP | PK_BTN_DOWN,
    rc_create, rc_destroy, rc_noop, rc_noop, rc_reset, rc_noop, rc_step, rc_snapshot, rc_screen,
    rc_blackout,   NULL,   NULL,   NULL, rc_frame_rgba,
      NULL,   NULL,
};

}

#undef A
#undef C
#undef D
#undef F
#undef G

PK_REGISTER_BACKEND(REDCORE_BACKEND)
