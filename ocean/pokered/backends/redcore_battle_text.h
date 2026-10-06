#ifndef REDCORE_OCEAN_BATTLE_TEXT_H
#define REDCORE_OCEAN_BATTLE_TEXT_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../pokered_backend.h"
#include "redcore_battle_gfx.h"
#include "redcore_item_names.h"

#define RC_TEXT_MAX_FRAMES 224
#define RC_TEXT_LINE 26

enum {
    RCF_BLACK = 1 << 0,
    RCF_OVERWORLD = 1 << 1,
    RCF_HUD_FOE = 1 << 2,
    RCF_HUD_ME = 1 << 3,
    RCF_PIC_FOE = 1 << 4,
    RCF_PIC_ME = 1 << 5,
    RCF_PIC_TRAINER = 1 << 6,
    RCF_PROMPT = 1 << 7,
    RCF_BOX = 1 << 8,
    RCF_STATS = 1 << 9,
    RCF_YESNO = 1 << 10,
};

typedef struct {
    uint8_t species, level, status;
    int32_t hp;
    uint16_t max_hp;
} RcDispMon;

typedef struct {
    char l1[RC_TEXT_LINE], l2[RC_TEXT_LINE];
    uint16_t flags;
    RcDispMon me, foe;
} RcTextFrame;

typedef struct {
    bool active;
    uint16_t n, i;
    uint8_t ticks;
    uint8_t yn_cursor;
    uint16_t stats[5];
    PkSnapshot held;
    RcTextFrame f[RC_TEXT_MAX_FRAMES];
} RcTextPhase;

static const char *rc_species_name(unsigned species) {
    return (species >= 1 && species <= RC_SPECIES_NAMES_COUNT) ? RC_SPECIES_NAMES[species - 1] : "MISSINGNO.";
}

static const char *rc_move_name(unsigned move) {
    return (move >= 1 && move <= RC_MOVE_NAMES_COUNT) ? RC_MOVE_NAMES[move - 1] : "-";
}

static const char *rc_type_name(unsigned type) {
    return type < RC_TYPE_NAMES_COUNT ? RC_TYPE_NAMES[type] : "NORMAL";
}

typedef struct {
    RcTextPhase *ph;
    RcDispMon me, foe;
    uint16_t base;
} RcTextBuilder;

static RcDispMon rc_disp_from_battle(const BattleMon *b) {
    RcDispMon d;
    memset(&d, 0, sizeof(d));
    d.species = b->species;
    d.level = b->level;
    d.status = b->status;
    d.hp = b->hp < 0 ? 0 : b->hp;
    d.max_hp = b->max_hp;
    return d;
}

static void tb_frame(RcTextBuilder *tb, const char *l1, const char *l2, uint16_t flags) {
    RcTextPhase *ph = tb->ph;
    if (ph->n >= RC_TEXT_MAX_FRAMES) return;
    RcTextFrame *f = &ph->f[ph->n++];
    memset(f, 0, sizeof(*f));
    snprintf(f->l1, sizeof(f->l1), "%s", l1 ? l1 : "");
    snprintf(f->l2, sizeof(f->l2), "%s", l2 ? l2 : "");
    f->flags = flags;
    f->me = tb->me;
    f->foe = tb->foe;
}

static void tb_hold(RcTextBuilder *tb, int n, const char *l1, const char *l2) {
    for (int i = 0; i < n; i++) tb_frame(tb, l1, l2, tb->base | RCF_BOX);
}

static void tb_blank(RcTextBuilder *tb, int n) { tb_hold(tb, n, "", ""); }

static void tb_partial(const char *l1, const char *l2, int pct, char *o1, char *o2) {
    int n1 = (int)strlen(l1), n2 = (int)strlen(l2);
    int k = (n1 + n2) * pct / 100;
    if (k < 1) k = 1;
    int k1 = k < n1 ? k : n1, k2 = k > n1 ? k - n1 : 0;
    snprintf(o1, RC_TEXT_LINE, "%.*s", k1, l1);
    snprintf(o2, RC_TEXT_LINE, "%.*s", k2, l2);
}

