#ifndef REDCORE_OCEAN_SCREEN_H
#define REDCORE_OCEAN_SCREEN_H

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define RC_FRAME_W 160
#define RC_FRAME_H 144
#define RC_FRAME_PIXELS (RC_FRAME_W * RC_FRAME_H)
#define RC_TILE_PX 8

#define NUM_TILESET_ASSETS 24
static const char *const RC_TILESET_PNG_FILENAMES[NUM_TILESET_ASSETS] = {
    "00_Overworld.png", "01_RedsHouse1.png", "02_Mart.png", "03_Forest.png",
    "04_RedsHouse2.png", "05_Dojo.png", "06_Pokecenter.png", "07_Gym.png",
    "08_House.png", "09_ForestGate.png", "10_Museum.png", "11_Underground.png",
    "12_Gate.png", "13_Ship.png", "14_ShipPort.png", "15_Cemetery.png",
    "16_Interior.png", "17_Cavern.png", "18_Lobby.png", "19_Mansion.png",
    "20_Lab.png", "21_Club.png", "22_Facility.png", "23_Plateau.png",
};

#define NUM_SPRITE_ASSETS 73
static const char *const RC_SPRITE_PNG_FILENAMES[NUM_SPRITE_ASSETS] = {
    0, "01_red.png", "02_blue.png", "03_oak.png", "04_youngster.png", "05_monster.png",
    "06_cooltrainer_f.png", "07_cooltrainer_m.png", "08_little_girl.png", "09_bird.png", "10_middle_aged_man.png", "11_gambler.png",
    "12_super_nerd.png", "13_girl.png", "14_hiker.png", "15_beauty.png", "16_gentleman.png", "17_daisy.png",
    "18_biker.png", "19_sailor.png", "20_cook.png", "21_bike_shop_clerk.png", "22_mr_fuji.png", "23_giovanni.png",
    "24_rocket.png", "25_channeler.png", "26_waiter.png", "27_silph_worker_f.png", "28_middle_aged_woman.png", "29_brunette_girl.png",
    "30_lance.png", 0, "32_scientist.png", "33_rocker.png", "34_swimmer.png", "35_safari_zone_worker.png",
    "36_gym_guide.png", "37_gramps.png", "38_clerk.png", "39_fishing_guru.png", "40_granny.png", "41_nurse.png",
    "42_link_receptionist.png", "43_silph_president.png", "44_silph_worker_m.png", "45_warden.png", "46_captain.png", "47_fisher.png",
    "48_koga.png", "49_guard.png", 0, "51_mom.png", "52_balding_guy.png", "53_little_boy.png",
    0, "55_gameboy_kid.png", "56_fairy.png", "57_agatha.png", "58_bruno.png", "59_lorelei.png",
    "60_seel.png", "61_poke_ball.png", "62_fossil.png", "63_boulder.png", "64_paper.png", "65_pokedex.png",
    "66_clipboard.png", "67_snorlax.png", 0, "69_old_amber.png", 0, 0,
    "72_gambler_asleep.png",
};

