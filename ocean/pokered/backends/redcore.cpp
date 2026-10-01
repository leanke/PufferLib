#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "../includes/ram_map.h"
#include "../pokered_layout.h"
#include "../pokered_backend.h"

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
}
#include "redcore_events.h"

#define REDCORE_POKEDEX_SLOTS 256
#define REDCORE_DEX_BYTES (REDCORE_POKEDEX_SLOTS / 8)

typedef enum RedcoreBattleMenu {
    RC_MENU_MAIN = 0,
    RC_MENU_FIGHT,
    RC_MENU_ITEM,
    RC_MENU_PARTY,
} RedcoreBattleMenu;

struct RcEnv {
    GameState gstate;
    PkBackendConfig cfg;
    uint8_t fixed_starter_species;
    uint8_t dex_owned[REDCORE_DEX_BYTES];
    uint8_t dex_seen[REDCORE_DEX_BYTES];
    int *event_bit;

    uint8_t battle_menu;
    uint8_t cursor_main;
    uint8_t cursor_fight;
    uint8_t cursor_item;
    uint8_t cursor_party;
    uint8_t player_selected_move;
    GameMode prev_mode;

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
        m.box.status = b->status;
        for (int k = 0; k < REDCORE_NUM_MOVES; k++) m.box.pp[k] = b->pp[k];
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

static int rc_party_fainted_count(const GameState *gs) {
    int fainted = 0;
    for (int i = 0; i < gs->party_count && i < 6; i++)
        if (rc_party_mon(gs, i).box.hp == 0) fainted++;
    return fainted;
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
#include "redcore_screen.h"

static void redcore_reset_to_fixed_start(RcEnv *env, unsigned *rng) {
    GameState *gs = &env->gstate;

    *rng = *rng * 1664525u + 1013904223u;
    gamestate_init(gs, *rng);
    gs->nickname_prompt_enabled = env->cfg.nickname_prompt_enabled;

    PartyMon starter;
    memset(&starter, 0, sizeof(starter));
    starter.box.species = env->fixed_starter_species;
    starter.level = 5;
    const PokemonBaseStats *base = pokemon_base_stats(env->fixed_starter_species);
    starter.box.type1 = base->type1;
    starter.box.type2 = base->type2;
    starter.box.catch_rate = base->catch_rate;
    memcpy(starter.box.moves, base->learnset, sizeof(starter.box.moves));
    for (int i = 0; i < REDCORE_NUM_MOVES; i++) {
        starter.box.pp[i] = (starter.box.moves[i] != 0) ? MOVES[starter.box.moves[i]].pp : 0;
    }
    redcore_calc_all_stats(&starter, starter.level);
    starter.box.hp = starter.max_hp;
    gs->party[0] = starter;
    gs->party_count = 1;
    gs->active_party_slot = 0;

    event_flag_set(&gs->flags, EVENT_GOT_STARTER);
    event_flag_set(&gs->flags, EVENT_OAK_ASKED_TO_CHOOSE_MON);
    event_flag_set(&gs->flags, EVENT_BATTLED_RIVAL_IN_OAKS_LAB);
    event_flag_set(&gs->flags, EVENT_GOT_POKEDEX);
    event_flag_set(&gs->flags, EVENT_GOT_POKEBALLS_FROM_OAK);
    event_flag_set(&gs->flags, EVENT_FOLLOWED_OAK_INTO_LAB);

    if (env->cfg.route22_rival_beaten) event_flag_set(&gs->flags, EVENT_BEAT_ROUTE22_RIVAL_1ST_BATTLE);
    if (env->cfg.route22_rival_2nd_beaten) event_flag_set(&gs->flags, EVENT_BEAT_ROUTE22_RIVAL_2ND_BATTLE);
    bag_add_item(&gs->bag, POKE_BALL, 5);

    gs->player.map_id = PALLET_TOWN;
    gs->player.x = 5;
    gs->player.y = 6;
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

static const uint8_t RC_BAG_TRACKED_ITEM_IDS[PKRED_LAYOUT_BAG_TRACKED_ITEMS] = {
    MASTER_BALL, ULTRA_BALL, GREAT_BALL, POKE_BALL, SAFARI_BALL,
    POTION, SUPER_POTION, HYPER_POTION, MAX_POTION, FULL_RESTORE,
    REVIVE, MAX_REVIVE, FULL_HEAL,
    ANTIDOTE, BURN_HEAL, ICE_HEAL, AWAKENING, PARLYZ_HEAL,
    ETHER, MAX_ETHER, ELIXER, MAX_ELIXER,
    ESCAPE_ROPE,
};

static const uint8_t RC_KEY_ITEM_IDS[PKRED_LAYOUT_KEY_ITEMS] = {
    TOWN_MAP, BICYCLE, OLD_AMBER,
    DOME_FOSSIL, HELIX_FOSSIL, SECRET_KEY,
    BIKE_VOUCHER, CARD_KEY, S_S_TICKET,
    GOLD_TEETH, COIN_CASE, OAKS_PARCEL,
    SILPH_SCOPE, POKE_FLUTE, LIFT_KEY,
    SAFARI_BALL,
};

static const uint8_t RC_HM_ITEM_IDS[5] = {HM_CUT, HM_FLY, HM_SURF, HM_STRENGTH, HM_FLASH};
static const uint8_t ROM_HM_ITEM_IDS[5] = {
    PKRED_ITEM_HM01_CUT, PKRED_ITEM_HM02_FLY, PKRED_ITEM_HM03_SURF, PKRED_ITEM_HM04_STRENGTH, PKRED_ITEM_HM05_FLASH,
};

static_assert(CUT == PKRED_MOVE_CUT && FLY == PKRED_MOVE_FLY && SURF == PKRED_MOVE_SURF &&
              STRENGTH == PKRED_MOVE_STRENGTH && FLASH == PKRED_MOVE_FLASH,
              "redcore HM move ids diverge from the ROM's");

static void fill_party_mon(PkMon *o, const PartyMon *m) {
    o->species = m->box.species;
    o->level = m->level;
    o->hp = m->box.hp;
    o->max_hp = m->max_hp;
    o->status = m->box.status;
    o->type1 = m->box.type1;
    o->type2 = m->box.type2;
    for (int k = 0; k < 4; k++) {
        o->moves[k] = m->box.moves[k];
        o->pp[k] = m->box.pp[k];
    }
}

static void fill_battle_mon(PkMon *o, const BattleMon *b, bool with_moves) {
    memset(o, 0, sizeof(*o));
    o->species = b->species;
    o->level = b->level;
    o->hp = (uint16_t)(b->hp < 0 ? 0 : b->hp);
    o->max_hp = (uint16_t)b->max_hp;
    o->status = b->status;
    o->type1 = b->type1;
    o->type2 = b->type2;
    if (with_moves)
        for (int k = 0; k < 4; k++) {
            o->moves[k] = b->moves[k];
            o->pp[k] = b->pp[k];
        }
}

namespace {

void *rc_create(const PkBackendConfig *cfg) {
    RcEnv *env = (RcEnv *)calloc(1, sizeof(RcEnv));
    env->cfg = *cfg;
    env->fixed_starter_species = (uint8_t)cfg->fixed_starter_species;
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

    if (cfg->screen_obs_enabled) redcore_load_atlases(cfg->assets_dir);

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
    if (full_reset) {
        memset(env->dex_owned, 0, sizeof(env->dex_owned));
        memset(env->dex_seen, 0, sizeof(env->dex_seen));
        redcore_reset_to_fixed_start(env, rng);
    }
    env->battle_menu = RC_MENU_MAIN;
    env->cursor_main = env->cursor_fight = env->cursor_item = env->cursor_party = 0;
    env->player_selected_move = 0;
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
    redcore_apply_button(env, action);
    rc_cancel_wild_battle(env);
    redcore_menu_after_step(env);
}

void rc_snapshot(void *impl, PkSnapshot *s) {
    RcEnv *env = (RcEnv *)impl;
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
    s->fainted_count = (uint8_t)rc_party_fainted_count(gs);
    s->money = gs->player.money;
    s->last_blackout_map = gs->player.last_pokecenter_map;
    s->num_bag_items = (uint8_t)gs->bag.num_slots;
    for (int i = 0; i < PKRED_LAYOUT_BAG_TRACKED_ITEMS; i++)
        s->bag_qty[PKRED_BAG_TRACKED_ITEM_IDS[i]] = (uint8_t)bag_count(&gs->bag, RC_BAG_TRACKED_ITEM_IDS[i]);
    for (int i = 0; i < PKRED_LAYOUT_KEY_ITEMS; i++)
        s->bag_qty[PKRED_KEY_ITEM_IDS[i]] = (uint8_t)bag_count(&gs->bag, RC_KEY_ITEM_IDS[i]);
    for (int i = 0; i < 5; i++)
        s->bag_qty[ROM_HM_ITEM_IDS[i]] = (uint8_t)bag_count(&gs->bag, RC_HM_ITEM_IDS[i]);

    if (rc_in_battle(gs)) {

        s->in_battle = gs->battle.is_trainer_battle ? 2 : 1;

        s->battle_type = gs->mode == GAME_MODE_SAFARI_BATTLE ? 2 : 0;
        s->selected_move = env->player_selected_move;

        bool has_player_mon = gs->mode == GAME_MODE_BATTLE || gs->mode == GAME_MODE_BATTLE_SWITCH;
        if (has_player_mon) fill_battle_mon(&s->battle_mon, &gs->battle.player, true);
        fill_battle_mon(&s->enemy_mon, &gs->battle.enemy, false);
    }

    for (int i = 0; i < pk_event_count(); i++)
        s->events[i] = env->event_bit[i] >= 0 && event_flag_get(&gs->flags, env->event_bit[i]) ? 1 : 0;
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
}

const PkBackend REDCORE_BACKEND = {
    "redcore", rc_create, rc_destroy, rc_noop, rc_noop, rc_reset, rc_noop, rc_step,
    rc_snapshot, rc_screen, rc_blackout, NULL, NULL, NULL, rc_frame_rgba, NULL,
};

}

#undef A
#undef C
#undef D
#undef F
#undef G

extern "C" const PkBackend *pk_backend_redcore(void) { return &REDCORE_BACKEND; }