static void tb_box(RcTextBuilder *tb, const char *l1, const char *l2) {
    char a[RC_TEXT_LINE], b[RC_TEXT_LINE];
    tb_partial(l1, l2, 15, a, b);
    tb_frame(tb, a, b, tb->base | RCF_BOX);
    tb_partial(l1, l2, 70, a, b);
    tb_frame(tb, a, b, tb->base | RCF_BOX);
    tb_frame(tb, l1, l2, tb->base | RCF_BOX | RCF_PROMPT);
}

static void tb_auto(RcTextBuilder *tb, int typing, int hold, const char *l1, const char *l2) {
    char a[RC_TEXT_LINE], b[RC_TEXT_LINE];
    for (int i = 0; i < typing; i++) {
        tb_partial(l1, l2, typing == 1 ? 40 : 30 + 40 * i, a, b);
        tb_frame(tb, a, b, tb->base | RCF_BOX);
    }
    tb_hold(tb, hold, l1, l2);
}

static void tb_box3(RcTextBuilder *tb, const char *l1, const char *l2, const char *l3) {
    char a[RC_TEXT_LINE], b[RC_TEXT_LINE];
    tb_partial(l1, l2, 30, a, b);
    tb_frame(tb, a, b, tb->base | RCF_BOX);
    tb_hold(tb, 1, l1, l2);
    tb_partial(l2, l3, 50, a, b);
    tb_frame(tb, a, b, tb->base | RCF_BOX);
    tb_frame(tb, l2, l3, tb->base | RCF_BOX | RCF_PROMPT);
}

typedef struct {
    BattleState bs;
    GameMode mode;
    uint8_t active_slot, party_count;
    uint8_t level[REDCORE_MAX_PARTY];
    uint32_t exp[REDCORE_MAX_PARTY];
    uint32_t money;
    bool have_tr;
    bool ai_acted;
    bool no_moves_left;
    TurnResult tr;
    uint8_t enemy_move;
    uint8_t player_move;
    PkSnapshot held;
} RcTurnPre;

static void rc_pre_capture(RcTurnPre *pre, const GameState *gs, Action a) {
    memset(&pre->tr, 0, sizeof(pre->tr));
    pre->bs = gs->battle;
    pre->mode = gs->mode;
    pre->active_slot = gs->active_party_slot;
    pre->party_count = gs->party_count;
    for (int i = 0; i < REDCORE_MAX_PARTY; i++) {
        pre->level[i] = gs->party[i].level;
        pre->exp[i] = gs->party[i].box.exp;
    }
    pre->money = gs->player.money;
    pre->have_tr = false;
    pre->ai_acted = false;
    pre->no_moves_left = false;
    pre->enemy_move = 0;
    pre->player_move = 0;

    bool plain_move = gs->mode == GAME_MODE_BATTLE && !a.use_item && !a.switch_party && !a.run &&
                      a.move_slot < REDCORE_NUM_MOVES && gs->battle.player.moves[a.move_slot] != 0;
    if (plain_move) {
        BattleState b = gs->battle;
        pre->player_move = b.player.moves[a.move_slot];
        if (battle_ai_take_turn(&b)) {
            pre->ai_acted = true;
            pre->tr = battle_run_player_only_turn(&b, a.move_slot);
        } else {
            uint8_t enemy_slot = battle_select_enemy_move(&b);
            pre->enemy_move = b.enemy.moves[enemy_slot & 3];
            pre->tr = battle_run_turn(&b, a.move_slot, enemy_slot);
        }
        pre->have_tr = true;
    }
}

static const char *rc_nick(const GameState *gs, int slot, char *buf, size_t cap) {
    const char *n = (slot >= 0 && slot < gs->party_count) ? gs->party[slot].nickname : "";
    if (n[0]) {
        snprintf(buf, cap, "%s", n);
        return buf;
    }
    return rc_species_name(slot >= 0 && slot < gs->party_count ? gs->party[slot].box.species : 0);
}