#define NUM_POKEMON_PIC_ASSETS 191
static const char *const RC_POKEMON_FRONT_PNG_FILENAMES[NUM_POKEMON_PIC_ASSETS] = {
    0, "001_rhydon.png", "002_kangaskhan.png", "003_nidoran_m.png", "004_clefairy.png", "005_spearow.png",
    "006_voltorb.png", "007_nidoking.png", "008_slowbro.png", "009_ivysaur.png", "010_exeggutor.png", "011_lickitung.png",
    "012_exeggcute.png", "013_grimer.png", "014_gengar.png", "015_nidoran_f.png", "016_nidoqueen.png", "017_cubone.png",
    "018_rhyhorn.png", "019_lapras.png", "020_arcanine.png", 0, "022_gyarados.png", "023_shellder.png",
    "024_tentacool.png", "025_gastly.png", "026_scyther.png", "027_staryu.png", "028_blastoise.png", "029_pinsir.png",
    "030_tangela.png", 0, 0, "033_growlithe.png", "034_onix.png", "035_fearow.png",
    "036_pidgey.png", "037_slowpoke.png", "038_kadabra.png", "039_graveler.png", "040_chansey.png", "041_machoke.png",
    "042_mr_mime.png", "043_hitmonlee.png", "044_hitmonchan.png", "045_arbok.png", "046_parasect.png", "047_psyduck.png",
    "048_drowzee.png", "049_golem.png", 0, "051_magmar.png", 0, "053_electabuzz.png",
    "054_magneton.png", "055_koffing.png", 0, "057_mankey.png", "058_seel.png", "059_diglett.png",
    "060_tauros.png", 0, 0, 0, "064_farfetchd.png", "065_venonat.png",
    "066_dragonite.png", 0, 0, 0, "070_doduo.png", "071_poliwag.png",
    "072_jynx.png", "073_moltres.png", "074_articuno.png", "075_zapdos.png", "076_ditto.png", "077_meowth.png",
    "078_krabby.png", 0, 0, 0, "082_vulpix.png", "083_ninetales.png",
    "084_pikachu.png", "085_raichu.png", 0, 0, "088_dratini.png", "089_dragonair.png",
    "090_kabuto.png", "091_kabutops.png", "092_horsea.png", "093_seadra.png", 0, 0,
    "096_sandshrew.png", "097_sandslash.png", "098_omanyte.png", "099_omastar.png", "100_jigglypuff.png", "101_wigglytuff.png",
    "102_eevee.png", "103_flareon.png", "104_jolteon.png", "105_vaporeon.png", "106_machop.png", "107_zubat.png",
    "108_ekans.png", "109_paras.png", "110_poliwhirl.png", "111_poliwrath.png", "112_weedle.png", "113_kakuna.png",
    "114_beedrill.png", 0, "116_dodrio.png", "117_primeape.png", "118_dugtrio.png", "119_venomoth.png",
    "120_dewgong.png", 0, 0, "123_caterpie.png", "124_metapod.png", "125_butterfree.png",
    "126_machamp.png", 0, "128_golduck.png", "129_hypno.png", "130_golbat.png", "131_mewtwo.png",
    "132_snorlax.png", "133_magikarp.png", 0, 0, "136_muk.png", 0,
    "138_kingler.png", "139_cloyster.png", 0, "141_electrode.png", "142_clefable.png", "143_weezing.png",
    "144_persian.png", "145_marowak.png", 0, "147_haunter.png", "148_abra.png", "149_alakazam.png",
    "150_pidgeotto.png", "151_pidgeot.png", "152_starmie.png", "153_bulbasaur.png", "154_venusaur.png", "155_tentacruel.png",
    0, "157_goldeen.png", "158_seaking.png", 0, 0, 0,
    0, "163_ponyta.png", "164_rapidash.png", "165_rattata.png", "166_raticate.png", "167_nidorino.png",
    "168_nidorina.png", "169_geodude.png", "170_porygon.png", "171_aerodactyl.png", 0, "173_magnemite.png",
    0, 0, "176_charmander.png", "177_squirtle.png", "178_charmeleon.png", "179_wartortle.png",
    "180_charizard.png", 0, 0, 0, 0, "185_oddish.png",
    "186_gloom.png", "187_vileplume.png", "188_bellsprout.png", "189_weepinbell.png", "190_victreebel.png",
};
static const char *const RC_POKEMON_BACK_PNG_FILENAMES[NUM_POKEMON_PIC_ASSETS] = {
    0, "001_rhydon.png", "002_kangaskhan.png", "003_nidoran_m.png", "004_clefairy.png", "005_spearow.png",
    "006_voltorb.png", "007_nidoking.png", "008_slowbro.png", "009_ivysaur.png", "010_exeggutor.png", "011_lickitung.png",
    "012_exeggcute.png", "013_grimer.png", "014_gengar.png", "015_nidoran_f.png", "016_nidoqueen.png", "017_cubone.png",
    "018_rhyhorn.png", "019_lapras.png", "020_arcanine.png", 0, "022_gyarados.png", "023_shellder.png",
    "024_tentacool.png", "025_gastly.png", "026_scyther.png", "027_staryu.png", "028_blastoise.png", "029_pinsir.png",
    "030_tangela.png", 0, 0, "033_growlithe.png", "034_onix.png", "035_fearow.png",
    "036_pidgey.png", "037_slowpoke.png", "038_kadabra.png", "039_graveler.png", "040_chansey.png", "041_machoke.png",
    "042_mr_mime.png", "043_hitmonlee.png", "044_hitmonchan.png", "045_arbok.png", "046_parasect.png", "047_psyduck.png",
    "048_drowzee.png", "049_golem.png", 0, "051_magmar.png", 0, "053_electabuzz.png",
    "054_magneton.png", "055_koffing.png", 0, "057_mankey.png", "058_seel.png", "059_diglett.png",
    "060_tauros.png", 0, 0, 0, "064_farfetchd.png", "065_venonat.png",
    "066_dragonite.png", 0, 0, 0, "070_doduo.png", "071_poliwag.png",
    "072_jynx.png", "073_moltres.png", "074_articuno.png", "075_zapdos.png", "076_ditto.png", "077_meowth.png",
    "078_krabby.png", 0, 0, 0, "082_vulpix.png", "083_ninetales.png",
    "084_pikachu.png", "085_raichu.png", 0, 0, "088_dratini.png", "089_dragonair.png",
    "090_kabuto.png", "091_kabutops.png", "092_horsea.png", "093_seadra.png", 0, 0,
    "096_sandshrew.png", "097_sandslash.png", "098_omanyte.png", "099_omastar.png", "100_jigglypuff.png", "101_wigglytuff.png",
    "102_eevee.png", "103_flareon.png", "104_jolteon.png", "105_vaporeon.png", "106_machop.png", "107_zubat.png",
    "108_ekans.png", "109_paras.png", "110_poliwhirl.png", "111_poliwrath.png", "112_weedle.png", "113_kakuna.png",
    "114_beedrill.png", 0, "116_dodrio.png", "117_primeape.png", "118_dugtrio.png", "119_venomoth.png",
    "120_dewgong.png", 0, 0, "123_caterpie.png", "124_metapod.png", "125_butterfree.png",
    "126_machamp.png", 0, "128_golduck.png", "129_hypno.png", "130_golbat.png", "131_mewtwo.png",
    "132_snorlax.png", "133_magikarp.png", 0, 0, "136_muk.png", 0,
    "138_kingler.png", "139_cloyster.png", 0, "141_electrode.png", "142_clefable.png", "143_weezing.png",
    "144_persian.png", "145_marowak.png", 0, "147_haunter.png", "148_abra.png", "149_alakazam.png",
    "150_pidgeotto.png", "151_pidgeot.png", "152_starmie.png", "153_bulbasaur.png", "154_venusaur.png", "155_tentacruel.png",
    0, "157_goldeen.png", "158_seaking.png", 0, 0, 0,
    0, "163_ponyta.png", "164_rapidash.png", "165_rattata.png", "166_raticate.png", "167_nidorino.png",
    "168_nidorina.png", "169_geodude.png", "170_porygon.png", "171_aerodactyl.png", 0, "173_magnemite.png",
    0, 0, "176_charmander.png", "177_squirtle.png", "178_charmeleon.png", "179_wartortle.png",
    "180_charizard.png", 0, 0, 0, 0, "185_oddish.png",
    "186_gloom.png", "187_vileplume.png", "188_bellsprout.png", "189_weepinbell.png", "190_victreebel.png",
};

