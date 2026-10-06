#ifndef REDCORE_OCEAN_MENU_H
#define REDCORE_OCEAN_MENU_H

#include <string.h>

static int rc_move_count(const GameState *gs) {
    const BattleMon *b = &gs->battle.player;
    int n = 0;
    for (int i = 0; i < REDCORE_NUM_MOVES; i++)
        if (b->moves[i] != 0) n = i + 1;
    return n;
}

static uint8_t rc_list_cursor(uint8_t cursor, int n, int button) {
    if (n <= 0) return 0;
    if (button == PKRED_ACTION_UP && cursor > 0) return (uint8_t)(cursor - 1);
    if (button == PKRED_ACTION_DOWN && cursor + 1 < n) return (uint8_t)(cursor + 1);
    return cursor < n ? cursor : (uint8_t)(n - 1);
}

static uint8_t rc_grid_cursor(uint8_t cursor, int button) {
    switch (button) {
        case PKRED_ACTION_UP: return cursor >= 2 ? (uint8_t)(cursor - 2) : cursor;
        case PKRED_ACTION_DOWN: return cursor < 2 ? (uint8_t)(cursor + 2) : cursor;
        case PKRED_ACTION_LEFT: return (cursor & 1) ? (uint8_t)(cursor - 1) : cursor;
        case PKRED_ACTION_RIGHT: return !(cursor & 1) ? (uint8_t)(cursor + 1) : cursor;
        default: return cursor;
    }
}

#define RC_BAG_ROWS 3
enum { RC_ITEM_MSG_NOT_TIME = 1, RC_ITEM_MSG_NO_EFFECT, RC_ITEM_MSG_BOX_FULL };

static int rc_bag_entries(const GameState *gs) { return gs->bag.num_slots + 1; }

static bool rc_item_is_medicine(uint8_t id) {
    switch (id) {
        case POTION: case SUPER_POTION: case HYPER_POTION: case MAX_POTION: case FULL_RESTORE:
        case SODA_POP: case LEMONADE: case FRESH_WATER:
        case REVIVE: case MAX_REVIVE:
        case ANTIDOTE: case BURN_HEAL: case ICE_HEAL: case AWAKENING: case PARLYZ_HEAL: case FULL_HEAL:
            return true;
        default: return false;
    }
}

static void rc_bag_move(RcEnv *env, int button) {
    int entries = rc_bag_entries(&env->gstate);
    if (button == PKRED_ACTION_UP && env->cursor_item > 0) {
        env->cursor_item--;
        if (env->cursor_item < env->bag_scroll) env->bag_scroll = env->cursor_item;
    } else if (button == PKRED_ACTION_DOWN && env->cursor_item + 1 < entries) {
        env->cursor_item++;
        if (env->cursor_item >= env->bag_scroll + RC_BAG_ROWS) env->bag_scroll = (uint8_t)(env->cursor_item - (RC_BAG_ROWS - 1));
    }
}

static void rc_bag_clamp(RcEnv *env) {
    int entries = rc_bag_entries(&env->gstate);
    if (env->cursor_item >= entries) env->cursor_item = (uint8_t)(entries - 1);
    if (env->bag_scroll > env->cursor_item) env->bag_scroll = env->cursor_item;
    if (env->cursor_item >= env->bag_scroll + RC_BAG_ROWS) env->bag_scroll = (uint8_t)(env->cursor_item - (RC_BAG_ROWS - 1));
}

namespace { void rc_snapshot_now(RcEnv *env, PkSnapshot *s); }

static void rc_text_start(RcEnv *env, const PkSnapshot *held) {
    RcTextPhase *ph = &env->text;
    if (ph->n == 0) return;
    ph->active = true;
    ph->i = 0;
    ph->ticks = 0;
    ph->yn_cursor = 0;
    ph->held = *held;
}

static void rc_text_finish(RcEnv *env) {
    env->text.active = false;
    if (env->gstate.mode == GAME_MODE_BATTLE) env->battle_menu = env->text_return_menu;
    env->text_return_menu = RC_MENU_MAIN;
}

