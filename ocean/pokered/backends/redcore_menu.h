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

static bool rc_item_usable_in_battle(const GameState *gs, uint8_t item_id) {
    if (item_is_poke_ball(item_id)) {

        return gs->mode == GAME_MODE_BATTLE && !gs->battle.is_trainer_battle;
    }
    BattleMon scratch = gs->battle.player;
    return item_use_on_battle_mon(&scratch, item_id) != ITEM_USE_NO_EFFECT;
}

static void rc_step_engine(RcEnv *env, Action a) { gamestate_step(&env->gstate, a); }

static void rc_battle_button(RcEnv *env, int button) {
    GameState *gs = &env->gstate;
    Action a;
    memset(&a, 0, sizeof(a));

    switch (env->battle_menu) {
        case RC_MENU_MAIN:
            if (button == PKRED_ACTION_A) {
                if (env->cursor_main == 0) {
                    env->battle_menu = RC_MENU_FIGHT;
                    env->cursor_fight = 0;
                } else if (env->cursor_main == 1) {
                    env->battle_menu = RC_MENU_PARTY;
                    env->cursor_party = gs->active_party_slot;
                } else if (env->cursor_main == 2 && gs->bag.num_slots > 0) {
                    env->battle_menu = RC_MENU_ITEM;
                    env->cursor_item = 0;
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
                if (slot < n && gs->battle.player.moves[slot] != 0) {
                    env->player_selected_move = gs->battle.player.moves[slot];
                    a.move_slot = slot;
                    rc_step_engine(env, a);
                    env->battle_menu = RC_MENU_MAIN;
                }
            } else if (rc_is_directional(button)) {
                env->cursor_fight = rc_list_cursor(env->cursor_fight, n, button);
            }
            break;
        }

        case RC_MENU_ITEM: {
            int n = gs->bag.num_slots;
            if (button == PKRED_ACTION_B) {
                env->battle_menu = RC_MENU_MAIN;
            } else if (button == PKRED_ACTION_A) {
                if (env->cursor_item < n) {
                    uint8_t id = gs->bag.slots[env->cursor_item].item_id;
                    if (rc_item_usable_in_battle(gs, id)) {
                        a.use_item = 1;
                        a.item_id = id;
                        rc_step_engine(env, a);
                        env->battle_menu = RC_MENU_MAIN;
                    }
                }
            } else if (rc_is_directional(button)) {
                env->cursor_item = rc_list_cursor(env->cursor_item, n, button);
            }
            break;
        }

        case RC_MENU_PARTY:
            if (button == PKRED_ACTION_B) {
                env->battle_menu = RC_MENU_MAIN;
            } else if (button == PKRED_ACTION_A) {
                if (env->cursor_party < gs->party_count && env->cursor_party != gs->active_party_slot &&
                    rc_party_mon(gs, env->cursor_party).box.hp > 0) {
                    a.switch_party = 1;
                    a.menu_choice = env->cursor_party;
                    rc_step_engine(env, a);
                    env->battle_menu = RC_MENU_MAIN;
                }

            } else if (rc_is_directional(button)) {
                env->cursor_party = rc_list_cursor(env->cursor_party, gs->party_count, button);
            }
            break;
    }
}

static void rc_switch_button(RcEnv *env, int button) {
    GameState *gs = &env->gstate;
    if (button == PKRED_ACTION_A) {
        Action a;
        memset(&a, 0, sizeof(a));
        a.a = 1;
        a.menu_choice = env->cursor_party;
        rc_step_engine(env, a);
    } else if (rc_is_directional(button)) {
        env->cursor_party = rc_list_cursor(env->cursor_party, gs->party_count, button);
    }
}

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
        env->cursor_main = 0;
        if (env->prev_mode != GAME_MODE_BATTLE_SWITCH) env->player_selected_move = 0;
    } else if (gs->mode == GAME_MODE_BATTLE_SWITCH) {
        env->cursor_party = 0;

        for (int i = 0; i < gs->party_count; i++) {
            if (rc_party_mon(gs, i).box.hp > 0) { env->cursor_party = (uint8_t)i; break; }
        }
    } else if (gs->mode == GAME_MODE_STARTER_SELECT) {
        env->cursor_party = 0;
    }
}

#endif