typedef struct RedcoreAtlas {
    uint8_t *gray;
    int w, h;
} RedcoreAtlas;

static RedcoreAtlas g_redcore_atlas[NUM_TILESET_ASSETS];
static RedcoreAtlas g_redcore_sprite_atlas[NUM_SPRITE_ASSETS];
static RedcoreAtlas g_redcore_front_atlas[NUM_POKEMON_PIC_ASSETS];
static RedcoreAtlas g_redcore_back_atlas[NUM_POKEMON_PIC_ASSETS];
static pthread_once_t g_redcore_atlas_once = PTHREAD_ONCE_INIT;
static char g_redcore_assets_dir[512];

static void load_atlas(const char *subdir, const char *filename, RedcoreAtlas *out) {
    out->gray = NULL;
    if (!filename) return;
    char path[768];
    snprintf(path, sizeof(path), "%s/%s/%s", g_redcore_assets_dir, subdir, filename);
    Image img = LoadImage(path);
    if (!img.data) {
        fprintf(stderr, "redcore: could not load image %s (will be blank)\n", path);
        return;
    }
    Color *px = LoadImageColors(img);
    uint8_t *gray = (uint8_t *)malloc((size_t)img.width * img.height);
    for (int p = 0; p < img.width * img.height; p++) {

        gray[p] = (uint8_t)((px[p].r * 77 + px[p].g * 150 + px[p].b * 29) >> 8);
    }
    UnloadImageColors(px);
    out->gray = gray;
    out->w = img.width;
    out->h = img.height;
    UnloadImage(img);
}

static void redcore_load_atlases_impl(void) {
    SetTraceLogLevel(LOG_WARNING);
    for (int i = 0; i < NUM_TILESET_ASSETS; i++) load_atlas("tilesets", RC_TILESET_PNG_FILENAMES[i], &g_redcore_atlas[i]);
    for (int i = 0; i < NUM_SPRITE_ASSETS; i++) load_atlas("sprites", RC_SPRITE_PNG_FILENAMES[i], &g_redcore_sprite_atlas[i]);
    for (int i = 0; i < NUM_POKEMON_PIC_ASSETS; i++) {
        load_atlas("pokemon/front", RC_POKEMON_FRONT_PNG_FILENAMES[i], &g_redcore_front_atlas[i]);
        load_atlas("pokemon/back", RC_POKEMON_BACK_PNG_FILENAMES[i], &g_redcore_back_atlas[i]);
    }
}