static void rc_text_input(RcEnv *env, int button) {
    RcTextPhase *ph = &env->text;
    const RcTextFrame *f = &ph->f[ph->i];
    bool advance = true;
    if (f->flags & RCF_YESNO) {
        if (button == PKRED_ACTION_UP) ph->yn_cursor = 0;
        else if (button == PKRED_ACTION_DOWN) ph->yn_cursor = 1;
        advance = button == PKRED_ACTION_A || button == PKRED_ACTION_B;
    } else if (f->flags & RCF_PROMPT) {
        advance = button == PKRED_ACTION_A || button == PKRED_ACTION_B;
    }
    if (!advance) {
        ph->ticks++;
        return;
    }
    ph->ticks = 0;
    if (++ph->i >= ph->n) rc_text_finish(env);
}

static void rc_step_engine(RcEnv *env, Action a) {
    GameState *gs = &env->gstate;
    bool narrate = env->opt.battle_text_enabled &&
                   (gs->mode == GAME_MODE_BATTLE || gs->mode == GAME_MODE_BATTLE_SWITCH);
    if (!narrate) {
        gamestate_step(gs, a);
        return;
    }
    RcTurnPre pre;
    rc_pre_capture(&pre, gs, a);
    pre.no_moves_left = env->struggle_turn != 0;
    rc_snapshot_now(env, &pre.held);
    gamestate_step(gs, a);

    bool turn_passed = memcmp(&pre.bs.rng, &gs->battle.rng, sizeof(pre.bs.rng)) != 0 ||
                       pre.mode != gs->mode || gs->battle.outcome != BATTLE_ONGOING;
    bool refused_run = a.run && pre.bs.is_trainer_battle;
    if (!turn_passed && !refused_run) return;

    RcTextPhase *ph = &env->text;
    ph->n = 0;
    RcTextBuilder tb;
    tb.ph = ph;
    tb.me = rc_disp_from_battle(&pre.bs.player);
    tb.foe = rc_disp_from_battle(&pre.bs.enemy);
    tb.base = RCF_PIC_FOE | RCF_PIC_ME | RCF_HUD_FOE | RCF_HUD_ME;
    rc_narrate_turn(&tb, &pre, gs, a);
    rc_text_start(env, &pre.held);
}
static bool rc_party_input(RcEnv *env, int button, bool forced);
static void rc_item_input(RcEnv *env, int button);

static void rc_battle_message(RcEnv *env, const char *l1, const char *l2, uint8_t return_menu) {
    GameState *gs = &env->gstate;
    RcTextPhase *ph = &env->text;
    ph->n = 0;
    RcTextBuilder tb;
    tb.ph = ph;
    tb.me = rc_disp_from_battle(&gs->battle.player);
    tb.foe = rc_disp_from_battle(&gs->battle.enemy);
    tb.base = RCF_PIC_FOE | RCF_PIC_ME | RCF_HUD_FOE | RCF_HUD_ME;
    tb_box(&tb, l1, l2);
    PkSnapshot held;
    rc_snapshot_now(env, &held);
    rc_text_start(env, &held);
    env->text_return_menu = return_menu;
}

static bool rc_any_pp_left(const BattleMon *b) {
    for (int i = 0; i < REDCORE_NUM_MOVES; i++)
        if (b->moves[i] != 0 && b->pp[i] > 0) return true;
    return false;
}

static void rc_struggle_turn(RcEnv *env) {
    BattleMon *p = &env->gstate.battle.player;
    uint8_t move0 = p->moves[0], pp0 = p->pp[0];
    p->moves[0] = STRUGGLE;
    p->pp[0] = 1;
    Action a;
    memset(&a, 0, sizeof(a));
    a.move_slot = 0;
    env->struggle_turn = 1;
    rc_step_engine(env, a);
    env->struggle_turn = 0;
    p->moves[0] = move0;
    p->pp[0] = pp0;
}

