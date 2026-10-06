#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
#include "overworld.h"
#include "rc_host.h"
#include "rc_event_names.h"
}
#include "redcore_start.h"

static_assert((int)RC_BTN_A == (int)PKRED_ACTION_A && (int)RC_BTN_B == (int)PKRED_ACTION_B && (int)RC_BTN_RIGHT == (int)PKRED_ACTION_RIGHT &&
                  (int)RC_BTN_LEFT == (int)PKRED_ACTION_LEFT && (int)RC_BTN_UP == (int)PKRED_ACTION_UP &&
                  (int)RC_BTN_DOWN == (int)PKRED_ACTION_DOWN && (int)RC_BTN_COUNT == (int)PKRED_ACTION_COUNT,
              "RcButton must mirror the PokeredAction button order");
static_assert(sizeof(PkSnapshot) <= RC_HOST_HELD_BYTES, "RC_HOST_HELD_BYTES too small for PkSnapshot");

struct RcEnv {
    RcHost host;
    PkBackendConfig cfg;
    char assets_dir[256];
    PkEventTracker events;
    RcStart start;
    int *event_bit;
};

static int rc_dex_count(const uint8_t *dex) {
    int n = 0;
    for (int i = 0; i < REDCORE_DEX_BYTES; i++) n += __builtin_popcount(dex[i]);
    return n;
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


static bool rc_load_gray_image(const char *path, uint8_t **gray, int *w, int *h) {
    SetTraceLogLevel(LOG_WARNING);
    Image img = LoadImage(path);
    if (!img.data) return false;
    Color *px = LoadImageColors(img);
    uint8_t *out = (uint8_t *)malloc((size_t)img.width * img.height);
    for (int p = 0; p < img.width * img.height; p++)
        out[p] = (uint8_t)((px[p].r * 77 + px[p].g * 150 + px[p].b * 29) >> 8);
    UnloadImageColors(px);
    *gray = out;
    *w = img.width;
    *h = img.height;
    UnloadImage(img);
    return true;
}

namespace {

void rc_snapshot_now(RcEnv *env, PkSnapshot *s);

void rc_capture(void *user, void *out) { rc_snapshot_now((RcEnv *)user, (PkSnapshot *)out); }

void *rc_create(const PkBackendConfig *cfg, const PkOptions *opts) {
    RcEnv *env = (RcEnv *)calloc(1, sizeof(RcEnv));
    env->cfg = *cfg;
    snprintf(env->assets_dir, sizeof(env->assets_dir), "%s", pk_opt_str(opts, "assets_dir", "vendor/redcore/assets"));

    RcHostOptions ho;
    memset(&ho, 0, sizeof(ho));
    ho.fixed_starter_species = pk_opt_int(opts, "fixed_starter_species", 0);
    ho.nickname_prompt_enabled = pk_opt_bool(opts, "nickname_prompt_enabled", false);
    ho.npc_text_enabled = pk_opt_bool(opts, "npc_text_enabled", false);
    ho.npc_movement_enabled = pk_opt_bool(opts, "npc_movement_enabled", false);
    ho.real_battle_ui_enabled = pk_opt_bool(opts, "real_battle_ui_enabled", false);
    ho.battle_text_enabled = pk_opt_bool(opts, "battle_text_enabled", false);
    ho.disable_wild_until_badge = cfg->disable_wild_until_badge;
    ho.route22_rival_beaten = cfg->route22_rival_beaten;
    ho.route22_rival_2nd_beaten = cfg->route22_rival_2nd_beaten;
    ho.frameskip = cfg->frameskip;
    rc_host_init(&env->host, &ho);
    rc_host_set_capture(&env->host, rc_capture, env);

    PkState state;
    char err[512];
    if (!pk_state_resolve(cfg->state_path, cfg->rom_path, cfg->pkstate_cache_enabled, &state, err, sizeof(err))) {
        fprintf(stderr, "pokered: redcore backend: %s\n", err);
        exit(1);
    }
    rc_start_decode(&state, &env->start);

    int n = pk_event_count();
    env->event_bit = (int *)malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++)
        env->event_bit[i] = redcore_event_bit_by_name(pk_event_name(i));

    if (cfg->screen_obs_enabled) rc_host_load_atlases(env->assets_dir, rc_load_gray_image);

    unsigned rng = cfg->env_id;
    rc_host_start(&env->host, &env->start, &rng);
    return env;
}

void rc_destroy(void *impl) {
    RcEnv *env = (RcEnv *)impl;
    free(env->event_bit);
    free(env);
}

void rc_noop(void *) {}

void rc_reset(void *impl, bool full_reset, unsigned *rng) {
    RcEnv *env = (RcEnv *)impl;
    pk_events_rebase(&env->events);
    if (full_reset) rc_host_start(&env->host, &env->start, rng);
    rc_host_reset_ui(&env->host);
}

void rc_step(void *impl, int action, const PkSnapshot *) {
    RcEnv *env = (RcEnv *)impl;
    pk_events_stepped(&env->events);
    rc_host_press(&env->host, action);
}

void rc_snapshot_now(RcEnv *env, PkSnapshot *s) {
    const GameState *gs = &env->host.gstate;
    memset(s, 0, sizeof(*s));

    rc_host_update_dex(&env->host);
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
    s->pokedex_owned_count = (uint8_t)rc_dex_count(env->host.dex_owned);
    s->pokedex_seen_count = (uint8_t)rc_dex_count(env->host.dex_seen);
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
    if (!env->host.text.active) {
        rc_snapshot_now(env, s);
        return;
    }
    const RcTextPhase *ph = &env->host.text;
    const RcTextFrame *tf = &ph->f[ph->i < ph->n ? ph->i : (ph->n ? ph->n - 1 : 0)];
    memcpy(s, ph->held, sizeof(*s));
    s->in_battle = s->in_battle ? s->in_battle : (env->host.gstate.battle.is_trainer_battle ? 2 : 1);
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

    rc_host_render(&env->host, env->host.frame);
    for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
        const uint8_t *r0 = &env->host.frame[(sy * 2) * RC_FRAME_W];
        const uint8_t *r1 = r0 + RC_FRAME_W;
        for (int sx = 0; sx < SCALED_WIDTH; sx++)
            obs[sy * SCALED_WIDTH + sx] =
                (float)((r0[sx * 2] + r0[sx * 2 + 1] + r1[sx * 2] + r1[sx * 2 + 1]) >> 2);
    }
}