static void redcore_load_atlases(const char *assets_dir) {

    if (assets_dir) snprintf(g_redcore_assets_dir, sizeof(g_redcore_assets_dir), "%s", assets_dir);
    pthread_once(&g_redcore_atlas_once, redcore_load_atlases_impl);
}

static void rc_fill(uint8_t *f, int x0, int y0, int x1, int y1, uint8_t v) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > RC_FRAME_W) x1 = RC_FRAME_W;
    if (y1 > RC_FRAME_H) y1 = RC_FRAME_H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) f[y * RC_FRAME_W + x] = v;
}

static void rc_box(uint8_t *f, int x0, int y0, int x1, int y1, uint8_t fill, uint8_t border) {
    rc_fill(f, x0, y0, x1, y1, border);
    rc_fill(f, x0 + 1, y0 + 1, x1 - 1, y1 - 1, fill);
}

static void rc_hp_bar(uint8_t *f, int x, int y, int w, float frac) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    rc_box(f, x, y, x + w, y + 5, 255, 0);
    rc_fill(f, x + 1, y + 1, x + 1 + (int)((w - 2) * frac), y + 4, 0);
}

static void rc_sprite(uint8_t *f, int x, int y, uint8_t body) {
    rc_fill(f, x + 2, y + 1, x + 14, y + 16, body);
    rc_fill(f, x + 2, y + 1, x + 14, y + 5, 0);
}

static void rc_blit(uint8_t *f, const RedcoreAtlas *atlas, int x, int y) {
    if (!atlas || !atlas->gray) return;
    for (int py = 0; py < atlas->h; py++) {
        int dy = y + py;
        if (dy < 0 || dy >= RC_FRAME_H) continue;
        const uint8_t *src = &atlas->gray[py * atlas->w];
        uint8_t *dst = &f[dy * RC_FRAME_W];
        for (int px = 0; px < atlas->w; px++) {
            int dx = x + px;
            if (dx < 0 || dx >= RC_FRAME_W) continue;
            dst[dx] = src[px];
        }
    }
}

static void rc_blit_sprite_pose(uint8_t *f, const RedcoreAtlas *atlas, Direction dir, int x, int y) {
    if (!atlas || !atlas->gray) return;
    int num_poses = atlas->h / 16;
    int row = 0, flip = 0;
    if (num_poses >= 3) {
        switch (dir) {
            case DIR_NORTH: row = 1; break;
            case DIR_WEST: row = 2; break;
            case DIR_EAST: row = 2; flip = 1; break;
            case DIR_SOUTH:
            default: row = 0; break;
        }
    }
    for (int py = 0; py < 16; py++) {
        int dy = y + py;
        if (dy < 0 || dy >= RC_FRAME_H) continue;
        int src_row = row * 16 + py;
        if (src_row >= atlas->h) continue;
        const uint8_t *src = &atlas->gray[src_row * atlas->w];
        uint8_t *dst = &f[dy * RC_FRAME_W];
        for (int px = 0; px < 16; px++) {
            int dx = x + px;
            if (dx < 0 || dx >= RC_FRAME_W) continue;
            dst[dx] = src[flip ? (16 - 1 - px) : px];
        }
    }
}