static void rc_battle_button(RcEnv *env, int button) {
    GameState *gs = &env->gstate;
    Action a;
    memset(&a, 0, sizeof(a));

    switch (env->battle_menu) {
        case RC_MENU_MAIN:
            if (button == PKRED_ACTION_A) {
                if (env->cursor_main == 0 && !rc_any_pp_left(&gs->battle.player)) {
                    rc_struggle_turn(env);
                    env->battle_menu = RC_MENU_MAIN;
                } else if (env->cursor_main == 0) {
                    env->battle_menu = RC_MENU_FIGHT;
                    int n = rc_move_count(gs);
                    env->cursor_fight = env->last_move_slot < n ? env->last_move_slot : 0;
                } else if (env->cursor_main == 1) {
                    env->battle_menu = RC_MENU_PARTY;
                    env->party_stage = 0;
                    env->cursor_party = env->cursor_party_saved < gs->party_count ? env->cursor_party_saved : 0;
                } else if (env->cursor_main == 2 && gs->bag.num_slots > 0) {
                    env->battle_menu = RC_MENU_ITEM;
                    env->item_stage = 0;
                    rc_bag_clamp(env);
                } else if (env->cursor_main == 3) {
                    a.run = 1;
                    rc_step_engine(env, a);

                }
            } else if (rc_is_directional(button)) {
                env->cursor_main = rc_grid_cursor(env->cursor_main, button);
            }
            break;

        case RC_MENU_FIGHT: {
            int n = rc_move_count(gs);
            if (button == PKRED_ACTION_B) {
                env->battle_menu = RC_MENU_MAIN;
            } else if (button == PKRED_ACTION_A) {
                uint8_t slot = env->cursor_fight;
                if (slot < n && gs->battle.player.moves[slot] != 0 && gs->battle.player.pp[slot] == 0) {
                    rc_battle_message(env, "No PP left for", "this move!", RC_MENU_FIGHT);
                } else if (slot < n && gs->battle.player.moves[slot] != 0) {
                    env->player_selected_move = gs->battle.player.moves[slot];
                    env->last_move_slot = slot;
                    a.move_slot = slot;
                    rc_step_engine(env, a);
                    env->battle_menu = RC_MENU_MAIN;
                }
            } else if (rc_is_directional(button)) {
                env->cursor_fight = rc_list_cursor(env->cursor_fight, n, button);
            }
            break;
        }

        case RC_MENU_ITEM:
            rc_item_input(env, button);
            break;

        case RC_MENU_PARTY:
            if (rc_party_input(env, button, false)) {
                env->battle_menu = RC_MENU_MAIN;
                env->cursor_main = 0;
                env->cursor_item = 0;
                env->bag_scroll = 0;
            }
            break;
    }
}

