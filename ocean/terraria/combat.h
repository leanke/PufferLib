#pragma once

// Depends on Terraria struct (included by terraria.h before this file)

#define ATTACK_CD_TICKS 20
#define KNOCKBACK_X     2.5f
#define PLAYER_IFRAMES  30
#define BOSS_TELEGRAPH_TICKS 20
#define BOSS_SOUL_DROP        3
#define HARDMODE_TRANSITION_REWARD 50.0f

static const int FOCUS_DELTA[10][2] = {
    { 0,  0},  // 0 - default (facing dir)
    {-1, +1},  // 1 - down-left
    { 0, +1},  // 2 - down
    {+1, +1},  // 3 - down-right
    {-1,  0},  // 4 - left
    { 0,  0},  // 5 - none (TODO: door interact)
    {+1,  0},  // 6 - right
    {-1, -1},  // 7 - up-left
    { 0, -1},  // 8 - up
    {+1, -1},  // 9 - up-right
};

// Resolves the tile targeted by focus_act (mine/attack/place all share this),
// and updates player.facing whenever the resolved direction has a horizontal
// component. Movement (in c_step) sets a baseline facing each tick; this can
// override it for that tick's target direction, e.g. attacking backward
// while retreating.
static void resolve_focus_tile(Terraria* env, int focus_act, int* out_tx, int* out_ty) {
    Player* p = &env->player;
    int ptx = (int)p->px;
    int pty = (int)(p->py);
    int mtx, mty;

    if (focus_act == 0 || focus_act == 5) {
        // Self/default: directly in front, using current facing (persisted from
        // the last horizontal focus). Try body tile, fall back to head tile.
        mtx = ptx + (int)p->facing;
        mty = pty;
        if (mty >= 0 && mty < WORLD_H && mtx >= 0 && mtx < WORLD_W) {
            TileType ft = TILE_TYPE(env->tiles[mty][mtx]);
            if (ft == TILE_AIR || ft == TILE_LEAVES)
                mty = pty - 1;
        }
    } else {
        int dx = FOCUS_DELTA[focus_act][0];
        int dy = FOCUS_DELTA[focus_act][1];
        if (dx != 0) p->facing = (dx > 0) ? 1 : -1;
        mtx = ptx + dx;
        if      (dy < 0) mty = (int)(p->py - PLAYER_HH) - 1;
        else if (dy > 0) mty = (int)(p->py + PLAYER_HH) + 1;
        else             mty = pty;
    }
    if (mtx < 0) mtx = 0;
    if (mtx >= WORLD_W) mtx = WORLD_W - 1;
    if (mty < 0) mty = 0;
    if (mty >= WORLD_H) mty = WORLD_H - 1;

    *out_tx = mtx;
    *out_ty = mty;
}