static void redcore_draw_overworld(const GameState *gs, uint8_t *f) {
    const MapInfo *info = &MAP_INFO[gs->player.map_id];
    uint8_t oob = (info->valid && info->tileset_id == 0) ? 85 : 0;
    const RedcoreAtlas *atlas = (info->valid && info->has_blocks) ? &g_redcore_atlas[info->tileset_id] : NULL;
    if (atlas && !atlas->gray) atlas = NULL;
    int cols = atlas ? atlas->w / RC_TILE_PX : 1;

    int cam_x = gs->player.x * 2 - 8;
    int cam_y = gs->player.y * 2 - 8;
    for (int ty = 0; ty < RC_FRAME_H / RC_TILE_PX; ty++) {
        for (int tx = 0; tx < RC_FRAME_W / RC_TILE_PX; tx++) {
            int16_t tile = atlas ? overworld_tile_at(gs->player.map_id, (int16_t)(cam_x + tx), (int16_t)(cam_y + ty))
                                 : (int16_t)-1;
            if (tile < 0) {
                rc_fill(f, tx * RC_TILE_PX, ty * RC_TILE_PX, (tx + 1) * RC_TILE_PX, (ty + 1) * RC_TILE_PX, oob);
                continue;
            }
            int sx = (tile % cols) * RC_TILE_PX;
            int sy = (tile / cols) * RC_TILE_PX;
            for (int py = 0; py < RC_TILE_PX; py++) {
                int src_row = sy + py;
                if (src_row >= atlas->h) break;
                const uint8_t *src = &atlas->gray[src_row * atlas->w + sx];
                uint8_t *dst = &f[(ty * RC_TILE_PX + py) * RC_FRAME_W + tx * RC_TILE_PX];
                for (int px = 0; px < RC_TILE_PX; px++) dst[px] = src[px];
            }
        }
    }

    if (info->valid) {
        for (uint16_t i = 0; i < info->num_objects; i++) {
            uint16_t gi = (uint16_t)(info->objects_offset + i);
            const MapObjectEvent *o = &MAP_OBJECT_EVENTS[gi];
            if (!overworld_object_visible(gs, gi)) continue;
            int16_t eox, eoy;
            overworld_object_effective_pos(gs, gi, &eox, &eoy);
            int ox = eox, oy = eoy;
            int sx = 64 + (ox - (int)gs->player.x) * 16;
            int sy = 60 + (oy - (int)gs->player.y) * 16;
            if (sx <= -16 || sx >= RC_FRAME_W || sy <= -16 || sy >= RC_FRAME_H) continue;
            const RedcoreAtlas *satlas =
                (o->sprite_id < NUM_SPRITE_ASSETS) ? &g_redcore_sprite_atlas[o->sprite_id] : NULL;
            if (satlas && satlas->gray) {
                rc_blit_sprite_pose(f, satlas, overworld_object_facing(gs, gi), sx, sy);
            } else {
                rc_sprite(f, sx, sy, o->kind == OBJEVENT_ITEM ? 170 : 85);
            }
        }
    }
    const RedcoreAtlas *player_atlas = (SPRITE_RED < NUM_SPRITE_ASSETS) ? &g_redcore_sprite_atlas[SPRITE_RED] : NULL;
    if (player_atlas && player_atlas->gray) {
        rc_blit_sprite_pose(f, player_atlas, (Direction)gs->player.direction, 64, 60);
    } else {
        rc_sprite(f, 64, 60, 85);
    }
}

static void rc_cursor(uint8_t *f, int x, int y) { rc_fill(f, x, y, x + 5, y + 6, 0); }

static float rc_battle_hp_frac(const BattleMon *m) {
    return m->max_hp > 0 ? (float)(m->hp < 0 ? 0 : m->hp) / (float)m->max_hp : 0.0f;
}

static void rc_line(uint8_t *f, int x, int y, int w) { rc_fill(f, x, y, x + w, y + 3, 85); }

static void rc_glyph(uint8_t *f, int x, int y, char c, uint8_t color);
static void rc_text(uint8_t *f, int x, int y, const char *s, uint8_t color);
static void rc_dialogue_box(uint8_t *f, const char *text, bool more);