bool rc_frame_rgba(void *impl, uint8_t *rgba) {
    RcEnv *env = (RcEnv *)impl;
    uint8_t *frame = env->host.frame;
    if (env->cfg.screen_obs_enabled) rc_host_render(&env->host, frame);
    else memset(frame, 0, RC_FRAME_PIXELS);
    for (int i = 0; i < RC_FRAME_PIXELS; i++) {
        rgba[i * 4 + 0] = frame[i];
        rgba[i * 4 + 1] = frame[i];
        rgba[i * 4 + 2] = frame[i];
        rgba[i * 4 + 3] = 255;
    }
    return true;
}

void rc_blackout(void *impl) {
    RcEnv *env = (RcEnv *)impl;
    rc_host_blackout(&env->host);
    pk_events_rebase(&env->events);
}

size_t rc_state_size(void) { return rc_host_state_size(); }

bool rc_state_save(void *impl, void *buf) {
    rc_host_state_save(&((RcEnv *)impl)->host, buf);
    return true;
}

bool rc_state_load(void *impl, const void *buf) {
    RcEnv *env = (RcEnv *)impl;
    rc_host_state_load(&env->host, buf);
    pk_events_rebase(&env->events);
    return true;
}

const PkBackend REDCORE_BACKEND = {
    "redcore",
    PK_CAP_FRAME_RGBA | PK_CAP_BLACKOUT | PK_CAP_STATE_SNAPSHOT,
    PK_BTN_A | PK_BTN_B | PK_BTN_RIGHT | PK_BTN_LEFT | PK_BTN_UP | PK_BTN_DOWN,
    rc_create, rc_destroy, rc_noop, rc_noop, rc_reset, rc_noop, rc_step, rc_snapshot, rc_screen,
    rc_blackout, rc_frame_rgba,
      NULL,   NULL,
    rc_state_size, rc_state_save, rc_state_load,
};

}

PK_REGISTER_BACKEND(REDCORE_BACKEND)

#undef A
#undef C
#undef D
#undef F
#undef G