static float do_mine_or_attack(Terraria* env, int focus_act) {
    Player* p = &env->player;
    float reward = 0.0f;

    int mtx, mty;
    resolve_focus_tile(env, focus_act, &mtx, &mty);

    if (p->attack_cd == 0) {
        float atk_range = 2.0f;
        int best = -1;
        float best_dist = atk_range * atk_range;
        EnemySoA* e = &env->enemies;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!e->eactive[i]) continue;
            float dx = e->ex[i] - p->px;
            float dy = e->ey[i] - p->py;
            if ((int)p->facing == 1 && dx < 0.0f) continue; // must be facing the right way
            if ((int)p->facing ==-1 && dx > 0.0f) continue;
            float d2 = dx*dx + dy*dy;
            if (d2 < best_dist) { best_dist = d2; best = i; }
        }
        if (best >= 0) {
            int dmg = weapon_damage(p);
            e->ehp[best] -= (int16_t)dmg;
            float kdx = (e->ex[best] > p->px) ? KNOCKBACK_X : -KNOCKBACK_X;
            e->evx[best] += kdx;
            e->evy[best] = -2.0f;
            p->attack_cd = ATTACK_CD_TICKS;

            if (e->ehp[best] <= 0) {
                e->eactive[best] = 0;
                env->ep_enemies_killed += 1.0f;
                reward += 2.0f * (float)ENEMY_PROPS[e->etype[best]].xp;
                inv_add(&env->inventory, ITEM_COIN, ENEMY_PROPS[e->etype[best]].xp);

                if (e->etype[best] == ENEMY_BOSS) {
                    env->boss_slot = -1;
                    inv_add(&env->inventory, ITEM_BOSS_SOUL, BOSS_SOUL_DROP);
                    if (!env->hardmode) {
                        env->hardmode = 1;
                        env->ep_hardmode_reached = 1.0f;
                        reward += HARDMODE_TRANSITION_REWARD;
                    }
                }
            }
            return reward; // attack takes precedence over mining
        }
    }

    uint8_t tile = env->tiles[mty][mtx];
    TileType ttype = TILE_TYPE(tile);
    if (ttype == TILE_AIR || ttype == TILE_LEAVES) {
        p->mine_tx = -1; p->mine_ty = -1; p->mine_progress = 0;
        return reward;
    }

    const TileProps* tp = &TILE_PROPS[ttype];

    int tier = pick_tier(p);
    if (tier < tp->tool_min) {
        p->mine_tx = -1; p->mine_ty = -1; p->mine_progress = 0;
        return reward;
    }

    if (p->mine_tx != (int16_t)mtx || p->mine_ty != (int16_t)mty) {
        p->mine_tx = (int16_t)mtx;
        p->mine_ty = (int16_t)mty;
        p->mine_progress = 0;
    }

    // Mining damage by pick_tier(): hand=1,wood=1,stone=2,iron=4,silver=6,gold=8,cobalt=12
    static const uint8_t PICK_DMG[7] = {1, 1, 2, 4, 6, 8, 12};
    p->mine_progress += PICK_DMG[tier];

    if (p->mine_progress >= tp->hardness) {
        TILE_SET_TYPE(env->tiles[mty][mtx], TILE_AIR);
        p->mine_tx = -1; p->mine_ty = -1; p->mine_progress = 0;

        if (tp->drop_item != ITEM_NONE) {
            inv_add(&env->inventory, tp->drop_item, 1);
        }

        env->ep_tiles_mined += 1.0f;
        reward += 0.1f; // dense mining reward
        if (ttype == TILE_COAL)   reward += 0.2f;
        if (ttype == TILE_IRON)   reward += 0.5f;
        if (ttype == TILE_GOLD)   reward += 1.0f;
    }

    return reward;
}

static void do_place_block(Terraria* env, int focus_act) {
    Player* p = &env->player;
    int btx, bty;
    resolve_focus_tile(env, focus_act, &btx, &bty);
    if (btx < 0 || btx >= WORLD_W || bty < 0 || bty >= WORLD_H) return;

    if (TILE_TYPE(env->tiles[bty][btx]) != TILE_AIR) return;

    InvSlot* slot = &env->inventory.slots[p->selected_slot];
    if (slot->item_id == ITEM_NONE || slot->count == 0) return;

    uint8_t place_tile_type = ITEM_TO_TILE[slot->item_id];
    if (place_tile_type == 0) return;

    uint8_t tile = env->tiles[bty][btx];
    TILE_SET_TYPE(tile, place_tile_type);
    env->tiles[bty][btx] = tile;

    slot->count--;
    if (slot->count == 0) slot->item_id = ITEM_NONE;
}

static inline uint32_t wg_hash2(uint32_t seed, uint32_t x, uint32_t y);  // fwd decl from worldgen.h

// Applies one enemy hit to the player (damage, iframes, knockback). Shared by
// regular immediate-hit enemies and the boss's telegraphed-hit resolution, so
// the hardmode damage multiplier only needs to live in one place.
static float apply_enemy_hit(Terraria* env, float dx, const EnemyProps* ep) {
    Player* p = &env->player;
    int raw_dmg = (int)ep->dmg * (env->hardmode ? 2 : 1);
    int def = armor_defense(p);
    int actual = raw_dmg - def / 2;
    if (actual < 1) actual = 1;
    p->php -= (int16_t)actual;
    p->iframes = PLAYER_IFRAMES;
    p->pvx += (dx < 0.0f) ? -KNOCKBACK_X : KNOCKBACK_X;
    p->pvy = -2.0f;
    return -0.2f; // penalty for taking damage
}