static void redcore_draw_battle(const RcEnv *env, uint8_t *f) {
    const GameState *gs = &env->gstate;
    const BattleState *bs = &gs->battle;
    memset(f, 255, RC_FRAME_PIXELS);

    {
        const RedcoreAtlas *front =
            (bs->enemy.species < NUM_POKEMON_PIC_ASSETS) ? &g_redcore_front_atlas[bs->enemy.species] : NULL;
        if (front && front->gray) rc_blit(f, front, 92, 2);
        else rc_box(f, 96, 8, 144, 56, 85, 0);
    }
    rc_hp_bar(f, 8, 12, 64, rc_battle_hp_frac(&bs->enemy));

    if (gs->mode != GAME_MODE_SAFARI_BATTLE) {
        const RedcoreAtlas *back =
            (bs->player.species < NUM_POKEMON_PIC_ASSETS) ? &g_redcore_back_atlas[bs->player.species] : NULL;
        if (back && back->gray) rc_blit(f, back, 20, 56);
        else rc_box(f, 16, 48, 64, 96, 85, 0);
        rc_hp_bar(f, 88, 76, 64, rc_battle_hp_frac(&bs->player));
    }

    if (gs->mode == GAME_MODE_BATTLE_SWITCH ||
        (gs->mode == GAME_MODE_BATTLE && (env->battle_menu == RC_MENU_PARTY || (env->battle_menu == RC_MENU_ITEM && env->item_stage == 1)))) {

        memset(f, 255, RC_FRAME_PIXELS);
        for (int i = 0; i < gs->party_count && i < 6; i++) {
            PartyMon m = rc_party_mon(gs, i);
            int y = 6 + i * 22;
            rc_box(f, 14, y, 30, y + 16, 85, 0);
            rc_line(f, 36, y + 2, 40);
            rc_hp_bar(f, 36, y + 9, 80, m.max_hp > 0 ? (float)m.box.hp / (float)m.max_hp : 0.0f);
            if (i == env->cursor_party) rc_cursor(f, 4, y + 5);
        }
        if (gs->mode == GAME_MODE_BATTLE && env->battle_menu == RC_MENU_ITEM) {
            // "Use item on which POKeMON?" -- the same list, with the question in the text box.
            rc_dialogue_box(f, "Use item on which POKeMON?", false);
        } else if (gs->mode == GAME_MODE_BATTLE && env->battle_menu == RC_MENU_PARTY && env->party_stage == 0) {
            rc_dialogue_box(f, "Choose a POKeMON.", false);
        }
        if (env->party_stage == 1) {  // SWITCH / STATS / CANCEL box, as in the real party menu
            rc_box(f, 88, 78, RC_FRAME_W, RC_FRAME_H, 255, 0);
            static const char *const LABELS[3] = {"SWITCH", "STATS", "CANCEL"};
            for (int i = 0; i < 3; i++) {
                rc_text(f, 104, 86 + i * 16, LABELS[i], 0);
                if (i == env->cursor_action) rc_glyph(f, 94, 86 + i * 16, '>', 0);
            }
        } else if (env->party_stage == 3) {
            rc_dialogue_box(f, env->party_msg == 1 ? "That POKeMON is already out!" : "There's no will to fight!", true);
        } else if (env->party_stage == 2) {  // stats page
            memset(f, 255, RC_FRAME_PIXELS);
            PartyMon m = rc_party_mon(gs, env->cursor_party < gs->party_count ? env->cursor_party : 0);
            char line[40];
            snprintf(line, sizeof(line), "HP %u/%u", m.box.hp, m.max_hp);
            rc_text(f, 8, 8, line, 0);
            snprintf(line, sizeof(line), "LV %u", m.level);
            rc_text(f, 8, 22, line, 0);
            snprintf(line, sizeof(line), "ATK %u  DEF %u", m.attack, m.defense);
            rc_text(f, 8, 36, line, 0);
            snprintf(line, sizeof(line), "SPD %u  SPC %u", m.speed, m.special);
            rc_text(f, 8, 50, line, 0);
        }
        return;
    }

    rc_box(f, 0, 104, RC_FRAME_W, RC_FRAME_H, 255, 0);
    if (gs->mode == GAME_MODE_SAFARI_BATTLE ||
        (gs->mode == GAME_MODE_BATTLE && env->battle_menu == RC_MENU_MAIN)) {

        static const int W[4] = {26, 20, 16, 12};
        rc_box(f, 72, 104, RC_FRAME_W, RC_FRAME_H, 255, 0);
        for (int c = 0; c < 4; c++) {
            int x = 88 + (c & 1) * 36, y = 114 + (c >> 1) * 14;
            rc_line(f, x, y, W[c]);
            if (c == env->cursor_main) rc_cursor(f, x - 8, y - 2);
        }
    } else if (gs->mode == GAME_MODE_BATTLE && env->battle_menu == RC_MENU_FIGHT) {
        rc_box(f, 32, 104, RC_FRAME_W, RC_FRAME_H, 255, 0);
        int n = rc_move_count(gs);
        for (int i = 0; i < n; i++) {
            uint8_t max_pp = MOVES[bs->player.moves[i]].pp;
            int w = 8 + (max_pp > 0 ? (int)(40.0f * (bs->player.pp[i] & 0x3F) / max_pp) : 0);
            int y = 108 + i * 9;
            rc_line(f, 48, y, w);
            if (i == env->cursor_fight) rc_cursor(f, 38, y - 2);
        }
    } else if (gs->mode == GAME_MODE_BATTLE && env->battle_menu == RC_MENU_ITEM && env->item_stage != 1) {
        // The bag: a 3-row list ending in CANCEL, item name over its "x quantity".
        rc_box(f, 32, 16, RC_FRAME_W, 104, 255, 0);
        int entries = gs->bag.num_slots + 1;
        for (int row = 0; row < 3; row++) {
            int i = env->bag_scroll + row;
            if (i >= entries) break;
            int y = 26 + row * 24;
            if (i == gs->bag.num_slots) {
                rc_text(f, 52, y, "CANCEL", 0);
            } else {
                char name[24], qty[8];
                rc_item_name(gs->bag.slots[i].item_id, name, sizeof(name));
                snprintf(qty, sizeof(qty), "x%u", gs->bag.slots[i].count);
                rc_text(f, 52, y, name, 0);
                rc_text(f, 112, y + 10, qty, 0);
            }
            if (i == env->cursor_item) rc_glyph(f, 40, y, '>', 0);
        }
        if (env->bag_scroll > 0) rc_glyph(f, 148, 20, '^', 0);
        if (env->bag_scroll + 3 < entries) rc_glyph(f, 148, 96, 'v', 0);
        if (env->item_stage == 2) {
            const char *msg = env->item_msg == RC_ITEM_MSG_NO_EFFECT ? "It won't have any effect."
                              : env->item_msg == RC_ITEM_MSG_BOX_FULL ? "The POKeMON BOX is full! Can't use that item now!"
                                                                       : "OAK: This isn't the time to use that!";
            rc_dialogue_box(f, msg, true);
        }
    }
}