static int rc_effectiveness(unsigned move_type, const BattleMon *def) {
    int e = TYPE_CHART[move_type][def->type1];
    if (def->type2 != def->type1) e = e * TYPE_CHART[move_type][def->type2] / 10;
    return e;
}

static const char *rc_status_text(uint8_t status) {
    if (status & 7) return "SLP";
    if (status & (1 << PSN)) return "PSN";
    if (status & (1 << BRN)) return "BRN";
    if (status & (1 << FRZ)) return "FRZ";
    if (status & (1 << PAR)) return "PAR";
    return "";
}

static void rc_stat_text(int stat, bool up, bool sharp, char *l1, char *l2, const char *who_prefix, const char *who) {
    static const char *const S[6] = {"ATTACK", "DEFENSE", "SPEED", "SPECIAL", "ACCURACY", "EVASION"};
    snprintf(l1, RC_TEXT_LINE, "%s%s's", who_prefix, who);
    snprintf(l2, RC_TEXT_LINE, "%s %s%s!", S[stat], sharp ? "sharply " : "", up ? "rose" : "fell");
}

static void rc_narrate_effects(RcTextBuilder *tb, const RcTurnPre *pre, const GameState *post, bool by_player,
                               uint8_t move, const char *who, const char *pn, const char *ename) {
    const unsigned E = MOVES[move].effect;
    const BattleMon *pa = by_player ? &pre->bs.player : &pre->bs.enemy;
    const BattleMon *qa = by_player ? &post->battle.player : &post->battle.enemy;
    const BattleMon *pd = by_player ? &pre->bs.enemy : &pre->bs.player;
    const BattleMon *qd = by_player ? &post->battle.enemy : &post->battle.player;
    char tgt[RC_TEXT_LINE * 2], l1[RC_TEXT_LINE * 2], l2[RC_TEXT_LINE * 2];
    if (by_player) snprintf(tgt, sizeof(tgt), "Enemy %s", ename);
    else snprintf(tgt, sizeof(tgt), "%s", pn);

    int stat = -1;
    bool up = false, sharp = false, self = false;
    if (E >= ATTACK_UP1_EFFECT && E <= EVASION_UP1_EFFECT) { stat = E - ATTACK_UP1_EFFECT; up = true; self = true; }
    else if (E >= ATTACK_DOWN1_EFFECT && E <= EVASION_DOWN1_EFFECT) { stat = E - ATTACK_DOWN1_EFFECT; }
    else if (E >= ATTACK_UP2_EFFECT && E <= EVASION_UP2_EFFECT) { stat = E - ATTACK_UP2_EFFECT; up = sharp = self = true; }
    else if (E >= ATTACK_DOWN2_EFFECT && E <= EVASION_DOWN2_EFFECT) { stat = E - ATTACK_DOWN2_EFFECT; sharp = true; }
    else if (E >= ATTACK_DOWN_SIDE_EFFECT && E <= SPECIAL_DOWN_SIDE_EFFECT) { stat = E - ATTACK_DOWN_SIDE_EFFECT; }
    if (stat >= 0) {
        const BattleMon *b = self ? pa : pd, *q = self ? qa : qd;
        if (b->stat_stage[stat] != q->stat_stage[stat]) {
            const char *prefix = "";
            char name[RC_TEXT_LINE * 2];
            if (self) snprintf(name, sizeof(name), "%s", who);
            else snprintf(name, sizeof(name), "%s", tgt);
            rc_stat_text(stat, up, sharp, l1, l2, prefix, name);
            tb_box(tb, l1, l2);
        }
        return;
    }

    bool status_move = E == POISON_SIDE_EFFECT1 || E == POISON_SIDE_EFFECT2 || E == POISON_EFFECT ||
                       E == BURN_SIDE_EFFECT1 || E == BURN_SIDE_EFFECT2 || E == FREEZE_SIDE_EFFECT1 ||
                       E == FREEZE_SIDE_EFFECT2 || E == PARALYZE_SIDE_EFFECT1 || E == PARALYZE_SIDE_EFFECT2 ||
                       E == PARALYZE_EFFECT || E == SLEEP_EFFECT;
    if (status_move && pd->status == 0 && qd->status != 0) {
        if (qd->status & 7) tb_box(tb, tgt, "fell asleep!");
        else if (qd->status & (1 << PSN)) tb_box(tb, tgt, "was poisoned!");
        else if (qd->status & (1 << BRN)) tb_box(tb, tgt, "was burned!");
        else if (qd->status & (1 << FRZ)) tb_box(tb, tgt, "was frozen solid!");
        else if (qd->status & (1 << PAR)) {
            snprintf(l1, sizeof(l1), "%s's", tgt);
            tb_box3(tb, l1, "paralyzed! It may", "not attack!");
        }
        return;
    }
    if ((E == CONFUSION_EFFECT || E == CONFUSION_SIDE_EFFECT) && pd->confusion_counter == 0 && qd->confusion_counter > 0)
        tb_box(tb, tgt, "became confused!");
}