static float update_enemies(Terraria* env) {
    float reward = 0.0f;
    EnemySoA* e = &env->enemies;
    Player* p = &env->player;
    uint32_t* rng = &env->rng;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!e->eactive[i]) continue;

        if (e->estun[i] > 0) { e->estun[i]--; continue; }

        if (e->eattack_cd[i] > 0) e->eattack_cd[i]--;
        if (e->ewander_cd[i] > 0) e->ewander_cd[i]--;

        float dx = p->px - e->ex[i];
        float dy = p->py - e->ey[i];
        float dist2 = dx*dx + dy*dy;
        float dist  = sqrtf(dist2);

        uint8_t etype = e->etype[i];
        const EnemyProps* ep = &ENEMY_PROPS[etype];
        float speed = ep->speed_x16 / 16.0f;

        if (dist < ep->aggro_range) {
            e->estate[i] = AI_CHASE;
        } else if (dist > (float)(ep->aggro_range + 4)) {
            e->estate[i] = AI_WANDER;
        }

        if (dist < (float)ep->attack_range) {
            e->estate[i] = AI_ATTACK;
        }

        switch (e->estate[i]) {
            case AI_WANDER:
                if (e->ewander_cd[i] == 0) {
                    *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
                    e->ewander_dir[i] = (*rng & 1) ? 1 : -1;
                    if (*rng % 4 == 0) e->ewander_dir[i] = 0; // idle
                    e->ewander_cd[i] = 30 + (*rng % 60);
                }
                e->evx[i] = e->ewander_dir[i] * speed * 0.5f;
                break;

            case AI_CHASE:
                if (ep->is_flying) {
                    float inv_dist = (dist > 0.01f) ? 1.0f / dist : 0.0f;
                    e->evx[i] = dx * inv_dist * speed;
                    e->evy[i] = dy * inv_dist * speed;
                } else {
                    e->evx[i] = (dx > 0.0f) ? speed : -speed;
                }
                break;

            case AI_ATTACK:
                e->evx[i] *= 0.5f; // slow down when attacking
                if (etype == ENEMY_BOSS) {
                    // Telegraphed: a wind-up precedes the hit, giving the agent
                    // a window to dodge instead of taking unavoidable damage.
                    if (e->etelegraph[i]) {
                        if (e->etelegraph_timer[i] > 0) {
                            e->etelegraph_timer[i]--;
                        } else {
                            if (p->iframes == 0 && dist < (float)ep->attack_range) {
                                reward += apply_enemy_hit(env, dx, ep);
                            }
                            e->etelegraph[i] = 0;
                            e->eattack_cd[i] = ep->attack_cd;
                        }
                    } else if (e->eattack_cd[i] == 0) {
                        e->etelegraph[i] = 1;
                        e->etelegraph_timer[i] = BOSS_TELEGRAPH_TICKS;
                    }
                } else if (e->eattack_cd[i] == 0 && p->iframes == 0) {
                    reward += apply_enemy_hit(env, dx, ep);
                    e->eattack_cd[i] = ep->attack_cd;
                }
                break;
        }

        enemy_physics(env, i);
    }
    return reward;
}

// Scans downward from the top of the world for the first solid tile with air
// above it — used to place a spawned entity standing on the ground.
static float find_ground_y(const Terraria* env, int tx, float fallback_y) {
    for (int ty = 0; ty < WORLD_H - 1; ty++) {
        if (TILE_TYPE(env->tiles[ty][tx]) != TILE_AIR &&
            TILE_TYPE(env->tiles[ty-1 < 0 ? 0 : ty-1][tx]) == TILE_AIR) {
            return (float)ty - ENEMY_HH - 0.05f;
        }
    }
    return fallback_y;
}