static void redcore_draw_starter_select(const RcEnv *env, uint8_t *f) {
    memset(f, 255, RC_FRAME_PIXELS);
    for (int i = 0; i < 3; i++) {
        rc_box(f, 20 + i * 44, 60, 44 + i * 44, 84, 85, 0);
        if (i == env->cursor_party) rc_cursor(f, 29 + i * 44, 48);
    }
}


// ---- Text rendering ---------------------------------------------------------
// Classic 5x7 ASCII font (0x20..0x7E), one byte per column, LSB = top row
// (descenders use bit 7). Glyph advance is 6 px. Lets the synthesized frame show
// real dialogue / naming-screen text instead of placeholder bars.
#define RC_FONT_W 6
static const uint8_t RC_FONT5X7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x56, 0x20, 0x50},
    {0x00, 0x08, 0x07, 0x03, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x2A, 0x1C, 0x7F, 0x1C, 0x2A},
    {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x80, 0x70, 0x30, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x00, 0x60, 0x60, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x72, 0x49, 0x49, 0x49, 0x46},
    {0x21, 0x41, 0x49, 0x4D, 0x33},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x31},
    {0x41, 0x21, 0x11, 0x09, 0x07},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x46, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x00, 0x14, 0x00, 0x00},
    {0x00, 0x40, 0x34, 0x00, 0x00},
    {0x00, 0x08, 0x14, 0x22, 0x41},
    {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08},
    {0x02, 0x01, 0x59, 0x09, 0x06},
    {0x3E, 0x41, 0x5D, 0x59, 0x4E},
    {0x7C, 0x12, 0x11, 0x12, 0x7C},
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x41, 0x51, 0x73},
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x1C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x26, 0x49, 0x49, 0x49, 0x32},
    {0x03, 0x01, 0x7F, 0x01, 0x03},
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x03, 0x04, 0x78, 0x04, 0x03},
    {0x61, 0x59, 0x49, 0x4D, 0x43},
    {0x00, 0x7F, 0x41, 0x41, 0x41},
    {0x02, 0x04, 0x08, 0x10, 0x20},
    {0x00, 0x41, 0x41, 0x41, 0x7F},
    {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x03, 0x07, 0x08, 0x00},
    {0x20, 0x54, 0x54, 0x78, 0x40},
    {0x7F, 0x28, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x28},
    {0x38, 0x44, 0x44, 0x28, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x00, 0x08, 0x7E, 0x09, 0x02},
    {0x18, 0xA4, 0xA4, 0x9C, 0x78},
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x40, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    {0x7C, 0x04, 0x78, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38},
    {0xFC, 0x18, 0x24, 0x24, 0x18},
    {0x18, 0x24, 0x24, 0x18, 0xFC},
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x24},
    {0x04, 0x04, 0x3F, 0x44, 0x24},
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x4C, 0x90, 0x90, 0x90, 0x7C},
    {0x44, 0x64, 0x54, 0x4C, 0x44},
    {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x77, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x02, 0x01, 0x02, 0x04, 0x02}
};

static void rc_glyph(uint8_t *f, int x, int y, char c, uint8_t color) {
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *g = RC_FONT5X7[c - 0x20];
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 8; row++) {
            int px = x + col, py = y + row;
            if ((g[col] >> row) & 1 && px >= 0 && px < RC_FRAME_W && py >= 0 && py < RC_FRAME_H)
                f[py * RC_FRAME_W + px] = color;
        }
}

static void rc_text(uint8_t *f, int x, int y, const char *s, uint8_t color) {
    for (; *s; s++, x += RC_FONT_W) rc_glyph(f, x, y, *s, color);
}