static void rc_item_input(RcEnv *env, int button) {
    GameState *gs = &env->gstate;
    int n = gs->bag.num_slots;
    switch (env->item_stage) {
        case 0: {
            if (button == PKRED_ACTION_B || (button == PKRED_ACTION_A && env->cursor_item >= n)) {
                env->battle_menu = RC_MENU_MAIN;
            } else if (button == PKRED_ACTION_A) {
                uint8_t id = gs->bag.slots[env->cursor_item].item_id;
                if (item_is_poke_ball(id)) {
                    int before = bag_count(&gs->bag, id);
                    Action a;
                    memset(&a, 0, sizeof(a));
                    a.use_item = 1;
                    a.item_id = id;
                    rc_step_engine(env, a);
                    if (bag_count(&gs->bag, id) == before) {
                        env->item_stage = 2;
                        env->item_msg = RC_ITEM_MSG_BOX_FULL;
                    } else {
                        env->battle_menu = RC_MENU_MAIN;
                    }
                } else if (rc_item_is_medicine(id)) {
                    env->item_stage = 1;
                    env->item_pending = id;
                    env->cursor_party = env->cursor_party_saved < gs->party_count ? env->cursor_party_saved : 0;
                } else {
                    env->item_stage = 2;
                    env->item_msg = RC_ITEM_MSG_NOT_TIME;
                }
            } else if (rc_is_directional(button)) {
                rc_bag_move(env, button);
            }
            return;
        }
        case 1: {
            int count = gs->party_count;
            if (button == PKRED_ACTION_B) {
                env->item_stage = 0;
            } else if (button == PKRED_ACTION_A && env->cursor_party < count) {
                int slot = env->cursor_party;
                env->cursor_party_saved = (uint8_t)slot;
                bool effect;
                if (slot == gs->active_party_slot) {
                    BattleMon probe = gs->battle.player;
                    effect = item_use_on_battle_mon(&probe, env->item_pending) != ITEM_USE_NO_EFFECT;
                } else {
                    PartyMon probe = gs->party[slot];
                    effect = item_use_on_party_mon(&probe, env->item_pending) != ITEM_USE_NO_EFFECT;
                }
                if (!effect) {
                    env->item_stage = 2;
                    env->item_msg = RC_ITEM_MSG_NO_EFFECT;
                } else {
                    Action a;
                    memset(&a, 0, sizeof(a));
                    a.use_item = 1;
                    a.item_id = env->item_pending;
                    a.item_party_slot_plus1 = (uint8_t)(slot + 1);
                    rc_step_engine(env, a);
                    env->item_stage = 0;
                    env->battle_menu = RC_MENU_MAIN;
                }
            } else if (rc_is_directional(button)) {
                env->cursor_party = rc_list_cursor(env->cursor_party, count, button);
            }
            return;
        }
        default:
            if (button == PKRED_ACTION_A || button == PKRED_ACTION_B) env->item_stage = 0;
            return;
    }
}

static bool rc_party_input(RcEnv *env, int button, bool forced) {
    GameState *gs = &env->gstate;
    int count = gs->party_count;
    switch (env->party_stage) {
        case 0:
            if (button == PKRED_ACTION_B && !forced) {
                env->cursor_party_saved = env->cursor_party;
                env->battle_menu = RC_MENU_MAIN;
            } else if (button == PKRED_ACTION_A && env->cursor_party < count) {
                env->party_stage = 1;
                env->cursor_action = 0;
            } else if (rc_is_directional(button)) {
                env->cursor_party = rc_list_cursor(env->cursor_party, count, button);
            }
            return false;
        case 1:
            if (button == PKRED_ACTION_B) {
                env->party_stage = 0;
            } else if (rc_is_directional(button)) {
                env->cursor_action = rc_list_cursor(env->cursor_action, 3, button);
            } else if (button == PKRED_ACTION_A) {
                if (env->cursor_action == 1) {
                    env->party_stage = 2;
                    return false;
                }
                env->party_stage = 0;
                if (env->cursor_action != 0) return false;
                if (env->cursor_party >= count) return false;
                if (env->cursor_party == gs->active_party_slot) {
                    env->party_stage = 3;
                    env->party_msg = 1;
                    return false;
                }
                if (rc_party_mon(gs, env->cursor_party).box.hp == 0) {
                    env->party_stage = 3;
                    env->party_msg = 2;
                    return false;
                }
                env->cursor_party_saved = env->cursor_party;
                Action a;
                memset(&a, 0, sizeof(a));
                if (forced) a.a = 1; else a.switch_party = 1;
                a.menu_choice = env->cursor_party;
                rc_step_engine(env, a);
                return true;
            }
            return false;
        default:
            if (button == PKRED_ACTION_A || button == PKRED_ACTION_B) env->party_stage = 0;
            return false;
    }
}

static void rc_switch_button(RcEnv *env, int button) { rc_party_input(env, button, true); }

static void rc_safari_button(RcEnv *env, int button) {
    if (button == PKRED_ACTION_A) {
        Action a;
        memset(&a, 0, sizeof(a));
        a.a = 1;
        a.menu_choice = env->cursor_main;
        rc_step_engine(env, a);
    } else if (rc_is_directional(button)) {
        env->cursor_main = rc_grid_cursor(env->cursor_main, button);
    }
}

