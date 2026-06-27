#pragma once

// Depends on Terraria struct (included by terraria.h before this file)
// OBS_SIZE must equal the total below (512)

// Layout (512 floats total):
//   [  0.. 377] Tile window 21×9 × 2 channels (type_norm, wall_bit) = 378
//   [378.. 383] Player state (6)
//   [384.. 385] World state (2)
//   [386.. 449] Inventory 32 × 2 = 64
//   [450.. 455] Equipment 6
//   [456.. 487] Nearest 8 enemies × 4 = 32
//   [488.. 507] Crafting availability 20
//   [508.. 511] Padding zeros = 4

#define OBS_TILE_W  21
#define OBS_TILE_H   9

static inline float clamp01(float v) {
    return (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
}

static void encode_observation(Terraria* env) {
    float* obs = env->observations;
    if (!obs) return;

    const Player* p = &env->player;
    int player_tx = (int)p->px;
    int player_ty = (int)p->py;
    int idx = 0;

    int half_w = OBS_TILE_W / 2;  // 10
    int half_h = OBS_TILE_H / 2;  // 4
    for (int dr = -half_h; dr <= half_h; dr++) {
        for (int dc = -half_w; dc <= half_w; dc++) {
            int tx = player_tx + dc;
            int ty = player_ty + dr;
            uint8_t tile;
            if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) {
                // World boundary: treat as stone wall
                tile = (uint8_t)TILE_STONE;
            } else {
                tile = env->tiles[ty][tx];
            }
            obs[idx++] = (float)TILE_TYPE(tile) / (float)TILE_COUNT;
            obs[idx++] = (float)TILE_HAS_WALL(tile);
        }
    }
    // idx == 378

    obs[idx++] = clamp01((float)p->php / (float)p->pmax_hp);
    obs[idx++] = clamp01((float)p->pmax_hp / 400.0f);
    obs[idx++] = clamp01(p->pvx / (WALK_SPEED * 2.0f) + 0.5f);
    obs[idx++] = clamp01(p->pvy / MAX_FALL + 0.5f);
    obs[idx++] = (float)p->on_ground;
    obs[idx++] = (float)p->selected_slot / 31.0f;
    // idx == 384

    obs[idx++] = (float)env->day_tick / (float)(DAY_LENGTH * 2);
    float surface_y = (float)(WORLD_H / 4);
    float depth_norm = clamp01((p->py - surface_y) / (float)(WORLD_H - (int)surface_y));
    obs[idx++] = depth_norm;
    // idx == 386

    for (int s = 0; s < INV_SLOTS; s++) {
        obs[idx++] = (float)env->inventory.slots[s].item_id / (float)ITEM_COUNT;
        obs[idx++] = (float)env->inventory.slots[s].count / 99.0f;
    }
    // idx == 450

    obs[idx++] = (float)p->equip_pick    / (float)ITEM_COUNT;
    obs[idx++] = (float)p->equip_axe     / (float)ITEM_COUNT;
    obs[idx++] = (float)p->equip_weapon  / (float)ITEM_COUNT;
    obs[idx++] = (float)p->equip_helmet  / (float)ITEM_COUNT;
    obs[idx++] = (float)p->equip_chest   / (float)ITEM_COUNT;
    obs[idx++] = (float)p->equip_legs    / (float)ITEM_COUNT;
    // idx == 456

    // Simple selection: scan all, pick 8 nearest
    const EnemySoA* e = &env->enemies;
    int   near_idx[8];
    float near_dist[8];
    int   near_count = 0;
    for (int i = 0; i < 8; i++) near_dist[i] = 1e9f;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!e->eactive[i]) continue;
        float dx = e->ex[i] - p->px;
        float dy = e->ey[i] - p->py;
        float d2 = dx*dx + dy*dy;
        for (int k = 0; k < 8; k++) {
            if (d2 < near_dist[k]) {
                for (int j = 7; j > k; j--) {
                    near_dist[j] = near_dist[j-1];
                    near_idx[j]  = near_idx[j-1];
                }
                near_dist[k] = d2;
                near_idx[k]  = i;
                if (near_count < 8) near_count++;
                break;
            }
        }
    }

    for (int k = 0; k < 8; k++) {
        if (k < near_count && near_dist[k] < 1e8f) {
            int i = near_idx[k];
            int16_t mhp = ENEMY_PROPS[e->etype[i]].hp;
            obs[idx++] = clamp01((e->ex[i] - p->px) / 20.0f + 0.5f);
            obs[idx++] = clamp01((e->ey[i] - p->py) / 20.0f + 0.5f);
            obs[idx++] = (float)e->etype[i] / (float)ENEMY_COUNT;
            obs[idx++] = clamp01((float)e->ehp[i] / (float)(mhp > 0 ? mhp : 1));
        } else {
            obs[idx++] = 0.5f;
            obs[idx++] = 0.5f;
            obs[idx++] = 0.0f;
            obs[idx++] = 0.0f;
        }
    }
    // idx == 488

    int has_wb = has_station(env, TILE_WORKBENCH);
    int has_fn = has_station(env, TILE_FURNACE);
    for (int r = 0; r < NUM_RECIPES; r++) {
        const Recipe* rec = &RECIPES[r];
        int can = 1;
        if (rec->station == STATION_WORKBENCH && !has_wb) { can = 0; goto craft_done; }
        if (rec->station == STATION_FURNACE   && !has_fn) { can = 0; goto craft_done; }
        for (int j = 0; j < rec->n_in; j++) {
            if (rec->in_item[j] == ITEM_NONE) continue;
            if (inv_count(&env->inventory, rec->in_item[j]) < rec->in_count[j]) {
                can = 0; break;
            }
        }
        craft_done:
        obs[idx++] = (float)can;
    }
    // idx == 508

    obs[idx++] = 0.0f;
    obs[idx++] = 0.0f;
    obs[idx++] = 0.0f;
    obs[idx++] = 0.0f;
    // idx == 512
}