// Word-wraps `s` (honoring '\n') to `cols` columns and draws the last `max_lines`
// lines, so a long dialogue scrolls like the real text box.
static void rc_text_wrapped(uint8_t *f, int x, int y, int line_h, int cols, int max_lines, const char *s) {
    char lines[16][40];
    int nlines = 0, len = 0;
    char word[40];
    int wlen = 0;
    lines[0][0] = '\0';
    #define RC_NEWLINE() do { if (nlines < 15) nlines++; lines[nlines][0] = '\0'; len = 0; } while (0)
    for (const char *p = s;; p++) {
        char c = *p;
        if (c == ' ' || c == '\n' || c == '\0') {
            if (wlen > 0) {
                if (len > 0 && len + 1 + wlen > cols) RC_NEWLINE();
                if (len > 0) lines[nlines][len++] = ' ';
                memcpy(lines[nlines] + len, word, wlen);
                len += wlen;
                lines[nlines][len] = '\0';
                wlen = 0;
            }
            if (c == '\n') RC_NEWLINE();
            if (c == '\0') break;
        } else if (wlen < cols) {
            word[wlen++] = c;
        }
    }
    #undef RC_NEWLINE
    int first = nlines + 1 > max_lines ? nlines + 1 - max_lines : 0;
    for (int i = first; i <= nlines; i++) rc_text(f, x, y + (i - first) * line_h, lines[i], 0);
}

// Bottom dialogue box shared by the overworld text box and the nickname prompt.
static void rc_dialogue_box(uint8_t *f, const char *text, bool more) {
    rc_box(f, 0, 96, RC_FRAME_W, RC_FRAME_H, 255, 0);
    rc_text_wrapped(f, 8, 103, 12, 24, 3, text);
    if (more) rc_glyph(f, 148, 134, 'v', 0);
}

static void redcore_draw_textbox(const GameState *gs, uint8_t *f) {
    rc_dialogue_box(f, gs->text.buffer, gs->text.waiting_for_input);
}

// Naming flow (GAME_MODE_NAMING): the YES/NO nickname question over the
// overworld, then the 9-column keyboard with the typed name above it.
static void redcore_draw_naming(const GameState *gs, uint8_t *f) {
    const NamingState *n = &gs->naming;
    if (n->stage == NAMING_STAGE_ASK) {
        rc_dialogue_box(f, "Do you want to give a nickname?", false);
        rc_box(f, 104, 56, RC_FRAME_W, 96, 255, 0);
        rc_text(f, 124, 66, "YES", 0);
        rc_text(f, 124, 80, "NO", 0);
        rc_glyph(f, 112, n->ask_cursor == 0 ? 66 : 80, '>', 0);
        return;
    }
    memset(f, 255, RC_FRAME_PIXELS);
    char name[40];
    snprintf(name, sizeof(name), "NAME: %s_", n->buffer);
    rc_text(f, 8, 6, name, 0);
    for (int r = 0; r < NAMING_GRID_ROWS - 1; r++) {
        for (int c = 0; c < NAMING_GRID_COLS; c++) {
            int x = 10 + c * 16, y = 24 + r * 14;
            bool end = r == NAMING_END_ROW && c == NAMING_END_COL;
            char ch = naming_grid_char(n->lowercase, r, c);
            bool sel = r == n->row && c == n->col;
            if (sel) rc_fill(f, x - 3, y - 2, x + (end ? 15 : 9), y + 10, 0);
            if (end) rc_text(f, x, y, "ED", sel ? 255 : 0);
            else rc_glyph(f, x, y, ch < 0x20 ? '?' : ch, sel ? 255 : 0);
        }
    }
    int cy = 24 + NAMING_CASE_ROW * 14;
    if (n->row == NAMING_CASE_ROW) rc_glyph(f, 10, cy, '>', 0);
    rc_text(f, 22, cy, n->lowercase ? "UPPER CASE" : "lower case", 0);
}

static void redcore_render_frame(const RcEnv *env, uint8_t *f) {
    const GameState *gs = &env->gstate;
    switch (gs->mode) {
        case GAME_MODE_BATTLE:
        case GAME_MODE_BATTLE_SWITCH:
        case GAME_MODE_SAFARI_BATTLE:
            redcore_draw_battle(env, f);
            break;
        case GAME_MODE_STARTER_SELECT:
            redcore_draw_starter_select(env, f);
            break;
        default:
            if (gs->mode == GAME_MODE_NAMING && gs->naming.stage == NAMING_STAGE_KEYBOARD) {
                redcore_draw_naming(gs, f);
                break;
            }
            redcore_draw_overworld(gs, f);
            if (gs->mode == GAME_MODE_TEXTBOX) redcore_draw_textbox(gs, f);
            else if (gs->mode == GAME_MODE_NAMING) redcore_draw_naming(gs, f);
            break;
    }
}

#endif