static void rc_narrate_turn(RcTextBuilder *tb, const RcTurnPre *pre, const GameState *post, Action a) {
    const GameState *gs = post;
    char l1[RC_TEXT_LINE * 2], l2[RC_TEXT_LINE * 2];
    char nick[24];
    const char *pname = rc_nick(gs, pre->active_slot, nick, sizeof(nick));
    char pn[24];
    snprintf(pn, sizeof(pn), "%s", pname);
    const char *ename = rc_species_name(pre->bs.enemy.species);
    const bool trainer = pre->bs.is_trainer_battle != 0;
    const char *plr = gs->player.name[0] ? gs->player.name : "RED";

    RcDispMon *me = &tb->me, *foe = &tb->foe;
    bool foe_fainted = false, me_fainted = false;

    if (pre->mode == GAME_MODE_BATTLE_SWITCH) {
        const char *nn = rc_nick(gs, post->active_party_slot, nick, sizeof(nick));
        snprintf(l1, sizeof(l1), "Go! %s!", nn);
        *me = rc_disp_from_battle(&post->battle.player);
        tb->base &= ~RCF_PIC_ME;
        tb_auto(tb, 2, 2, l1, "");
        tb->base |= RCF_PIC_ME;
        tb_blank(tb, 1);
        return;
    }

    if (a.run) {
        if (trainer) {
            tb_box3(tb, "No! There's no", "running from a", "trainer battle!");
            return;
        }
        if (post->battle.outcome == BATTLE_PLAYER_RAN || post->mode != pre->mode) {
            tb_box(tb, "Got away safely!", "");
            return;
        }
        tb_box(tb, "Can't escape!", "");
    }

    bool enemy_only = false;
    if (a.switch_party) {
        snprintf(l1, sizeof(l1), "%s", pn);
        tb_auto(tb, 1, 3, l1, "Come back!");
        const char *nn = rc_nick(gs, post->active_party_slot, nick, sizeof(nick));
        snprintf(l1, sizeof(l1), "Go! %s!", nn);
        *me = rc_disp_from_battle(&post->battle.player);
        tb->base = (tb->base & ~RCF_PIC_ME);
        tb_auto(tb, 2, 2, l1, "");
        tb->base |= RCF_PIC_ME;
        tb_blank(tb, 1);
        snprintf(pn, sizeof(pn), "%s", nn);
        enemy_only = true;
    } else if (a.use_item) {
        char item[24];
        rc_item_name(a.item_id, item, sizeof(item));
        snprintf(l1, sizeof(l1), "%s used", plr);
        snprintf(l2, sizeof(l2), "%s!", item);
        tb_auto(tb, 1, 4, l1, l2);
        if (item_is_poke_ball(a.item_id)) {
            if (trainer) {
                tb_box(tb, "The trainer blocked the BALL!", "");
                tb_box(tb, "Don't be a thief!", "");
            } else if (post->battle.outcome == BATTLE_PLAYER_CAUGHT_ENEMY) {
                tb_hold(tb, 6, "", "");
                snprintf(l1, sizeof(l1), "All right!");
                snprintf(l2, sizeof(l2), "%s was", ename);
                tb_box3(tb, l1, l2, "caught!");
                return;
            } else {
                tb_hold(tb, 6, "", "");
                float frac = pre->bs.enemy.max_hp ? (float)pre->bs.enemy.hp / pre->bs.enemy.max_hp : 1.0f;
                if (frac > 0.66f) tb_box(tb, "Darn! The POK\001MON", "broke free!");
                else if (frac > 0.33f) tb_box(tb, "Aww! It appeared", "to be caught!");
                else tb_box(tb, "Shoot! It was so", "close too!");
            }
        } else {
            *me = rc_disp_from_battle(&post->battle.player);
            if (me->hp > pre->bs.player.hp) {
                snprintf(l2, sizeof(l2), "recovered by %d!", (int)(me->hp - pre->bs.player.hp));
                tb_auto(tb, 1, 3, pn, l2);
            }
        }
        enemy_only = true;
    }

    struct Atk { bool player; uint8_t move; bool missed, crit, faint; uint16_t dmg; uint8_t hits; bool confused; };
    Atk atk[2];
    int natk = 0;
    if (pre->have_tr) {
        for (int i = 0; i < pre->tr.num_events && natk < 2; i++) {
            const TurnEvent *e = &pre->tr.events[i];
            Atk k;
            k.player = e->attacker_is_player;
            k.move = k.player ? pre->player_move : pre->enemy_move;
            k.missed = e->missed;
            k.crit = e->critical;
            k.faint = e->fainted_target;
            k.dmg = e->damage_dealt;
            k.hits = e->num_hits;
            k.confused = e->self_hit_confusion;
            atk[natk++] = k;
        }
    } else if (enemy_only && post->battle.enemy.last_move_used != 0) {
        Atk k;
        k.player = false;
        k.move = post->battle.enemy.last_move_used;
        k.missed = false;
        k.crit = false;
        k.dmg = pre->bs.player.hp > post->battle.player.hp ? (uint16_t)(pre->bs.player.hp - post->battle.player.hp) : 0;
        k.faint = post->battle.player.hp <= 0 && pre->bs.player.hp > 0;
        k.hits = 1;
        k.confused = false;
        atk[natk++] = k;
    }

    if (pre->no_moves_left) {
        snprintf(l1, sizeof(l1), "%s has no", pn);
        tb_auto(tb, 1, 3, l1, "moves left!");
    }
    for (int i = 0; i < natk; i++) {
        const Atk *k = &atk[i];
        RcDispMon *att = k->player ? me : foe;
        RcDispMon *def = k->player ? foe : me;
        const BattleMon *defmon = k->player ? &pre->bs.enemy : &pre->bs.player;
        char who[RC_TEXT_LINE * 2];
        if (k->player) snprintf(who, sizeof(who), "%s", pn);
        else snprintf(who, sizeof(who), "%s%s", "Enemy ", ename);
        (void)att;

        if (k->confused) {
            tb_box(tb, who, "hurt itself in its confusion!");
            continue;
        }
        {
            const BattleMon *am = k->player ? &pre->bs.player : &pre->bs.enemy;
            const BattleMon *bm = k->player ? &post->battle.player : &post->battle.enemy;
            if (am->status & 7) {
                tb_box(tb, who, (bm->status & 7) ? "is fast asleep!" : "woke up!");
                continue;
            }
            if (am->status & (1 << FRZ)) {
                tb_box(tb, who, "is frozen solid!");
                continue;
            }
            if ((am->status & (1 << PAR)) && !k->missed && k->dmg == 0 && MOVES[k->move].power > 0 &&
                rc_effectiveness(MOVES[k->move].type, defmon) != 0) {
                snprintf(l1, sizeof(l1), "%s's", who);
                tb_box(tb, l1, "fully paralyzed!");
                continue;
            }
        }
        snprintf(l2, sizeof(l2), "used %s!", rc_move_name(k->move));
        int typing = k->player ? 1 : 2, old_hold = k->player ? 4 : 6, new_hold = k->player ? 3 : 2;
        {
            char part[RC_TEXT_LINE];
            if (k->player) {
                tb_frame(tb, who, "used", tb->base | RCF_BOX);
            } else {
                snprintf(part, sizeof(part), "%.8s", who);
                tb_frame(tb, part, "", tb->base | RCF_BOX);
                snprintf(part, sizeof(part), "%.7s", l2);
                tb_frame(tb, who, part, tb->base | RCF_BOX);
            }
            (void)typing;
            tb_hold(tb, old_hold, who, l2);
        }
        if (k->dmg > 0 && !k->missed) {
            int32_t nh = def->hp - (int32_t)k->dmg;
            def->hp = nh < 0 ? 0 : nh;
        }
        tb_hold(tb, new_hold, who, l2);

        if (k->missed) {
            snprintf(l1, sizeof(l1), "%s's", who);
            tb_box(tb, l1, "attack missed!");
        } else {
            if (k->crit) tb_box(tb, "Critical hit!", "");
            unsigned mt = MOVES[k->move].type;
            if (k->dmg > 0 || MOVES[k->move].power > 0) {
                int eff = rc_effectiveness(mt, defmon);
                if (eff == 0) {
                    snprintf(l2, sizeof(l2), "%s!", k->player ? ename : pn);
                    tb_box(tb, "It doesn't affect", l2);
                } else if (eff > 10) {
                    tb_box(tb, "It's super", "effective!");
                } else if (eff < 10 && k->dmg > 0) {
                    tb_box(tb, "It's not very", "effective...");
                }
            }
            if (k->dmg > 0 && MOVES[k->move].effect == RECOIL_EFFECT) {
                snprintf(l1, sizeof(l1), "%s's", who);
                tb_box(tb, l1, "hit with recoil!");
            }
            if (k->hits > 1) {
                snprintf(l2, sizeof(l2), "%d times!", k->hits);
                tb_box(tb, k->player ? "Hit the enemy" : "Hit", l2);
            }
        }
        if (k->faint) {
            if (k->player) foe_fainted = true; else me_fainted = true;
            break;
        }
        if (!k->missed) rc_narrate_effects(tb, pre, post, k->player, k->move, who, pn, ename);
    }

    if (pre->ai_acted && !foe_fainted && !me_fainted) {
        const char *tn = pre->bs.trainer_class_index < RC_TRAINER_NAMES_COUNT
                             ? RC_TRAINER_NAMES[pre->bs.trainer_class_index] : "TRAINER";
        const BattleMon *pe = &pre->bs.enemy, *qe = &post->battle.enemy;
        if (qe->species != pe->species) {
            snprintf(l1, sizeof(l1), "%s with-", tn);
            snprintf(l2, sizeof(l2), "drew %s!", rc_species_name(pe->species));
            tb_box(tb, l1, l2);
            snprintf(l1, sizeof(l1), "%s sent", tn);
            snprintf(l2, sizeof(l2), "out %s!", rc_species_name(qe->species));
            *foe = rc_disp_from_battle(qe);
            tb_auto(tb, 2, 4, l1, l2);
            tb_blank(tb, 1);
        } else {
            const char *item = NULL;
            if (qe->hp > pe->hp) {
                int d = qe->hp - pe->hp;
                item = d <= 20 ? "POTION" : d <= 50 ? "SUPER POTION" : d <= 200 ? "HYPER POTION" : "FULL RESTORE";
            } else if (pe->status != 0 && qe->status == 0) {
                item = "FULL HEAL";
            } else {
                static const char *const X[4] = {"X ATTACK", "X DEFEND", "X SPEED", "X SPECIAL"};
                for (int st = 0; st < 4 && !item; st++)
                    if (qe->stat_stage[st] > pe->stat_stage[st]) item = X[st];
                if (!item) item = "GUARD SPEC.";
            }
            snprintf(l1, sizeof(l1), "%s", tn);
            snprintf(l2, sizeof(l2), "used %s", item);
            char l3[RC_TEXT_LINE * 2];
            snprintf(l3, sizeof(l3), "on %s!", ename);
            tb_hold(tb, 1, l1, l2);
            tb_hold(tb, 4, l2, l3);
            foe->hp = qe->hp;
        }
    }

    if (!foe_fainted && !me_fainted) {
        struct Side { RcDispMon *d; const BattleMon *q; const char *name; } sides[2] = {
            {me, &post->battle.player, pn}, {foe, &post->battle.enemy, nullptr}};
        for (int si = 0; si < 2; si++) {
            Side *sd = &sides[si];
            if (sd->q->species != sd->d->species || sd->q->hp >= sd->d->hp) continue;
            char who2[RC_TEXT_LINE * 2];
            if (sd->name) snprintf(who2, sizeof(who2), "%s's", sd->name);
            else snprintf(who2, sizeof(who2), "Enemy %s's", ename);
            if (sd->q->status & (1 << PSN)) tb_box(tb, who2, "hurt by poison!");
            else if (sd->q->status & (1 << BRN)) tb_box(tb, who2, "hurt by the burn!");
            else continue;
            sd->d->hp = sd->q->hp < 0 ? 0 : sd->q->hp;
        }
    }

    if (!foe_fainted && !me_fainted) {
        if (post->battle.outcome == BATTLE_PLAYER_WON) foe_fainted = true;
        else if (post->battle.outcome == BATTLE_PLAYER_LOST || post->mode == GAME_MODE_BATTLE_SWITCH) me_fainted = true;
    }

    if (foe_fainted) {
        foe->hp = 0;
        tb->base &= ~RCF_PIC_FOE;
        snprintf(l2, sizeof(l2), "%s", ename);
        snprintf(l1, sizeof(l1), "Enemy %s", l2);
        tb_box(tb, l1, "fainted!");

        const PartyMon *pm = &post->party[pre->active_slot];
        uint32_t gained = pm->box.exp > pre->exp[pre->active_slot] ? pm->box.exp - pre->exp[pre->active_slot] : 0;
        if (gained > 0) {
            snprintf(l1, sizeof(l1), "%s gained", pn);
            snprintf(l2, sizeof(l2), "%u EXP. Points!", (unsigned)gained);
            if (trainer) tb_box3(tb, l1, "a boosted", l2);
            else tb_box(tb, l1, l2);
            if (pm->level > pre->level[pre->active_slot]) {
                snprintf(l1, sizeof(l1), "%s grew", pn);
                snprintf(l2, sizeof(l2), "to level %u!", pm->level);
                tb_box(tb, l1, l2);
                tb->ph->stats[0] = pm->max_hp;
                tb->ph->stats[1] = pm->attack;
                tb->ph->stats[2] = pm->defense;
                tb->ph->stats[3] = pm->speed;
                tb->ph->stats[4] = pm->special;
                tb_frame(tb, "", "", tb->base | RCF_BOX | RCF_STATS | RCF_PROMPT);
                me->level = pm->level;
            }
        }
        if (trainer) {
            if (post->battle.outcome == BATTLE_PLAYER_WON || post->mode == GAME_MODE_OVERWORLD) {
                const char *tn = pre->bs.trainer_class_index < RC_TRAINER_NAMES_COUNT
                                     ? RC_TRAINER_NAMES[pre->bs.trainer_class_index] : "TRAINER";
                snprintf(l1, sizeof(l1), "%s defeated", plr);
                snprintf(l2, sizeof(l2), "%s!", tn);
                tb_box(tb, l1, l2);
                uint32_t won = post->player.money > pre->money ? post->player.money - pre->money : 0;
                if (won > 0) {
                    snprintf(l1, sizeof(l1), "%s got $%u", plr, (unsigned)won);
                    tb_box(tb, l1, "for winning!");
                }
            } else if (post->battle.enemy_active_index != pre->bs.enemy_active_index) {
                int spare = 0;
                for (int pi = 0; pi < post->party_count; pi++)
                    if (pi != pre->active_slot && post->party[pi].box.hp > 0) spare++;
                const char *tn = pre->bs.trainer_class_index < RC_TRAINER_NAMES_COUNT
                                     ? RC_TRAINER_NAMES[pre->bs.trainer_class_index] : "TRAINER";
                const char *nm = rc_species_name(post->battle.enemy.species);
                if (spare > 0 && post->mode == GAME_MODE_BATTLE) {
                    snprintf(l1, sizeof(l1), "%s is", tn);
                    snprintf(l2, sizeof(l2), "about to use");
                    char l3[RC_TEXT_LINE * 2];
                    snprintf(l3, sizeof(l3), "%s!", nm);
                    tb_box3(tb, l1, l2, l3);
                    snprintf(l1, sizeof(l1), "Will %s", plr);
                    tb_frame(tb, l1, "change POK\001MON?", tb->base | RCF_BOX | RCF_YESNO);
                }
                snprintf(l1, sizeof(l1), "%s sent", tn);
                snprintf(l2, sizeof(l2), "out %s!", nm);
                *foe = rc_disp_from_battle(&post->battle.enemy);
                tb->base |= RCF_PIC_FOE;
                tb_auto(tb, 2, 4, l1, l2);
                tb_blank(tb, 1);
            }
        }
    }

    if (me_fainted) {
        me->hp = 0;
        tb->base &= ~RCF_PIC_ME;
        tb_box(tb, pn, "fainted!");
        if (post->mode == GAME_MODE_BATTLE_SWITCH) {
            tb_frame(tb, "Use next POK\001MON?", "", tb->base | RCF_BOX | RCF_YESNO);
        } else {
            snprintf(l1, sizeof(l1), "%s is out of", plr);
            tb_box(tb, l1, "useable POK\001MON!");
            snprintf(l1, sizeof(l1), "%s blacked", plr);
            tb_box(tb, l1, "out!");
        }
    }
}