// Common slot initialization shared by regular spawns and spawn_boss. HP is
// scaled by the hardmode multiplier so a post-hardmode boss rematch (or any
// enemy spawned after the transition) is tougher, matching the doubled
// enemy HP/damage effect applied in apply_enemy_hit.
static void init_enemy_slot(Terraria* env, int slot, uint8_t etype, float x, float y, AIState state) {
    EnemySoA* e = &env->enemies;
    e->ex[slot]       = x;
    e->ey[slot]       = y;
    e->evx[slot]      = 0.0f;
    e->evy[slot]      = 0.0f;
    e->etype[slot]    = etype;
    e->ehp[slot]      = (int16_t)(ENEMY_PROPS[etype].hp * (env->hardmode ? 2 : 1));
    e->estate[slot]   = state;
    e->eactive[slot]  = 1;
    e->estun[slot]    = 0;
    e->eattack_cd[slot] = 0;
    e->ewander_cd[slot] = 0;
    e->ewander_dir[slot] = 1;
    e->etelegraph[slot] = 0;
    e->etelegraph_timer[slot] = 0;
}

static void try_spawn_enemy(Terraria* env) {
    if (enemies_count(&env->enemies) >= MAX_ENEMIES - 1) return;

    int slot = enemies_alloc(&env->enemies);
    if (slot < 0) return;

    uint32_t* rng = &env->rng;
    *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;

    // Spawn off-screen: 20-40 tiles from player
    float px = env->player.px;
    float py = env->player.py;
    int   dir = (*rng & 1) ? 1 : -1;
    float spawn_x = px + dir * (20.0f + (float)(*rng % 20));
    spawn_x = fmaxf(2.0f, fminf((float)(WORLD_W - 2), spawn_x));

    int sx = (int)spawn_x;
    float spawn_y = find_ground_y(env, sx, py);

    // Pick enemy type: night → more varied, day → mostly slimes. Excludes
    // ENEMY_BOSS (the last enum value) — bosses only spawn via summon item.
    uint8_t etype;
    if (env->is_blood_moon) {
        // Blood Moon: roll among the tougher roster only (no Slime), and
        // skip the depth-based Bat bias below so it can't water this down.
        static const uint8_t BLOOD_MOON_ROSTER[3] = { ENEMY_ZOMBIE, ENEMY_BAT, ENEMY_SKELETON };
        etype = BLOOD_MOON_ROSTER[(*rng >> 4) % 3];
    } else if (env->is_night) {
        etype = (uint8_t)((*rng >> 4) % (ENEMY_COUNT - 1));
    } else {
        etype = ENEMY_SLIME;
    }

    // Underground: bats more common
    float depth = py / (float)WORLD_H;
    if (!env->is_blood_moon && depth > 0.5f && (*rng & 3) == 0) etype = ENEMY_BAT;

    init_enemy_slot(env, slot, etype, spawn_x, spawn_y, AI_WANDER);
}

// Spawns the boss a few tiles in front of the player, facing them immediately.
static void spawn_boss(Terraria* env) {
    int slot = enemies_alloc(&env->enemies);
    if (slot < 0) return;

    Player* p = &env->player;
    float spawn_x = p->px + (float)p->facing * 8.0f;
    spawn_x = fmaxf(2.0f, fminf((float)(WORLD_W - 2), spawn_x));
    int sx = (int)spawn_x;
    float spawn_y = find_ground_y(env, sx, p->py);

    init_enemy_slot(env, slot, ENEMY_BOSS, spawn_x, spawn_y, AI_CHASE);
    env->boss_slot = (int8_t)slot;
}

#define HEALTH_POTION_HEAL 50

// tool_act==3: context-sensitive "use selected item" — the boss summon item
// (Phase 1) or a Merchant-bought Health Potion (Phase 4). Both are consumed
// from the selected slot the same way, so this stays a single dispatch point
// rather than growing new action branches per consumable.
static float do_interact(Terraria* env) {
    Player* p = &env->player;
    InvSlot* slot = &env->inventory.slots[p->selected_slot];

    if (slot->item_id == ITEM_BOSS_SUMMON && slot->count > 0 && env->boss_slot < 0) {
        spawn_boss(env);
        slot->count--;
        if (slot->count == 0) slot->item_id = ITEM_NONE;
    } else if (slot->item_id == ITEM_HEALTH_POTION && slot->count > 0) {
        p->php += HEALTH_POTION_HEAL;
        if (p->php > p->pmax_hp) p->php = p->pmax_hp;
        slot->count--;
        if (slot->count == 0) slot->item_id = ITEM_NONE;
    }

    return 0.0f;
}