static void rc_starter_button(RcEnv *env, int button) {
    if (button == PKRED_ACTION_A) {
        Action a;
        memset(&a, 0, sizeof(a));
        a.a = 1;
        a.menu_choice = env->cursor_party;
        rc_step_engine(env, a);
    } else if (button == PKRED_ACTION_RIGHT || button == PKRED_ACTION_DOWN) {
        env->cursor_party = (uint8_t)((env->cursor_party + 1) % 3);
    } else if (button == PKRED_ACTION_LEFT || button == PKRED_ACTION_UP) {
        env->cursor_party = (uint8_t)((env->cursor_party + 2) % 3);
    }
}

static void rc_naming_button(RcEnv *env, int button) {
    Action a;
    memset(&a, 0, sizeof(a));
    switch (button) {
        case PKRED_ACTION_A: a.a = 1; break;
        case PKRED_ACTION_B: a.b = 1; break;
        case PKRED_ACTION_UP: a.up = 1; break;
        case PKRED_ACTION_DOWN: a.down = 1; break;
        case PKRED_ACTION_LEFT: a.left = 1; break;
        case PKRED_ACTION_RIGHT: a.right = 1; break;
        default: return;
    }
    rc_step_engine(env, a);
}

static void redcore_apply_button(RcEnv *env, int button) {
    GameState *gs = &env->gstate;
    if (button < 0 || button >= PKRED_ACTION_COUNT) return;

    Action a;
    memset(&a, 0, sizeof(a));

    switch (gs->mode) {
        case GAME_MODE_OVERWORLD:
            switch (button) {
                case PKRED_ACTION_A: a.a = 1; break;
                case PKRED_ACTION_UP: a.up = 1; break;
                case PKRED_ACTION_DOWN: a.down = 1; break;
                case PKRED_ACTION_LEFT: a.left = 1; break;
                case PKRED_ACTION_RIGHT: a.right = 1; break;
                default: return;
            }
            rc_step_engine(env, a);
            break;
        case GAME_MODE_TEXTBOX:
            if (button == PKRED_ACTION_A || button == PKRED_ACTION_B) {
                a.a = 1;
                rc_step_engine(env, a);
            }
            break;
        case GAME_MODE_BATTLE: rc_battle_button(env, button); break;
        case GAME_MODE_BATTLE_SWITCH: rc_switch_button(env, button); break;
        case GAME_MODE_SAFARI_BATTLE: rc_safari_button(env, button); break;
        case GAME_MODE_STARTER_SELECT: rc_starter_button(env, button); break;
        case GAME_MODE_NAMING: rc_naming_button(env, button); break;
        default: break;
    }
}

static void redcore_menu_after_step(RcEnv *env) {
    GameState *gs = &env->gstate;
    if (gs->mode == env->prev_mode) return;
    if (gs->mode == GAME_MODE_BATTLE || gs->mode == GAME_MODE_SAFARI_BATTLE) {
        env->battle_menu = RC_MENU_MAIN;
        env->party_stage = 0;
        env->cursor_main = 0;
        env->cursor_item = 0;
        env->bag_scroll = 0;
        env->item_stage = 0;
        if (env->prev_mode != GAME_MODE_BATTLE_SWITCH) {
            env->player_selected_move = 0;
            env->cursor_party_saved = 0;
            if (env->opt.battle_text_enabled && gs->mode == GAME_MODE_BATTLE) {
                RcTextBuilder tb;
                tb.ph = &env->text;
                tb.ph->n = 0;
                rc_narrate_intro(&tb, gs);
                PkSnapshot held;
                rc_snapshot_now(env, &held);
                rc_text_start(env, &held);
            }
        }
    } else if (gs->mode == GAME_MODE_BATTLE_SWITCH) {
        env->party_stage = 0;
        env->cursor_party = env->cursor_party_saved < gs->party_count ? env->cursor_party_saved : 0;
    } else if (gs->mode == GAME_MODE_STARTER_SELECT) {
        env->cursor_party = 0;
    }
}

#endif