static void rc_narrate_intro(RcTextBuilder *tb, const GameState *gs) {
    char l1[RC_TEXT_LINE * 2], l2[RC_TEXT_LINE * 2], nick[24];
    const BattleState *bs = &gs->battle;
    const bool trainer = bs->is_trainer_battle != 0;
    tb->foe = rc_disp_from_battle(&bs->enemy);
    tb->me = rc_disp_from_battle(&bs->player);

    tb->base = 0;
    for (int i = 0; i < 4; i++) tb_frame(tb, "", "", RCF_OVERWORLD);
    for (int i = 0; i < 3; i++) tb_frame(tb, "", "", RCF_BLACK);
    tb->base = RCF_PIC_FOE | RCF_PIC_TRAINER;
    tb_blank(tb, 3);
    const char *tn = bs->trainer_class_index < RC_TRAINER_NAMES_COUNT ? RC_TRAINER_NAMES[bs->trainer_class_index] : "TRAINER";
    if (trainer) {
        tb->base &= ~RCF_PIC_FOE;
        snprintf(l1, sizeof(l1), "%s wants", tn);
        tb_box(tb, l1, "to fight!");
        tb_blank(tb, 2);
        snprintf(l1, sizeof(l1), "%s sent", tn);
        snprintf(l2, sizeof(l2), "out %s!", rc_species_name(bs->enemy.species));
        tb->base |= RCF_PIC_FOE | RCF_HUD_FOE;
        tb_auto(tb, 2, 4, l1, l2);
        tb_blank(tb, 1);
    } else {
        snprintf(l1, sizeof(l1), "Wild %s", rc_species_name(bs->enemy.species));
        tb_box(tb, l1, "appeared!");
        tb->base |= RCF_HUD_FOE;
        tb_blank(tb, 2);
    }
    snprintf(l1, sizeof(l1), "Go! %s!", rc_nick(gs, gs->active_party_slot, nick, sizeof(nick)));
    tb->base |= RCF_HUD_ME;
    tb_auto(tb, 2, 2, l1, "");
    tb->base &= ~RCF_PIC_TRAINER;
    tb->base |= RCF_PIC_ME;
    tb_hold(tb, 2, l1, "");
    tb_blank(tb, 1);
}

#endif
